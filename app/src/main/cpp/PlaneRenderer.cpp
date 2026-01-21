#include "PlaneRenderer.h"
#include "AndroidOut.h"
#include <cmath>
#include <algorithm>

namespace {
constexpr char kPlaneVertexShader[] = R"(
    #version 300 es
    precision highp float;

    layout(location = 0) in vec3 a_Position;
    uniform mat4 u_MVP;

    void main() {
        gl_Position = u_MVP * vec4(a_Position, 1.0);
    }
)";

constexpr char kPlaneFragmentShader[] = R"(
    #version 300 es
    precision mediump float;

    uniform vec4 u_Color;
    out vec4 fragColor;

    void main() {
        fragColor = u_Color;
    }
)";

bool IsPointInTriangle(const float* polygon, int ia, int ib, int ic, int ip, bool ccw) {
    const float ax = polygon[ia * 2 + 0];
    const float az = polygon[ia * 2 + 1];
    const float bx = polygon[ib * 2 + 0];
    const float bz = polygon[ib * 2 + 1];
    const float cx = polygon[ic * 2 + 0];
    const float cz = polygon[ic * 2 + 1];
    const float px = polygon[ip * 2 + 0];
    const float pz = polygon[ip * 2 + 1];

    const float abx = bx - ax;
    const float abz = bz - az;
    const float bcx = cx - bx;
    const float bcz = cz - bz;
    const float cax = ax - cx;
    const float caz = az - cz;

    const float apx = px - ax;
    const float apz = pz - az;
    const float bpx = px - bx;
    const float bpz = pz - bz;
    const float cpx = px - cx;
    const float cpz = pz - cz;

    const float cross1 = abx * apz - abz * apx;
    const float cross2 = bcx * bpz - bcz * bpx;
    const float cross3 = cax * cpz - caz * cpx;

    if (ccw) {
        return cross1 >= 0.0f && cross2 >= 0.0f && cross3 >= 0.0f;
    }
    return cross1 <= 0.0f && cross2 <= 0.0f && cross3 <= 0.0f;
}
}

PlaneRenderer::PlaneRenderer() {
    vertices_.fill(0.0f);
    indices_.fill(0);
}

PlaneRenderer::~PlaneRenderer() {
    if (plane_pose_) {
        ArPose_destroy(plane_pose_);
    }
    if (vertex_buffer_) {
        glDeleteBuffers(1, &vertex_buffer_);
    }
    if (index_buffer_) {
        glDeleteBuffers(1, &index_buffer_);
    }
    if (shader_program_) {
        glDeleteProgram(shader_program_);
    }
}

void PlaneRenderer::Initialize(const ArSession* session) {
    if (initialized_) return;

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    const GLchar* vert_src = kPlaneVertexShader;
    glShaderSource(vertex_shader, 1, &vert_src, nullptr);
    glCompileShader(vertex_shader);

    GLint compiled = 0;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint log_length = 0;
        glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 0) {
            char log[512];
            glGetShaderInfoLog(vertex_shader, 512, nullptr, log);
            aout << "PlaneRenderer vertex shader error: " << log << std::endl;
        }
        glDeleteShader(vertex_shader);
        return;
    }

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    const GLchar* frag_src = kPlaneFragmentShader;
    glShaderSource(fragment_shader, 1, &frag_src, nullptr);
    glCompileShader(fragment_shader);

    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint log_length = 0;
        glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 0) {
            char log[512];
            glGetShaderInfoLog(fragment_shader, 512, nullptr, log);
            aout << "PlaneRenderer fragment shader error: " << log << std::endl;
        }
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return;
    }

    shader_program_ = glCreateProgram();
    glAttachShader(shader_program_, vertex_shader);
    glAttachShader(shader_program_, fragment_shader);
    glLinkProgram(shader_program_);

    GLint linked = 0;
    glGetProgramiv(shader_program_, GL_LINK_STATUS, &linked);
    if (!linked) {
        aout << "PlaneRenderer shader link failed" << std::endl;
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return;
    }

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    mvp_uniform_ = glGetUniformLocation(shader_program_, "u_MVP");
    color_uniform_ = glGetUniformLocation(shader_program_, "u_Color");

    glGenBuffers(1, &vertex_buffer_);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenBuffers(1, &index_buffer_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(uint16_t), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    if (session) {
        ArPose_create(session, nullptr, &plane_pose_);
    }

    initialized_ = true;
    aout << "PlaneRenderer initialized" << std::endl;
}

