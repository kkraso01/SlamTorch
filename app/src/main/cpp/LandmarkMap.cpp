#include "LandmarkMap.h"
#include "AndroidOut.h"
#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
const char* kVertexShader = R"(
    #version 300 es
    precision highp float;
    layout(location = 0) in vec3 a_Position;
    layout(location = 1) in vec4 a_Color;
    uniform mat4 u_MVP;
    out vec4 v_Color;
    void main() {
        gl_Position = u_MVP * vec4(a_Position, 1.0);
        gl_PointSize = 6.0;
        v_Color = a_Color;
    }
)";

const char* kFragmentShader = R"(
    #version 300 es
    precision mediump float;
    in vec4 v_Color;
    out vec4 FragColor;
    void main() {
        vec2 coord = gl_PointCoord - vec2(0.5);
        if (length(coord) > 0.5) discard;
        FragColor = v_Color;
    }
)";

constexpr float kDedupeDistance = 0.05f;
constexpr int kMaxAge = 300;
constexpr float kMinConfidence = 0.05f;
}

LandmarkMap::LandmarkMap(int max_points)
    : max_points_(max_points) {
    landmarks_ = new Landmark[max_points_];
    vertex_buffer_ = new Vertex[max_points_];
    memset(landmarks_, 0, sizeof(Landmark) * max_points_);
    memset(vertex_buffer_, 0, sizeof(Vertex) * max_points_);
    InitGL();
    __android_log_print(ANDROID_LOG_INFO, "SlamTorch", "LandmarkMap initialized: max=%d", max_points_);
}

LandmarkMap::~LandmarkMap() {
    CleanupGL();
    delete[] landmarks_;
    delete[] vertex_buffer_;
}

void LandmarkMap::BeginFrame() {
    frame_index_++;
    for (int i = 0; i < point_count_; ++i) {
        Landmark& lm = landmarks_[i];
        if (frame_index_ - lm.last_seen > 30) {
            lm.confidence *= 0.99f;
            if (lm.confidence < kMinConfidence) {
                lm.confidence = 0.0f;
            }
        }
        lm.age = std::min(lm.age + 1, kMaxAge);
    }
}

void LandmarkMap::AddObservation(const float* world_pos, float confidence) {
    if (!world_pos) return;
    if (confidence <= 0.0f) return;

    const float dx_thresh = kDedupeDistance * kDedupeDistance;
    int best_index = -1;
    float best_dist = dx_thresh;

    for (int i = 0; i < point_count_; ++i) {
        Landmark& lm = landmarks_[i];
        if (lm.confidence <= 0.0f) continue;
        const float dx = lm.x - world_pos[0];
        const float dy = lm.y - world_pos[1];
        const float dz = lm.z - world_pos[2];
        const float dist = dx * dx + dy * dy + dz * dz;
        if (dist < best_dist) {
            best_dist = dist;
            best_index = i;
        }
    }

    if (best_index >= 0) {
        Landmark& lm = landmarks_[best_index];
        const float blend = 0.2f;
        lm.x = lm.x * (1.0f - blend) + world_pos[0] * blend;
        lm.y = lm.y * (1.0f - blend) + world_pos[1] * blend;
        lm.z = lm.z * (1.0f - blend) + world_pos[2] * blend;
        lm.confidence = std::min(1.0f, lm.confidence + confidence * 0.2f);
        lm.last_seen = frame_index_;
        return;
    }

    int idx = write_index_;
    landmarks_[idx].x = world_pos[0];
    landmarks_[idx].y = world_pos[1];
    landmarks_[idx].z = world_pos[2];
    landmarks_[idx].confidence = std::min(1.0f, confidence);
    landmarks_[idx].age = 0;
    landmarks_[idx].last_seen = frame_index_;

    write_index_ = (write_index_ + 1) % max_points_;
    if (point_count_ < max_points_) {
        point_count_++;
    }
}

void LandmarkMap::InitGL() {
    GLuint vert_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert_shader, 1, &kVertexShader, nullptr);
    glCompileShader(vert_shader);

    GLuint frag_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag_shader, 1, &kFragmentShader, nullptr);
    glCompileShader(frag_shader);

    program_ = glCreateProgram();
    glAttachShader(program_, vert_shader);
    glAttachShader(program_, frag_shader);
    glLinkProgram(program_);

    glDeleteShader(vert_shader);
    glDeleteShader(frag_shader);

    mvp_uniform_ = glGetUniformLocation(program_, "u_MVP");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, max_points_ * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(sizeof(float) * 3));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void LandmarkMap::CleanupGL() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
}

void LandmarkMap::BuildColor(float confidence, int age, float* out_rgba) const {
    const float age_norm = std::min(1.0f, static_cast<float>(age) / static_cast<float>(kMaxAge));
    const float conf = std::min(1.0f, confidence);
    const float r = 0.2f + 0.8f * conf;
    const float g = 0.4f + 0.6f * age_norm;
    const float b = 1.0f - 0.5f * age_norm;
    out_rgba[0] = r;
    out_rgba[1] = g;
    out_rgba[2] = b;
    out_rgba[3] = 0.7f + 0.3f * conf;
}

void LandmarkMap::UpdateGLBuffer() {
    if (point_count_ == 0) return;
    for (int i = 0; i < point_count_; ++i) {
        Landmark& lm = landmarks_[i];
        Vertex& v = vertex_buffer_[i];
        v.x = lm.x;
        v.y = lm.y;
        v.z = lm.z;
        float color[4];
        BuildColor(lm.confidence, lm.age, color);
        v.r = color[0];
        v.g = color[1];
        v.b = color[2];
        v.a = color[3];
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, point_count_ * sizeof(Vertex), vertex_buffer_);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void LandmarkMap::Draw(const float* view_matrix, const float* projection_matrix) {
    if (point_count_ == 0) return;

    float mvp[16];
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += projection_matrix[k * 4 + row] * view_matrix[col * 4 + k];
            }
            mvp[col * 4 + row] = sum;
        }
    }

    UpdateGLBuffer();
    glUseProgram(program_);
    glUniformMatrix4fv(mvp_uniform_, 1, GL_FALSE, mvp);
    glBindVertexArray(vao_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);
    glDrawArrays(GL_POINTS, 0, point_count_);
    glBindVertexArray(0);
}

void LandmarkMap::Clear() {
    point_count_ = 0;
    write_index_ = 0;
    frame_index_ = 0;
    memset(landmarks_, 0, sizeof(Landmark) * max_points_);
    memset(vertex_buffer_, 0, sizeof(Vertex) * max_points_);
    __android_log_print(ANDROID_LOG_INFO, "SlamTorch", "LandmarkMap cleared");
}
