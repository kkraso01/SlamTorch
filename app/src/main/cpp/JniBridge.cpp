#include <jni.h>
#include <android/log.h>
#include "Renderer.h"

// Global pointer to renderer (set by main loop)
extern Renderer* g_renderer;

extern "C" {

JNIEXPORT void JNICALL
Java_com_example_slamtorch_MainActivity_nativeUpdateRotation(JNIEnv* env, jobject /* this */, jint rotation) {
    if (g_renderer) {
        // Convert Android rotation (0,1,2,3) to degrees (0,90,180,270)
        int rotation_degrees = rotation * 90;
        g_renderer->UpdateRotation(rotation_degrees);
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

JNIEXPORT jobject JNICALL
Java_com_example_slamtorch_MainActivity_nativeGetDebugStats(JNIEnv* env, jobject /* this */) {
    if (!g_renderer) return nullptr;
    
    auto stats = g_renderer->GetDebugStats();
    
    // Find MainActivity$DebugStats class
    jclass statsClass = env->FindClass("com/example/slamtorch/MainActivity$DebugStats");
    if (!statsClass) {
        __android_log_print(ANDROID_LOG_ERROR, "SlamTorch", "Failed to find DebugStats class");
        return nullptr;
    }
    
    // Get constructor
    jmethodID constructor = env->GetMethodID(statsClass, "<init>", 
        "(Ljava/lang/String;IIFLjava/lang/String;Z)V");
    if (!constructor) {
        __android_log_print(ANDROID_LOG_ERROR, "SlamTorch", "Failed to find DebugStats constructor");
        return nullptr;
    }
    
    // Create Java strings
    jstring trackingState = env->NewStringUTF(stats.tracking_state.c_str());
    jstring torchMode = env->NewStringUTF(stats.torch_mode.c_str());
    
    // Create DebugStats object
    jobject result = env->NewObject(statsClass, constructor,
        trackingState, stats.point_count, stats.map_points, stats.fps,
        torchMode, stats.depth_enabled);
    
    env->DeleteLocalRef(trackingState);
    env->DeleteLocalRef(torchMode);
    env->DeleteLocalRef(statsClass);
    
    return result;
}

} // extern "C"