void PlaneRenderer::Update(const ArSession* session, const ArTrackableList* plane_list) {
    if (!initialized_ || !session || !plane_list || !enabled_) {
        plane_count_ = 0;
        return;
    }

    if (!plane_pose_) {
        ArPose_create(session, nullptr, &plane_pose_);
    }

    plane_count_ = 0;
    vertex_count_ = 0;
    index_count_ = 0;

    int32_t trackable_count = 0;
    ArTrackableList_getSize(session, plane_list, &trackable_count);

    for (int i = 0; i < trackable_count && plane_count_ < kMaxPlanes; ++i) {
        ArTrackable* trackable = nullptr;
        ArTrackableList_acquireItem(session, plane_list, i, &trackable);
        if (!trackable) {
            continue;
        }

        ArTrackableType type = AR_TRACKABLE_NOT_VALID;
        ArTrackable_getType(session, trackable, &type);
        if (type != AR_TRACKABLE_PLANE) {
            ArTrackable_release(trackable);
            continue;
        }

        ArTrackingState tracking_state = AR_TRACKING_STATE_STOPPED;
        ArTrackable_getTrackingState(session, trackable, &tracking_state);
        if (tracking_state != AR_TRACKING_STATE_TRACKING) {
            ArTrackable_release(trackable);
            continue;
        }

        ArPlane* plane = ArAsPlane(trackable);
        const float* polygon = nullptr;
        int32_t polygon_size = 0;
        // ARCore NDK: ArPlane_getPolygon returns [x0, z0, x1, z1, ...] in plane-local space.
        ArPlane_getPolygon(session, plane, &polygon, &polygon_size);
        const int vertex_count = polygon_size / 2;
        if (!polygon || vertex_count < 3) {
            ArTrackable_release(trackable);
            continue;
        }

        if (vertex_count_ + vertex_count > kMaxVertices) {
            ArTrackable_release(trackable);
            break;
        }

        ArPlane_getCenterPose(session, plane, plane_pose_);
        float plane_matrix[16];
        ArPose_getMatrix(session, plane_pose_, plane_matrix);

        const int vertex_start = vertex_count_;
        const int safe_vertex_count = std::min(vertex_count, kMaxVerticesPerPlane);
        for (int v = 0; v < safe_vertex_count; ++v) {
            const float local_x = polygon[v * 2 + 0];
            const float local_z = polygon[v * 2 + 1];
            const float local_y = 0.0f;

            const float world_x = plane_matrix[0] * local_x +
                                  plane_matrix[4] * local_y +
                                  plane_matrix[8] * local_z +
                                  plane_matrix[12];
            const float world_y = plane_matrix[1] * local_x +
                                  plane_matrix[5] * local_y +
                                  plane_matrix[9] * local_z +
                                  plane_matrix[13];
            const float world_z = plane_matrix[2] * local_x +
                                  plane_matrix[6] * local_y +
                                  plane_matrix[10] * local_z +
                                  plane_matrix[14];

            const int dst = (vertex_start + v) * 3;
            vertices_[dst + 0] = world_x;
            vertices_[dst + 1] = world_y;
            vertices_[dst + 2] = world_z;
        }

        int triangle_indices = 0;
        if (safe_vertex_count >= 3) {
            const int available = kMaxIndices - index_count_;
            triangle_indices = TriangulatePolygon(polygon, safe_vertex_count,
                                                  static_cast<uint16_t>(vertex_start),
                                                  indices_.data() + index_count_,
                                                  available);
        }

        if (triangle_indices > 0) {
            PlaneDrawInfo& info = plane_draw_info_[plane_count_++];
            info.vertex_start = vertex_start;
            info.vertex_count = safe_vertex_count;
            info.index_start = index_count_;
            info.index_count = triangle_indices;
            ArPlane_getType(session, plane, &info.type);

            vertex_count_ += safe_vertex_count;
            index_count_ += triangle_indices;
        }

        ArTrackable_release(trackable);
    }

    UploadBuffers();
}

void PlaneRenderer::UploadBuffers() {
    if (vertex_count_ == 0 || index_count_ == 0) return;

    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_count_ * 3 * sizeof(float), vertices_.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_count_ * sizeof(uint16_t), indices_.data());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

