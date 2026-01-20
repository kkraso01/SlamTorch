#include "Renderer.h"
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <GLES3/gl3.h>
#include <assert.h>
#include <time.h>
#include "AndroidOut.h"

Renderer::Renderer(android_app *pApp) :
        app_(pApp),
        display_(EGL_NO_DISPLAY),
        surface_(EGL_NO_SURFACE),
        context_(EGL_NO_CONTEXT),
        width_(0),
        height_(0),
        shaderNeedsNewProjectionMatrix_(true) {
    
    initRenderer();

    // Attach JNI thread once (not per-frame for performance)
    if (app_->activity->vm->GetEnv((void**)&env_, JNI_VERSION_1_6) != JNI_OK) {
        app_->activity->vm->AttachCurrentThread(&env_, nullptr);
        jni_attached_ = true;
    }

    aout << "Creating ARCore SLAM..." << std::endl;
    ar_slam_ = std::make_unique<ArCoreSlam>(env_, app_->activity->javaGameActivity);
    if (ar_slam_) {
        ar_slam_->OnResume(env_);
        aout << "ARCore SLAM created and resumed successfully" << std::endl;
    }
    
    // Initialize renderers
    background_renderer_ = std::make_unique<BackgroundRenderer>();
    background_renderer_->Initialize();
    
    // CRITICAL: Set camera texture BEFORE first ArSession_update()
    if (ar_slam_ && ar_slam_->GetSession()) {
        background_renderer_->SetCameraTexture(ar_slam_->GetSession());
    }
    
    point_cloud_renderer_ = std::make_unique<PointCloudRenderer>();
    point_cloud_renderer_->Initialize();
    
    persistent_map_ = std::make_unique<PersistentPointMap>(500000);  // Production: 500k points
    
    // Initialize last-known matrices to identity
    for (int i = 0; i < 16; ++i) {
        last_good_view_[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        last_good_proj_[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
    
    // Initialize FPS tracking
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fps_last_time_ = ts.tv_sec + ts.tv_nsec / 1e9;
}

Renderer::~Renderer() {
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(display_, context_);
        }
        if (surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, surface_);
        }
        eglTerminate(display_);
    }
    
    // Detach JNI if we attached it
    if (jni_attached_ && app_->activity->vm) {
        app_->activity->vm->DetachCurrentThread();
    }
}

