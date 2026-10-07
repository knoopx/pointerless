// SPDX-License-Identifier: GPL-3.0-only

// NCNN-based GUI icon detection for the Salesforce GPA-GUI-Detector YOLOv8
// model. Adapted from the reference detector in nihui/ncnn-android-yolov8
// (app/src/main/jni/yolov8_det.cpp), which is BSD-3-Clause licensed.
//
// The model is a single-class YOLOv8 ({0: "icon"}) with input (1,3,640,640)
// RGB 0-255. The ncnn export bakes the YOLOv8 DFL decode into the graph, so
// the single output tensor is (1,5,8400): per-anchor [cx, cy, w, h, score]
// over 8400 anchors (6400/1600/400 at strides 8/16/32), box coordinates in
// 640x640 letterbox pixel units, class score already activated.

#include "guidetect.h"

#include "log.h"

#include <layer.h>
#include <mat.h>
#include <net.h>
#include <option.h>
#include <paramdict.h>

#include <wayland-client.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

struct GpaObject {
    float x;
    float y;
    float w;
    float h;
    int   label;
    float prob;
};

static inline float intersection_area(const GpaObject &a, const GpaObject &b) {
    float x0 = a.x > b.x ? a.x : b.x;
    float y0 = a.y > b.y ? a.y : b.y;
    float x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    float y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);

    if (x0 >= x1 || y0 >= y1) {
        return 0.f;
    }

    return (x1 - x0) * (y1 - y0);
}

static void qsort_descent_inplace(
    std::vector<GpaObject> &objects, int left, int right
) {
    int   i = left;
    int   j = right;
    float p = objects[(left + right) / 2].prob;

    while (i <= j) {
        while (objects[i].prob > p) {
            i++;
        }

        while (objects[j].prob < p) {
            j--;
        }

        if (i <= j) {
            std::swap(objects[i], objects[j]);
            i++;
            j--;
        }
    }

    if (left < j) {
        qsort_descent_inplace(objects, left, j);
    }
    if (i < right) {
        qsort_descent_inplace(objects, i, right);
    }
}

static void qsort_descent_inplace(std::vector<GpaObject> &objects) {
    if (objects.empty()) {
        return;
    }

    qsort_descent_inplace(objects, 0, (int)objects.size() - 1);
}

static void nms_sorted_bboxes(
    const std::vector<GpaObject> &objects, std::vector<int> &picked,
    float nms_threshold
) {
    picked.clear();

    const int n = (int)objects.size();

    std::vector<float> areas(n);
    for (int i = 0; i < n; i++) {
        areas[i] = objects[i].w * objects[i].h;
    }

    for (int i = 0; i < n; i++) {
        const GpaObject &a = objects[i];

        int keep = 1;
        for (size_t j = 0; j < picked.size(); j++) {
            const GpaObject &b = objects[picked[j]];

            float inter_area = intersection_area(a, b);
            float union_area  = areas[i] + areas[picked[j]] - inter_area;
            if (union_area <= 0.f) {
                continue;
            }

            // Intersection over union.
            if (inter_area / union_area > nms_threshold) {
                keep = 0;
            }
        }

        if (keep) {
            picked.push_back(i);
        }
    }
}

static void generate_proposals(
    const ncnn::Mat &pred, float prob_threshold,
    std::vector<GpaObject> &objects
) {
    // The export bakes the YOLOv8 DFL decode into the graph, so `pred` is a
    // plain 5-channel head: 8400 anchors x [cx, cy, w, h, score] in 640x640
    // letterbox pixel units, class score already activated. The tensor is
    // (w=8400, h=5, c=1); the 5 channels run along h, so anchor i channel j
    // is `data[j*8400 + i]` (NOT a contiguous i*5 stride). The system ncnn
    // headers expose no 3-arg `operator()`, so read via the raw data pointer.
    if (pred.w != 8400 || pred.h != 5) {
        LOG_ERR(
            "gui_detect: unexpected output tensor %dx%d (expected 8400x5).",
            pred.w, pred.h
        );
        return;
    }

    const float *d = (const float *)pred.data;
    for (int i = 0; i < 8400; i++) {
        const float cx    = d[0 * 8400 + i];
        const float cy    = d[1 * 8400 + i];
        const float bw    = d[2 * 8400 + i];
        const float bh    = d[3 * 8400 + i];
        const float score = d[4 * 8400 + i];

        if (score < prob_threshold) {
            continue;
        }

        GpaObject obj;
        obj.x     = cx - bw / 2;
        obj.y     = cy - bh / 2;
        obj.w     = bw;
        obj.h     = bh;
        obj.label = 0;
        obj.prob  = score;

        objects.push_back(obj);
    }
}

