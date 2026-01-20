#include "BackgroundRenderer.h"
#include "AndroidOut.h"
#include <array>

namespace {
    // Fullscreen quad vertices (position + texcoord)
    constexpr std::array<float, 20> kVertices = {
        // x, y, z, u, v
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,  // bottom-left
         1.0f, -1.0f, 0.0f, 1.0f, 0.0f,  // bottom-right
        -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,  // top-left
         1.0f,  1.0f, 0.0f, 1.0f, 1.0f   // top-right
    };

    constexpr char kVertexShader[] = R"(
        #version 300 es
        precision mediump float;
        
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec2 a_TexCoord;
        
        out vec2 v_TexCoord;
        
        void main() {
            gl_Position = vec4(a_Position, 1.0);
            v_TexCoord = a_TexCoord;
        }
    )";

    constexpr char kFragmentShader[] = R"(
        #version 300 es
        #extension GL_OES_EGL_image_external_essl3 : require
        precision mediump float;
        
        uniform samplerExternalOES u_Texture;
        
        in vec2 v_TexCoord;
        out vec4 fragColor;
        
        void main() {
            fragColor = texture(u_Texture, v_TexCoord);
        }
    )";
}

BackgroundRenderer::BackgroundRenderer() = default;

BackgroundRenderer::~BackgroundRenderer() {
    if (vertex_buffer_) glDeleteBuffers(1, &vertex_buffer_);
    if (texture_id_) glDeleteTextures(1, &texture_id_);
    if (shader_program_) glDeleteProgram(shader_program_);
}

void BackgroundRenderer::Initialize() {
    if (initialized_) return;
    
    __android_log_print(ANDROID_LOG_INFO, "SlamTorch", "BackgroundRenderer::Initialize() starting");
    
    // Compile shaders
    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    const GLchar* vert_src = kVertexShader;
    glShaderSource(vertex_shader, 1, &vert_src, nullptr);
    glCompileShader(vertex_shader);
    
    GLint compiled;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint log_length;
        glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 0) {
            char log[512];
            glGetShaderInfoLog(vertex_shader, 512, nullptr, log);
            aout << "Background vertex shader error: " << log << std::endl;
        }
        return;
    }
    
    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    const GLchar* frag_src = kFragmentShader;
    glShaderSource(fragment_shader, 1, &frag_src, nullptr);
    glCompileShader(fragment_shader);
    
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint log_length;
        glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 0) {
            char log[512];
            glGetShaderInfoLog(fragment_shader, 512, nullptr, log);
            aout << "Background fragment shader error: " << log << std::endl;
        }
        glDeleteShader(vertex_shader);
        return;
    }
    
    // Link program
    shader_program_ = glCreateProgram();
    glAttachShader(shader_program_, vertex_shader);
    glAttachShader(shader_program_, fragment_shader);
    glLinkProgram(shader_program_);
    
    GLint linked;
    glGetProgramiv(shader_program_, GL_LINK_STATUS, &linked);
    if (!linked) {
        aout << "Background shader link failed" << std::endl;
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return;
    }
    
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    
    // Get attribute/uniform locations
    position_attrib_ = 0;  // layout(location = 0)
    tex_coord_attrib_ = 1; // layout(location = 1)
    texture_uniform_ = glGetUniformLocation(shader_program_, "u_Texture");
    
    // Create vertex buffer (DYNAMIC - we update UVs every frame)
    glGenBuffers(1, &vertex_buffer_);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    // Create OES texture for ARCore
    glGenTextures(1, &texture_id_);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture_id_);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
    
    initialized_ = true;
    __android_log_print(ANDROID_LOG_INFO, "SlamTorch", "BackgroundRenderer initialized: shader=%u texture=%u", shader_program_, texture_id_);
}

void BackgroundRenderer::SetCameraTexture(const ArSession* session) {
    if (!initialized_ || !session) {
        __android_log_print(ANDROID_LOG_ERROR, "SlamTorch", "SetCameraTexture failed: init=%d session=%p", initialized_, session);
        return;
    }
    
    // Set camera texture ID for ARCore (cast away const - ARCore API design)
    // MUST be called BEFORE first ArSession_update()
    ArSession_setCameraTextureName(const_cast<ArSession*>(session), texture_id_);
    __android_log_print(ANDROID_LOG_INFO, "SlamTorch", "Camera texture ID set: %u", texture_id_);
}

void BackgroundRenderer::UpdateCameraTexture(const ArSession* session) {
    if (!initialized_ || !session) return;
    
    // ARCore writes to the texture after ArSession_update()
    // Must refresh binding each frame
    ArSession_setCameraTextureName(const_cast<ArSession*>(session), texture_id_);
}

void BackgroundRenderer::Draw(const ArSession* session, const ArFrame* frame) {
    if (!initialized_ || !session || !frame) {
        __android_log_print(ANDROID_LOG_ERROR, "SlamTorch", "Draw failed: init=%d session=%p frame=%p", initialized_, session, frame);
        return;
    }
    
    static int frame_count = 0;
    if (frame_count++ % 60 == 0) {
        __android_log_print(ANDROID_LOG_DEBUG, "SlamTorch", "Drawing background frame %d, texture_id=%u", frame_count, texture_id_);
    }
    
    // Transform UV coordinates from NDC to texture space
    // ARCore provides correct UVs for device rotation/aspect ratio
    ArFrame_transformCoordinates2d(
        session, frame,
        AR_COORDINATES_2D_OPENGL_NORMALIZED_DEVICE_COORDINATES,
        4,  // 4 vertices (quad corners)
        quad_ndc_,  // Transform fullscreen quad positions: (-1,-1), (1,-1), (-1,1), (1,1)
        AR_COORDINATES_2D_TEXTURE_NORMALIZED,
        quad_uvs_out_
    );
    
    // Update VBO with ARCore's transformed UVs (interleaved: xyzuv)
    // Use preallocated member to avoid per-frame allocation
    verts_[0] = -1.0f; verts_[1] = -1.0f; verts_[2] = 0.0f; verts_[3] = quad_uvs_out_[0]; verts_[4] = quad_uvs_out_[1];
    verts_[5] =  1.0f; verts_[6] = -1.0f; verts_[7] = 0.0f; verts_[8] = quad_uvs_out_[2]; verts_[9] = quad_uvs_out_[3];
    verts_[10] = -1.0f; verts_[11] = 1.0f; verts_[12] = 0.0f; verts_[13] = quad_uvs_out_[4]; verts_[14] = quad_uvs_out_[5];
    verts_[15] =  1.0f; verts_[16] = 1.0f; verts_[17] = 0.0f; verts_[18] = quad_uvs_out_[6]; verts_[19] = quad_uvs_out_[7];
    
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts_), verts_);
    
    // Disable depth test/write for background
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    
    // Bind shader
    glUseProgram(shader_program_);
    
    // Bind texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture_id_);
    glUniform1i(texture_uniform_, 0);
    
    // Setup vertex attributes (VBO already bound from BufferSubData above)
    glEnableVertexAttribArray(position_attrib_);
    glVertexAttribPointer(position_attrib_, 3, GL_FLOAT, GL_FALSE, 20, (void*)0);
    glEnableVertexAttribArray(tex_coord_attrib_);
    glVertexAttribPointer(tex_coord_attrib_, 2, GL_FLOAT, GL_FALSE, 20, (void*)12);
    
    // Draw fullscreen quad (triangle strip)
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    // Cleanup
    glDisableVertexAttribArray(position_attrib_);
    glDisableVertexAttribArray(tex_coord_attrib_);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
    
    // Re-enable depth for subsequent draws
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}