void Renderer::render() {
    updateRenderArea();
    
    // FPS calculation (zero allocation)
    frame_count_++;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double current_time = ts.tv_sec + ts.tv_nsec / 1e9;
    double delta = current_time - fps_last_time_;
    if (delta >= 1.0) {
        last_fps_ = frame_count_ / delta;
        frame_count_ = 0;
        fps_last_time_ = current_time;
    }

    // Clear screen
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Update ARCore SLAM (acquires frame, camera, point cloud)
    if (ar_slam_ && ar_slam_->GetSession()) {
        ar_slam_->Update(env_);
        
        ArTrackingState tracking_state = ar_slam_->GetTrackingState();
        static int log_counter = 0;
        static ArTrackingState last_state = AR_TRACKING_STATE_PAUSED;
        
        // Log state changes - NOTE: ARCore enum is: TRACKING=0, PAUSED=1, STOPPED=2
        if (tracking_state != last_state || log_counter % 60 == 0) {
            const char* state_str = (tracking_state == AR_TRACKING_STATE_TRACKING) ? "TRACKING" :
                                   (tracking_state == AR_TRACKING_STATE_PAUSED) ? "PAUSED" : "STOPPED";
            __android_log_print(ANDROID_LOG_INFO, "SlamTorch", "Tracking: %s (raw=%d)", state_str, tracking_state);
            last_state = tracking_state;
        }
        log_counter++;
        
        // 1. ALWAYS render background (camera feed) - even if not tracking yet
        background_renderer_->Draw(ar_slam_->GetSession(), ar_slam_->GetFrame());
        
        // 2. Accumulate and render 3D content
        if (tracking_state == AR_TRACKING_STATE_TRACKING) {
            // Get camera matrices for 3D rendering
            ar_slam_->GetViewMatrix(view_matrix_);
            ar_slam_->GetProjectionMatrix(0.1f, 100.0f, projection_matrix_);
            
            // Save good matrices for frozen rendering when tracking is lost
            for (int i = 0; i < 16; ++i) {
                last_good_view_[i] = view_matrix_[i];
                last_good_proj_[i] = projection_matrix_[i];
            }
            has_good_matrices_ = true;
            
            const ArPointCloud* point_cloud = ar_slam_->GetPointCloud();
            int32_t num_points = 0;
            if (point_cloud) {
                ArPointCloud_getNumberOfPoints(ar_slam_->GetSession(), point_cloud, &num_points);
                current_point_count_ = num_points;
                
                // Accumulate points into persistent map
                if (num_points > 0) {
                    const float* point_data = nullptr;
                    ArPointCloud_getData(ar_slam_->GetSession(), point_cloud, &point_data);
                    
                    if (point_data) {
                        float world_from_camera[16];
                        ar_slam_->GetWorldFromCameraMatrix(world_from_camera);
                        persistent_map_->AddPoints(world_from_camera, point_data, num_points);
                    }
                }
            }
            
            static int pc_log = 0;
            if (pc_log++ % 60 == 0) {
                __android_log_print(ANDROID_LOG_INFO, "SlamTorch", 
                    "Frame points=%d, Map total=%d (wrapped=%d)",
                    num_points, persistent_map_->GetPointCount(), persistent_map_->IsBufferWrapped());
            }
            
            // 3. Render ephemeral point cloud (current frame only)
            point_cloud_renderer_->Draw(
                ar_slam_->GetSession(),
                point_cloud,
                view_matrix_,
                projection_matrix_
            );
        } else {
            static int warn_log = 0;
            if (warn_log++ % 180 == 0) {
                __android_log_print(ANDROID_LOG_WARN, "SlamTorch", "Not tracking - move phone slowly over textured surfaces");
            }
        }
        
        // 4. ALWAYS render persistent map (even when not tracking, using last good matrices)
        if (persistent_map_ && persistent_map_->GetPointCount() > 0) {
            const float* view_to_use = has_good_matrices_ ? last_good_view_ : view_matrix_;
            const float* proj_to_use = has_good_matrices_ ? last_good_proj_ : projection_matrix_;
            persistent_map_->Draw(view_to_use, proj_to_use);
        }
    }

    // Presentation
    eglSwapBuffers(display_, surface_);
}

void Renderer::OnPause() {
    if (ar_slam_) ar_slam_->OnPause();
}

void Renderer::OnResume() {
    if (ar_slam_) ar_slam_->OnResume(env_);
}

void Renderer::UpdateRotation(int display_rotation) {
    display_rotation_ = display_rotation;
    if (ar_slam_ && width_ > 0 && height_ > 0) {
        ar_slam_->OnSurfaceChanged(display_rotation_, width_, height_);
        __android_log_print(ANDROID_LOG_INFO, "SlamTorch", "Rotation updated: %d", display_rotation);
    }
}

void Renderer::ClearPersistentMap() {
    if (persistent_map_) {
        persistent_map_->Clear();
        has_good_matrices_ = false;
    }
}

void Renderer::CycleTorchMode() {
    if (!ar_slam_) return;
    
    auto mode = ar_slam_->GetTorchMode();
    if (mode == ArCoreSlam::TorchMode::AUTO) {
        ar_slam_->SetTorchMode(ArCoreSlam::TorchMode::MANUAL_ON);
        __android_log_print(ANDROID_LOG_INFO, "SlamTorch", "Torch: MANUAL_ON");
    } else if (mode == ArCoreSlam::TorchMode::MANUAL_ON) {
        ar_slam_->SetTorchMode(ArCoreSlam::TorchMode::MANUAL_OFF);
        __android_log_print(ANDROID_LOG_INFO, "SlamTorch", "Torch: MANUAL_OFF");
    } else {
        ar_slam_->SetTorchMode(ArCoreSlam::TorchMode::AUTO);
        __android_log_print(ANDROID_LOG_INFO, "SlamTorch", "Torch: AUTO");
    }
}

