#include <jni.h>
#include <android/log.h>
#include "Renderer.h"

// Global pointer to renderer (set by main loop)
extern Renderer* g_renderer;

extern "C" {

JNIEXPORT void JNICALL
Java_com_example_slamtorch_MainActivity_nativeUpdateRotation(JNIEnv* env, jobject /* this */, jint rotation) {
    if (g_renderer) {
        g_renderer->UpdateRotation(rotation);
    }
}

JNIEXPORT void JNICALL
Java_com_example_slamtorch_MainActivity_nativeClearMap(JNIEnv* env, jobject /* this */) {
    if (g_renderer) {
        g_renderer->ClearPersistentMap();
    }
}

JNIEXPORT void JNICALL
Java_com_example_slamtorch_MainActivity_nativeCycleTorch(JNIEnv* env, jobject /* this */) {
    if (g_renderer) {
        g_renderer->CycleTorchMode();
    }
}

JNIEXPORT void JNICALL
Java_com_example_slamtorch_MainActivity_nativeSetTorchMode(JNIEnv* env, jobject /* this */, jint mode) {
    if (!g_renderer) return;
    auto torch_mode = ArCoreSlam::TorchMode::AUTO;
    if (mode == 1) {
        torch_mode = ArCoreSlam::TorchMode::MANUAL_ON;
    } else if (mode == 2) {
        torch_mode = ArCoreSlam::TorchMode::MANUAL_OFF;
    }
    g_renderer->SetTorchMode(torch_mode);
}

JNIEXPORT jobject JNICALL
Java_com_example_slamtorch_MainActivity_nativeGetDebugStats(JNIEnv* env, jobject /* this */) {
    if (!g_renderer) return nullptr;
    
    auto stats = g_renderer->GetDebugStats();
    
    // Find MainActivity$DebugStats class
    static jclass statsClass = nullptr;
    static jmethodID constructor = nullptr;
    if (!statsClass) {
        jclass localClass = env->FindClass("com/example/slamtorch/MainActivity$DebugStats");
        if (!localClass) {
            __android_log_print(ANDROID_LOG_ERROR, "SlamTorch", "Failed to find DebugStats class");
            return nullptr;
        }
        statsClass = reinterpret_cast<jclass>(env->NewGlobalRef(localClass));
        env->DeleteLocalRef(localClass);
    }
    if (!constructor) {
        constructor = env->GetMethodID(statsClass, "<init>",
            "(Ljava/lang/String;IIIIIIFFFLjava/lang/String;ZZLjava/lang/String;)V");
        if (!constructor) {
            __android_log_print(ANDROID_LOG_ERROR, "SlamTorch", "Failed to find DebugStats constructor");
            return nullptr;
        }
    }
    
    // Create Java strings
    jstring trackingState = env->NewStringUTF(stats.tracking_state);
    jstring torchMode = env->NewStringUTF(stats.torch_mode);
    jstring failureReason = env->NewStringUTF(stats.last_failure_reason);
    
    // Create DebugStats object
    jobject result = env->NewObject(statsClass, constructor,
        trackingState, stats.point_count, stats.map_points, stats.bearing_landmarks,
        stats.metric_landmarks, stats.tracked_features, stats.stable_tracks,
        stats.avg_track_age, stats.depth_hit_rate, stats.fps,
        torchMode, stats.torch_enabled, stats.depth_enabled, failureReason);
    
    env->DeleteLocalRef(trackingState);
    env->DeleteLocalRef(torchMode);
    env->DeleteLocalRef(failureReason);
    return result;
}

} // extern "C"
