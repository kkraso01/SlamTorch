#ifndef SLAMTORCH_DEPTH_OVERLAY_RENDERER_H
#define SLAMTORCH_DEPTH_OVERLAY_RENDERER_H

#include <GLES3/gl3.h>

class DepthOverlayRenderer {
public:
    void Initialize();
    void UpdateTexture(const uint8_t* data, int width, int height);
    void Draw();
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

private:
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint texture_ = 0;
    GLint texture_uniform_ = -1;
    int tex_width_ = 0;
    int tex_height_ = 0;
    bool enabled_ = true;
};

#endif // SLAMTORCH_DEPTH_OVERLAY_RENDERER_H
