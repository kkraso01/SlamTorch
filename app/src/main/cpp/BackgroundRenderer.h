#ifndef SLAMTORCH_BACKGROUND_RENDERER_H
#define SLAMTORCH_BACKGROUND_RENDERER_H

#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include "arcore/arcore_c_api.h"

// Renders ARCore camera background as fullscreen quad
// Uses OES external texture (ARCore camera feed)
class BackgroundRenderer {
public:
    BackgroundRenderer();
    ~BackgroundRenderer();

    // Initialize shaders and geometry (call once)
    void Initialize();
    
    // Set the camera texture on ARCore session (call once after Initialize, before first update)
    void SetCameraTexture(const ArSession* session);
    
    // Update camera texture binding (call every frame after ArSession_update)
    void UpdateCameraTexture(const ArSession* session);

    // Draw the background texture (call every frame)
    // Must be called with valid ArSession and ArFrame
    void Draw(const ArSession* session, const ArFrame* frame);

private:
    GLuint shader_program_ = 0;
    GLuint texture_id_ = 0;
    
    // Vertex attributes
    GLint position_attrib_ = -1;
    GLint tex_coord_attrib_ = -1;
    
    // Uniforms
    GLint texture_uniform_ = -1;
    
    // Geometry
    GLuint vertex_buffer_ = 0;
    
    // Preallocated buffers (no per-frame alloc)
    float quad_ndc_[8] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};  // Fullscreen quad in NDC
    float quad_uvs_out_[8];  // ARCore-transformed UVs
    float verts_[20];  // Interleaved vertex data (xyzuv * 4 vertices)
    
    bool initialized_ = false;
};

#endif // SLAMTORCH_BACKGROUND_RENDERER_H