/**
 * Convert a raw screencopy buffer to a tightly packed RGB buffer (3 bytes per
 * pixel, row-major). Handles the common 32-bit little-endian Wayland formats
 * where the buffer is tightly packed (stride == width * 4).
 */
static int convert_to_rgb(
    const void *pixels, int w, int h, int pixelformat, std::vector<uint8_t> &out
) {
    const uint8_t *src = (const uint8_t *)pixels;
    out.resize((size_t)w * h * 3);

    switch (pixelformat) {
    case WL_SHM_FORMAT_XRGB8888:
    case WL_SHM_FORMAT_ARGB8888:
        // Little-endian byte order in memory: B, G, R, X/A.
        for (int y = 0; y < h; y++) {
            const uint8_t *row = src + (size_t)y * w * 4;
            for (int x = 0; x < w; x++) {
                const uint8_t *p       = row + x * 4;
                uint8_t       *dst     = &out[((size_t)y * w + x) * 3];
                dst[0]                 = p[2]; // R
                dst[1]                 = p[1]; // G
                dst[2]                 = p[0]; // B
            }
        }
        return 0;

    case WL_SHM_FORMAT_XBGR8888:
    case WL_SHM_FORMAT_ABGR8888:
        // Little-endian byte order in memory: R, G, B, X/A.
        for (int y = 0; y < h; y++) {
            const uint8_t *row = src + (size_t)y * w * 4;
            for (int x = 0; x < w; x++) {
                const uint8_t *p       = row + x * 4;
                uint8_t       *dst     = &out[((size_t)y * w + x) * 3];
                dst[0]                 = p[0]; // R
                dst[1]                 = p[1]; // G
                dst[2]                 = p[2]; // B
            }
        }
        return 0;

    default:
        LOG_ERR("Unsupported pixel format 0x%08x.", pixelformat);
        return 1;
    }
}

ncnn::Net g_net;
bool      g_net_loaded = false;

/**
 * Resolve the bundled model base path (without the `.param`/`.bin`
 * suffix). The model files are installed with the package at
 * `GUIDETECT_MODEL_DIR` (the datadir `pointerless` directory, set at build
 * time); fall back to `model.ncnn` in the CWD when the macro is undefined.
 */
static int get_model_base_path(char *buf, size_t buf_len) {
#ifdef GUIDETECT_MODEL_DIR
    snprintf(buf, buf_len, "%s/model.ncnn", GUIDETECT_MODEL_DIR);
#else
    snprintf(buf, buf_len, "model.ncnn");
#endif

    return 0;
}

static int ensure_net_loaded() {
    if (g_net_loaded) {
        return 0;
    }

    char base[4096];
    get_model_base_path(base, sizeof(base));

    char param[4096 + 16];
    char bin[4096 + 16];
    snprintf(param, sizeof(param), "%s.param", base);
    snprintf(bin, sizeof(bin), "%s.bin", base);

    return gui_detect_load(param, bin);
}

} // namespace

int gui_detect_load(const char *param_path, const char *bin_path) {
    if (g_net_loaded) {
        return 0;
    }

    int ret = g_net.load_param(param_path);
    if (ret != 0) {
        LOG_ERR("Could not load NCNN param file '%s'.", param_path);
        return 1;
    }

    ret = g_net.load_model(bin_path);
    if (ret != 0) {
        LOG_ERR("Could not load NCNN model file '%s'.", bin_path);
        return 1;
    }

    g_net_loaded = true;
    LOG_INFO("Loaded GPA-GUI-Detector NCNN net.");

    return 0;
}

