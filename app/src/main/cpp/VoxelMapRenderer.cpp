#include "VoxelMapRenderer.h"
#include <android/log.h>
#include <cstring>

namespace {
const char* kVertexShader = R"(
    #version 300 es
    precision highp float;
    uniform mat4 u_MVP;
    uniform float u_PointSize;
    layout(location = 0) in vec3 a_Position;
    void main() {
        gl_Position = u_MVP * vec4(a_Position, 1.0);
        gl_PointSize = u_PointSize;
    }
)";

const char* kFragmentShader = R"(
    #version 300 es
    precision mediump float;
    out vec4 FragColor;
    void main() {
        vec2 coord = gl_PointCoord - vec2(0.5);
        if (length(coord) > 0.5) discard;
        FragColor = vec4(0.2, 0.8, 1.0, 0.9);
    }
)";

void Multiply4x4(const float* a, const float* b, float* out) {
    for (int i = 0; i < 16; ++i) out[i] = 0.0f;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
}
}

void VoxelMapRenderer::Initialize() {
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

    mvp_uniform_ = glGetUniformLocation(program_, "u_MVP");
    point_size_uniform_ = glGetUniformLocation(program_, "u_PointSize");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void VoxelMapRenderer::UpdatePoints(const float* points, int point_count) {
    if (!points || point_count <= 0) {
        point_count_ = 0;
        return;
    }
    point_count_ = point_count;
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, point_count_ * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, point_count_ * 3 * sizeof(float), points);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VoxelMapRenderer::Draw(const float* view, const float* proj) {
    if (!view || !proj || point_count_ <= 0) return;

    float mvp[16];
    Multiply4x4(proj, view, mvp);

    glUseProgram(program_);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glUniformMatrix4fv(mvp_uniform_, 1, GL_FALSE, mvp);
    glUniform1f(point_size_uniform_, 4.0f);
    glBindVertexArray(vao_);
    glDrawArrays(GL_POINTS, 0, point_count_);
    glBindVertexArray(0);
    glUseProgram(0);
}
