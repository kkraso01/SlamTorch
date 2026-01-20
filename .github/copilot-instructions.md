# Copilot Instructions for SlamTorch

Guidelines and patterns for working on the SlamTorch AR project.

## Architecture & Data Flow
SlamTorch is an Android AR application that uses ARCore's C API and the Android Game SDK (`GameActivity`).

- **Entry Point**: [app/src/main/java/com/example/slamtorch/MainActivity.kt](app/src/main/java/com/example/slamtorch/MainActivity.kt) loads the `slamtorch` native library. On the C++ side, [app/src/main/cpp/main.cpp](app/src/main/cpp/main.cpp) contains the `android_main` loop.
- **Core Loop**: The native `android_main` handles events via `ALooper_pollOnce` and calls `Renderer::render()` every frame.
- **AR Logic**: Managed in [app/src/main/cpp/ArCoreSlam.cpp](app/src/main/cpp/ArCoreSlam.cpp). It handles the `ArSession` and `ArFrame`.
- **Torch Control**: 
    1. C++ identifies light intensity thresholds in `ArCoreSlam::UpdateTorchLogic`.
    2. It triggers a JNI callback to `MainActivity.setTorchEnabled(boolean)`.
    3. `MainActivity` uses [app/src/main/java/com/example/slamtorch/TorchController.kt](app/src/main/java/com/example/slamtorch/TorchController.kt) to toggle the device flashlight via `CameraManager`.

## Development Workflows
- **Build**: Use `./gradlew assembleDebug` or the Android Studio "Run" button.
- **Native Build**: CMake handles the C++ compilation. Configuration is in [app/src/main/cpp/CMakeLists.txt](app/src/main/cpp/CMakeLists.txt).
- **Logging**: Use `aout` (from `AndroidOut.h`) in C++ for `logcat` output. In Kotlin, use standard `android.util.Log`.

## Key Patterns
- **JNI Callbacks**: When adding new functionality that requires Android hardware (like sensors or flash), implement the logic in C++ and use `env->CallVoidMethod` to trigger a Kotlin implementation in `MainActivity`.
- **Resource Management**: Always clean up ARCore objects in destructors or designated cleanup methods (e.g., `ArSession_destroy`, `ArFrame_destroy`).
- **Memory**: Prefer stack allocation for temporary ARCore data (like `ArPose`) and use `jobject` global references for cross-thread persistence.

## Important Thresholds
- **Light Estimation**: `ArLightEstimate_getPixelIntensity` returns 0.0 (dark) to 1.0 (bright).
- **Auto-Torch Logic**: 
    - Turns **ON** if intensity < `0.2f`.
    - Turns **OFF** if intensity > `0.4f`.
    - Logic located in `ArCoreSlam::UpdateTorchLogic`.

## Dependencies
- **ARCore NDK**: Access via `arcore_sdk_c`.
- **GameActivity**: Handles native window and input sync.
- **OpenGL ES 3.0**: Used for all rendering in `Renderer.cpp`.
