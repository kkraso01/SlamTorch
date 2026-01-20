# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

cmake_minimum_required(VERSION 3.22.1)

project("slamtorch")

# Searches for packages provided by dependencies
find_package(game-activity REQUIRED CONFIG)

# Create an imported library for arcore
add_library(arcore_imported SHARED IMPORTED)
set_target_properties(arcore_imported PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/libs/${ANDROID_ABI}/libarcore_sdk_c.so
)

# Creates your game shared library.
add_library(slamtorch SHARED
        main.cpp
        AndroidOut.cpp
        Renderer.cpp
        Shader.cpp
        TextureAsset.cpp
        Utility.cpp
        ArCoreSlam.cpp
        BackgroundRenderer.cpp
        PointCloudRenderer.cpp
        PersistentPointMap.cpp
        OpticalFlowTracker.cpp
        LandmarkMap.cpp
        KeyframeLite.cpp
        DebugHud.cpp
        JniBridge.cpp)

target_include_directories(slamtorch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Forces the linker to keep the JNI entry point for GameActivity
# Also add 16 KB page size alignment for Google Play compliance
set(CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS} -u Java_com_google_androidgamesdk_GameActivity_initializeNativeCode -Wl,-z,max-page-size=16384")

# Configure libraries CMake uses to link your target library.
target_link_libraries(slamtorch
        # The game activity
        game-activity::game-activity_static
        
        # ARCore NDK
        arcore_imported

        # EGL and other dependent libraries required for drawing
        # and interacting with Android system
        EGL
        GLESv3
        jnigraphics
        android
        log)
