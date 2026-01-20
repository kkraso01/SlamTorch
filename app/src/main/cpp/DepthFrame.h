#ifndef SLAMTORCH_DEPTH_FRAME_H
#define SLAMTORCH_DEPTH_FRAME_H

#include "arcore/arcore_c_api.h"
#include <cstdint>

struct DepthFrame {
    const uint16_t* depth_data = nullptr;
    int width = 0;
    int height = 0;
    int row_stride = 0;
    int pixel_stride = 0;
    int32_t format = 0;
    int64_t timestamp_ns = 0;
    const uint8_t* confidence_data = nullptr;
    int confidence_row_stride = 0;
    int confidence_pixel_stride = 0;
    int32_t confidence_format = 0;
    bool is_raw = false;
};

#endif // SLAMTORCH_DEPTH_FRAME_H
