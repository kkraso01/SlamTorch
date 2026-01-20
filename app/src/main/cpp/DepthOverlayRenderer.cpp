#include "DepthOverlayRenderer.h"
#include <android/log.h>

namespace {
const char* kVertexShader = R"(
    #version 300 es
    layout(location = 0) in vec2 a_Position;
    layout(location = 1) in vec2 a_TexCoord;
    out vec2 v_TexCoord;
    void main() {
        gl_Position = vec4(a_Position, 0.0, 1.0);
        v_TexCoord = a_TexCoord;
    }
)";

const char* kFragmentShader = R"(
    #version 300 es
    precision mediump float;
    in vec2 v_TexCoord;
    uniform sampler2D u_Texture;
    out vec4 FragColor;
    void main() {
        float d = texture(u_Texture, v_TexCoord).r;
        FragColor = vec4(vec3(d), 0.6);
    }
)";
}

void DepthOverlayRenderer::Initialize() {
    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &kVertexShader, nullptr);
    glCompileShader(vert);

    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &kFragmentShader, nullptr);
    glCompileShader(frag);

    program_ = glCreateProgram();
    glAttachShader(program_, vert);
    glAttachShader(program_, frag);
    glLinkProgram(program_);

    glDeleteShader(vert);
    glDeleteShader(frag);

    texture_uniform_ = glGetUniformLocation(program_, "u_Texture");

    const float quad[] = {
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
    };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void DepthOverlayRenderer::UpdateTexture(const uint8_t* data, int width, int height) {
    if (!data || width <= 0 || height <= 0) return;
    if (!texture_) return;

    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (width != tex_width_ || height != tex_height_) {
        tex_width_ = width;
        tex_height_ = height;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, tex_width_, tex_height_, 0, GL_RED, GL_UNSIGNED_BYTE, data);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex_width_, tex_height_, GL_RED, GL_UNSIGNED_BYTE, data);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}

void DepthOverlayRenderer::Draw() {
    if (!enabled_ || !texture_ || tex_width_ == 0 || tex_height_ == 0) {
        return;
    }

    glUseProgram(program_);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(texture_uniform_, 0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    glUseProgram(0);
}