void Renderer::SetTorchMode(ArCoreSlam::TorchMode mode) {
    if (!ar_slam_) return;
    ar_slam_->SetTorchMode(mode);
}

DebugStats Renderer::GetDebugStats() const {
    DebugStats stats;
    stats.tracking_state = "NONE";
    stats.torch_mode = "NONE";
    stats.torch_enabled = false;
    stats.depth_enabled = false;
    
    if (ar_slam_) {
        ArTrackingState state = ar_slam_->GetTrackingState();
        stats.tracking_state = (state == AR_TRACKING_STATE_TRACKING) ? "TRACKING" :
                               (state == AR_TRACKING_STATE_PAUSED) ? "PAUSED" : "STOPPED";
        
        auto mode = ar_slam_->GetTorchMode();
        stats.torch_mode = (mode == ArCoreSlam::TorchMode::AUTO) ? "AUTO" :
                           (mode == ArCoreSlam::TorchMode::MANUAL_ON) ? "ON" : "OFF";
        stats.torch_enabled = ar_slam_->IsTorchOn();
        
        stats.depth_enabled = ar_slam_->IsDepthEnabled();
    }
    
    stats.point_count = current_point_count_;
    stats.map_points = persistent_map_ ? persistent_map_->GetPointCount() : 0;
    stats.fps = last_fps_;
    
    return stats;
}

void Renderer::initRenderer() {
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display_, nullptr, nullptr);

    constexpr EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE
    };

    EGLint numConfigs;
    EGLConfig config;
    eglChooseConfig(display_, attribs, &config, 1, &numConfigs);

    surface_ = eglCreateWindowSurface(display_, config, app_->window, nullptr);
    EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context_ = eglCreateContext(display_, config, nullptr, contextAttribs);

    eglMakeCurrent(display_, surface_, surface_, context_);
}

void Renderer::updateRenderArea() {
    EGLint width, height;
    eglQuerySurface(display_, surface_, EGL_WIDTH, &width);
    eglQuerySurface(display_, surface_, EGL_HEIGHT, &height);

    if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        glViewport(0, 0, width, height);
        if (ar_slam_) ar_slam_->OnSurfaceChanged(display_rotation_, width, height);
    }
}

void Renderer::handleInput() {
    auto *inputBuffer = android_app_swap_input_buffers(app_);
    if (!inputBuffer) return;

    for (auto i = 0; i < inputBuffer->keyEventsCount; i++) {
        auto &keyEvent = inputBuffer->keyEvents[i];
        if (keyEvent.action == AKEY_EVENT_ACTION_DOWN) {
            // Volume Down (Keycode 25) - Clear persistent map
            if (keyEvent.keyCode == 25 && persistent_map_) {
                persistent_map_->Clear();
                has_good_matrices_ = false;
                __android_log_print(ANDROID_LOG_INFO, "SlamTorch", "User cleared persistent map");
            }
            // Cycle through Torch Modes on Volume Up (Keycode 24)
            else if (keyEvent.keyCode == 24 && ar_slam_) {
                auto mode = ar_slam_->GetTorchMode();
                if (mode == ArCoreSlam::TorchMode::AUTO) ar_slam_->SetTorchMode(ArCoreSlam::TorchMode::MANUAL_ON);
                else if (mode == ArCoreSlam::TorchMode::MANUAL_ON) ar_slam_->SetTorchMode(ArCoreSlam::TorchMode::MANUAL_OFF);
                else ar_slam_->SetTorchMode(ArCoreSlam::TorchMode::AUTO);
            }
        }
    }
    android_app_clear_motion_events(inputBuffer);
    android_app_clear_key_events(inputBuffer);
}