int gui_detect(
    struct state *state, const void *pixels, int w, int h, int pixelformat,
    struct rect **out_areas, int *out_count
) {
    *out_areas = NULL;
    *out_count = 0;

    (void)state;

    if (pixels == NULL || w <= 0 || h <= 0) {
        LOG_ERR("gui_detect: invalid input dimensions (%dx%d).", w, h);
        return 1;
    }

    if (ensure_net_loaded() != 0) {
        return 1;
    }

    const int target_size = 640;
    const float prob_threshold = 0.05f;
    // Match smoke_test.cpp's default NMS threshold.
    const float nms_threshold  = 0.7f;

    const int img_w = w;
    const int img_h = h;

    // Letterbox: scale to fit target_size on the long side.
    int   w_ = img_w;
    int   h_ = img_h;
    float scale = 1.f;
    if (w_ > h_) {
        scale  = (float)target_size / w_;
        w_     = target_size;
        h_     = (int)(h_ * scale);
    } else {
        scale = (float)target_size / h_;
        h_    = target_size;
        w_    = (int)(w_ * scale);
    }

    std::vector<uint8_t> rgb;
    if (convert_to_rgb(pixels, img_w, img_h, pixelformat, rgb) != 0) {
        return 1;
    }

    ncnn::Mat in = ncnn::Mat::from_pixels_resize(
        rgb.data(), ncnn::Mat::PIXEL_RGB, img_w, img_h, w_, h_
    );

    // Pad to a SQUARE target_size x target_size canvas (matches the
    // ultralytics letterbox the model was exported with), image centered.
    // The 8400-anchor decode requires the full 640x640 grid (80x80 + 40x40
    // + 20x20); any other canvas size changes the anchor count and breaks
    // the output layout.
    int wpad = target_size - w_;
    int hpad = target_size - h_;

    ncnn::Mat in_pad;
    ncnn::copy_make_border(
        in, in_pad, hpad / 2, hpad - hpad / 2, wpad / 2, wpad - wpad / 2,
        ncnn::BORDER_CONSTANT, 114.f
    );

    const float norm_vals[3] = {1 / 255.f, 1 / 255.f, 1 / 255.f};
    in_pad.substract_mean_normalize(0, norm_vals);

    ncnn::Extractor ex = g_net.create_extractor();
    ex.input("in0", in_pad);

    ncnn::Mat out;
    ex.extract("out0", out);

    std::vector<GpaObject> proposals;
    generate_proposals(out, prob_threshold, proposals);

    // Match smoke_test.cpp: decode to original image coordinates BEFORE
    // NMS — inverse letterbox, clip to bounds, drop degenerate boxes —
    // then run the greedy NMS in original space.
    std::vector<GpaObject> boxes;
    for (const GpaObject &p : proposals) {
        float x0 = (p.x - (wpad / 2)) / scale;
        float y0 = (p.y - (hpad / 2)) / scale;
        float x1 = (p.x + p.w - (wpad / 2)) / scale;
        float y1 = (p.y + p.h - (hpad / 2)) / scale;

        // Clip to the image bounds.
        x0 = std::max(std::min(x0, (float)(img_w - 1)), 0.f);
        y0 = std::max(std::min(y0, (float)(img_h - 1)), 0.f);
        x1 = std::max(std::min(x1, (float)(img_w - 1)), 0.f);
        y1 = std::max(std::min(y1, (float)(img_h - 1)), 0.f);

        // Drop degenerate/clamped boxes (w or h ~ 0 after edge clamping).
        if (x1 - x0 < 4.f || y1 - y0 < 4.f) {
            continue;
        }

        GpaObject b;
        b.x     = x0;
        b.y     = y0;
        b.w     = x1 - x0;
        b.h     = y1 - y0;
        b.label = 0;
        b.prob  = p.prob;
        boxes.push_back(b);
    }

    // Sort all boxes by score from highest to lowest, then apply NMS.
    qsort_descent_inplace(boxes);

    std::vector<int> picked;
    nms_sorted_bboxes(boxes, picked, nms_threshold);

    const int count = (int)picked.size();

    struct rect *areas = NULL;
    if (count > 0) {
        areas = (struct rect *)malloc(sizeof(struct rect) * count);
        if (areas == NULL) {
            LOG_ERR("gui_detect: out of memory.");
            return 1;
        }
    }

    int out_idx = 0;
    for (int i = 0; i < count; i++) {
        GpaObject b = boxes[picked[i]];

        areas[out_idx].x = (int32_t)std::round(b.x);
        areas[out_idx].y = (int32_t)std::round(b.y);
        areas[out_idx].w = (int32_t)std::round(b.w);
        areas[out_idx].h = (int32_t)std::round(b.h);
        out_idx++;
    }

    *out_areas  = areas;
    *out_count  = out_idx;

    LOG_DEBUG("gui_detect: %d UI box(es) detected.", out_idx);

    return 0;
}