int PlaneRenderer::TriangulatePolygon(const float* polygon, int vertex_count, uint16_t base_index,
                                      uint16_t* out_indices, int max_indices) const {
    if (vertex_count < 3 || max_indices < (vertex_count - 2) * 3) return 0;

    float area = 0.0f;
    for (int i = 0; i < vertex_count; ++i) {
        const int j = (i + 1) % vertex_count;
        area += polygon[i * 2 + 0] * polygon[j * 2 + 1] -
                polygon[j * 2 + 0] * polygon[i * 2 + 1];
    }
    const bool ccw = area >= 0.0f;

    std::array<int, kMaxVerticesPerPlane> index_list{};
    for (int i = 0; i < vertex_count; ++i) {
        index_list[i] = i;
    }

    int remaining = vertex_count;
    int out_count = 0;
    int guard = 0;

    while (remaining > 2 && guard++ < vertex_count * vertex_count) {
        bool ear_found = false;
        for (int i = 0; i < remaining; ++i) {
            const int prev = index_list[(i + remaining - 1) % remaining];
            const int curr = index_list[i];
            const int next = index_list[(i + 1) % remaining];

            const float ax = polygon[prev * 2 + 0];
            const float az = polygon[prev * 2 + 1];
            const float bx = polygon[curr * 2 + 0];
            const float bz = polygon[curr * 2 + 1];
            const float cx = polygon[next * 2 + 0];
            const float cz = polygon[next * 2 + 1];

            const float cross = (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
            if (ccw ? (cross <= 0.0f) : (cross >= 0.0f)) {
                continue;
            }

            bool contains_vertex = false;
            for (int j = 0; j < remaining; ++j) {
                const int test = index_list[j];
                if (test == prev || test == curr || test == next) continue;
                if (IsPointInTriangle(polygon, prev, curr, next, test, ccw)) {
                    contains_vertex = true;
                    break;
                }
            }

            if (contains_vertex) {
                continue;
            }

            out_indices[out_count++] = base_index + static_cast<uint16_t>(prev);
            out_indices[out_count++] = base_index + static_cast<uint16_t>(curr);
            out_indices[out_count++] = base_index + static_cast<uint16_t>(next);

            for (int k = i; k < remaining - 1; ++k) {
                index_list[k] = index_list[k + 1];
            }
            --remaining;
            ear_found = true;
            break;
        }

        if (!ear_found) {
            break;
        }
    }

    if (remaining > 2 && out_count == 0) {
        for (int i = 1; i < vertex_count - 1; ++i) {
            out_indices[out_count++] = base_index;
            out_indices[out_count++] = base_index + static_cast<uint16_t>(i);
            out_indices[out_count++] = base_index + static_cast<uint16_t>(i + 1);
        }
    }

    return out_count;
}

void PlaneRenderer::MultiplyMatrix(float* out, const float* a, const float* b) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            out[i * 4 + j] = 0.0f;
            for (int k = 0; k < 4; ++k) {
                out[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
            }
        }
    }
}

void PlaneRenderer::Draw(const float* view_matrix, const float* projection_matrix) const {
    if (!initialized_ || !enabled_ || plane_count_ == 0) return;

    float mvp[16];
    MultiplyMatrix(mvp, projection_matrix, view_matrix);

    glUseProgram(shader_program_);
    glUniformMatrix4fv(mvp_uniform_, 1, GL_FALSE, mvp);

    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (int i = 0; i < plane_count_; ++i) {
        const PlaneDrawInfo& info = plane_draw_info_[i];
        const bool is_horizontal = (info.type == AR_PLANE_HORIZONTAL_UPWARD_FACING ||
                                    info.type == AR_PLANE_HORIZONTAL_DOWNWARD_FACING);
        if (is_horizontal) {
            glUniform4f(color_uniform_, 0.2f, 0.8f, 0.9f, 0.25f);
        } else {
            glUniform4f(color_uniform_, 0.9f, 0.5f, 0.2f, 0.25f);
        }

        glDrawElements(GL_TRIANGLES, info.index_count, GL_UNSIGNED_SHORT,
                       reinterpret_cast<const void*>(info.index_start * sizeof(uint16_t)));

        if (is_horizontal) {
            glUniform4f(color_uniform_, 0.4f, 0.95f, 1.0f, 0.8f);
        } else {
            glUniform4f(color_uniform_, 1.0f, 0.7f, 0.4f, 0.8f);
        }
        glLineWidth(2.0f);
        glDrawArrays(GL_LINE_LOOP, info.vertex_start, info.vertex_count);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glUseProgram(0);
}
