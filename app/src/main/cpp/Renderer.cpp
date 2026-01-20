#include "Renderer.h"
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <GLES3/gl3.h>
#include <assert.h>
#include <cmath>
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
    
    landmark_map_ = std::make_unique<LandmarkMap>(20000);
    optical_flow_ = std::make_unique<OpticalFlowTracker>(800, 3);
    debug_hud_ = std::make_unique<DebugHud>();
    
    // Initialize last-known matrices to identity
    for (int i = 0; i < 16; ++i) {
        last_good_view_[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        last_good_proj_[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        last_good_world_from_camera_[i] = (i % 5 == 0) ? 1.0f : 0.0f;
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
    delete[] camera_image_buffer_;
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
            float world_from_camera[16];
            ar_slam_->GetWorldFromCameraMatrix(world_from_camera);
            
            // Save good matrices for frozen rendering when tracking is lost
            for (int i = 0; i < 16; ++i) {
                last_good_view_[i] = view_matrix_[i];
                last_good_proj_[i] = projection_matrix_[i];
                last_good_world_from_camera_[i] = world_from_camera[i];
            }
            has_good_matrices_ = true;
            
            const ArPointCloud* point_cloud = ar_slam_->GetPointCloud();
            int32_t num_points = 0;
            if (point_cloud) {
                ArPointCloud_getNumberOfPoints(ar_slam_->GetSession(), point_cloud, &num_points);
                current_point_count_ = num_points;
            }
            
            static int pc_log = 0;
            if (pc_log++ % 60 == 0) {
                __android_log_print(ANDROID_LOG_INFO, "SlamTorch", 
                    "Frame points=%d, Landmark map=%d",
                    num_points, landmark_map_ ? landmark_map_->GetPointCount() : 0);
            }
            
            // 3. Render ephemeral point cloud (current frame only)
            point_cloud_renderer_->Draw(
                ar_slam_->GetSession(),
                point_cloud,
                view_matrix_,
                projection_matrix_
            );

            // 4. CPU image acquisition and optical flow tracking
            int image_width = 0;
            int image_height = 0;
            ar_slam_->GetImageDimensions(&image_width, &image_height);
            if (landmark_map_) {
                landmark_map_->BeginFrame();
            }

            if (image_width > 0 && image_height > 0 && optical_flow_) {
                const int required_capacity = image_width * image_height;
                if (camera_image_capacity_ < required_capacity) {
                    delete[] camera_image_buffer_;
                    camera_image_buffer_ = new uint8_t[required_capacity];
                    camera_image_capacity_ = required_capacity;
                }
                camera_image_stride_ = image_width;

                if (camera_image_buffer_) {
                    const bool got_image = ar_slam_->AcquireCameraImageY(
                        camera_image_buffer_,
                        camera_image_stride_,
                        camera_image_capacity_,
                        &image_width,
                        &image_height);

                    if (got_image) {
                        optical_flow_->Update(camera_image_buffer_, image_width, image_height);
                        current_feature_count_ = optical_flow_->GetTrackCount();

                        if (landmark_map_) {
                            float fx = 0.0f, fy = 0.0f, cx = 0.0f, cy = 0.0f;
                            ar_slam_->GetCameraIntrinsics(&fx, &fy, &cx, &cy);

                            ArCoreSlam::DepthImageInfo depth_info;
                            ArImage* depth_image = nullptr;
                            const bool depth_ok = ar_slam_->AcquireDepthImage16(&depth_info, &depth_image);

                            const OpticalFlowTracker::Track* tracks = optical_flow_->GetTracks();
                            const int track_count = optical_flow_->GetTrackCount();
                            int stable_tracks = 0;
                            float total_track_age = 0.0f;
                            int depth_attempts = 0;
                            int depth_hits = 0;

                            for (int i = 0; i < track_count; ++i) {
                                const auto& track = tracks[i];
                                if (!track.active) continue;
                                total_track_age += static_cast<float>(track.age);

                                if (track.stable_count < 20) continue;
                                if (track.error > 5.0f) continue;

                                stable_tracks++;

                                const float bearing_x = (track.x - cx) / fx;
                                const float bearing_y = (track.y - cy) / fy;
                                float bearing_z = -1.0f;
                                const float bearing_len = std::sqrt(
                                    bearing_x * bearing_x +
                                    bearing_y * bearing_y +
                                    bearing_z * bearing_z);
                                float bearing[3] = {
                                    bearing_x / bearing_len,
                                    bearing_y / bearing_len,
                                    bearing_z / bearing_len
                                };

                                if (!depth_ok || !depth_info.data) {
                                    const float confidence = 0.4f + 0.4f * (track.stable_count / 30.0f);
                                    landmark_map_->AddBearingObservation(bearing, confidence);
                                    continue;
                                }

                                const float depth_scale_x = static_cast<float>(depth_info.width) /
                                                            static_cast<float>(image_width);
                                const float depth_scale_y = static_cast<float>(depth_info.height) /
                                                            static_cast<float>(image_height);
                                const int px = static_cast<int>(track.x * depth_scale_x);
                                const int py = static_cast<int>(track.y * depth_scale_y);
                                if (px < 0 || py < 0 || px >= depth_info.width || py >= depth_info.height) {
                                    continue;
                                }

                                const uint8_t* row = reinterpret_cast<const uint8_t*>(depth_info.data) +
                                                     depth_info.row_stride * py;
                                const uint16_t* depth_pixel = reinterpret_cast<const uint16_t*>(row + depth_info.pixel_stride * px);
                                const uint16_t depth_mm = *depth_pixel;
                                depth_attempts++;
                                if (depth_mm == 0) continue;
                                depth_hits++;

                                const float depth_m = static_cast<float>(depth_mm) * 0.001f;
                                const float x_cam = (track.x - cx) * depth_m / fx;
                                const float y_cam = (track.y - cy) * depth_m / fy;
                                const float z_cam = -depth_m;

                                float world_pos[3];
                                world_pos[0] = world_from_camera[0] * x_cam +
                                               world_from_camera[4] * y_cam +
                                               world_from_camera[8] * z_cam +
                                               world_from_camera[12];
                                world_pos[1] = world_from_camera[1] * x_cam +
                                               world_from_camera[5] * y_cam +
                                               world_from_camera[9] * z_cam +
                                               world_from_camera[13];
                                world_pos[2] = world_from_camera[2] * x_cam +
                                               world_from_camera[6] * y_cam +
                                               world_from_camera[10] * z_cam +
                                               world_from_camera[14];

                                const float confidence = 0.5f + 0.5f * (track.stable_count / 30.0f);
                                landmark_map_->AddMetricObservation(world_pos, bearing, confidence);
                            }

                            current_stable_track_count_ = stable_tracks;
                            current_avg_track_age_ = track_count > 0
                                ? (total_track_age / static_cast<float>(track_count))
                                : 0.0f;
                            current_depth_hit_rate_ = depth_attempts > 0
                                ? (100.0f * static_cast<float>(depth_hits) / static_cast<float>(depth_attempts))
                                : 0.0f;
                            current_bearing_landmarks_ = landmark_map_->GetBearingCount();
                            current_metric_landmarks_ = landmark_map_->GetMetricCount();

                            if (depth_image) {
                                ar_slam_->ReleaseDepthImage(depth_image);
                            }
                        }
                    }
                }
            }
        } else {
            static int warn_log = 0;
            if (warn_log++ % 180 == 0) {
                __android_log_print(ANDROID_LOG_WARN, "SlamTorch", "Not tracking - move phone slowly over textured surfaces");
            }
        }
        
        // 4. ALWAYS render persistent map (even when not tracking, using last good matrices)
        if (landmark_map_ && landmark_map_->GetPointCount() > 0) {
            const float* view_to_use = has_good_matrices_ ? last_good_view_ : view_matrix_;
            const float* proj_to_use = has_good_matrices_ ? last_good_proj_ : projection_matrix_;
            landmark_map_->Draw(view_to_use, proj_to_use, last_good_world_from_camera_);
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
    if (landmark_map_) {
        landmark_map_->Clear();
    }
    if (optical_flow_) {
        optical_flow_->Reset();
    }
    has_good_matrices_ = false;
    current_bearing_landmarks_ = 0;
    current_metric_landmarks_ = 0;
    current_feature_count_ = 0;
    current_stable_track_count_ = 0;
    current_avg_track_age_ = 0.0f;
    current_depth_hit_rate_ = 0.0f;
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
    if (debug_hud_) {
        debug_hud_->Update(ar_slam_.get(), current_point_count_,
                           landmark_map_ ? landmark_map_->GetPointCount() : 0,
                           current_bearing_landmarks_,
                           current_metric_landmarks_,
                           current_feature_count_,
                           current_stable_track_count_,
                           current_avg_track_age_,
                           current_depth_hit_rate_,
                           last_fps_);
        const DebugHudData& data = debug_hud_->GetData();
        stats.tracking_state = data.tracking_state;
        stats.torch_mode = data.torch_mode;
        stats.torch_enabled = data.torch_enabled;
        stats.depth_enabled = data.depth_enabled;
        stats.last_failure_reason = data.last_failure_reason;
        stats.point_count = data.point_count;
        stats.map_points = data.map_points;
        stats.bearing_landmarks = data.bearing_landmarks;
        stats.metric_landmarks = data.metric_landmarks;
        stats.tracked_features = data.tracked_features;
        stats.stable_tracks = data.stable_tracks;
        stats.avg_track_age = data.avg_track_age;
        stats.depth_hit_rate = data.depth_hit_rate;
        stats.fps = data.fps;
    } else {
        stats.tracking_state = "NONE";
        stats.torch_mode = "NONE";
        stats.torch_enabled = false;
        stats.depth_enabled = false;
        stats.last_failure_reason = "NONE";
        stats.point_count = 0;
        stats.map_points = 0;
        stats.bearing_landmarks = 0;
        stats.metric_landmarks = 0;
        stats.tracked_features = 0;
        stats.stable_tracks = 0;
        stats.avg_track_age = 0.0f;
        stats.depth_hit_rate = 0.0f;
        stats.fps = 0.0f;
    }
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
            if (keyEvent.keyCode == 25 && landmark_map_) {
                landmark_map_->Clear();
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
