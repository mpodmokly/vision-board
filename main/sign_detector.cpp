#include "sign_detector.h"
#include "esp_log.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <map>
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/micro_interpreter.h"

#define TAG "DETECTOR"

tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;
TfLiteTensor* output = nullptr;
uint8_t* patch_src = nullptr;
uint8_t* resized_patch = nullptr;

void resize_rgb888_nearest(
    const uint8_t* src, int src_w, int src_h,
    uint8_t* dst, int dst_w, int dst_h
) {
    for (int y = 0; y < dst_h; ++y) {
        int src_y = y * src_h / dst_h;
        for (int x = 0; x < dst_w; ++x) {
            int src_x = x * src_w / dst_w;
            const uint8_t* src_pixel = &src[(src_y * src_w + src_x) * 3];
            uint8_t* dst_pixel = &dst[(y * dst_w + x) * 3];
            dst_pixel[0] = src_pixel[0];
            dst_pixel[1] = src_pixel[1];
            dst_pixel[2] = src_pixel[2];
        }
    }
}

static bool is_candidate(uint8_t* rgb, int width, int x, int y, int patch_size) {
    int red_like = 0, total = 0;
    float r_sum = 0, g_sum = 0, b_sum = 0;
    float r_center = 0, g_center = 0, b_center = 0;
    int center_total = 0;

    int margin = patch_size / 4;
    int center_start = margin;
    int center_end = patch_size - margin;

    for (int j = 0; j < patch_size; ++j) {
        for (int i = 0; i < patch_size; ++i) {
            int px = x + i, py = y + j;
            uint8_t* pixel = &rgb[(py * width + px) * 3];
            float r = pixel[0] / 255.0f;
            float g = pixel[1] / 255.0f;
            float b = pixel[2] / 255.0f;

            float max = std::max({r, g, b});
            float min = std::min({r, g, b});
            float delta = max - min;

            float h = 0;
            if (delta > 0.0001f) {
                if (max == r) h = fmod((g - b) / delta, 6.0f);
                else if (max == g) h = ((b - r) / delta) + 2.0f;
                else h = ((r - g) / delta) + 4.0f;
                h *= 60.0f;
                if (h < 0) h += 360.0f;
            }

            float s = max == 0 ? 0 : delta / max;

            if (s > 0.25f && (h < 30.0f || h > 330.0f)) {
                red_like++;
            }

            r_sum += r; g_sum += g; b_sum += b;
            if (i >= center_start && i < center_end && j >= center_start && j < center_end) {
                r_center += r; g_center += g; b_center += b;
                center_total++;
            }

            total++;
        }
    }

    float red_ratio = static_cast<float>(red_like) / total;

    float r_avg = r_sum / total, g_avg = g_sum / total, b_avg = b_sum / total;
    float r_cavg = r_center / center_total, g_cavg = g_center / center_total, b_cavg = b_center / center_total;
    float contrast = fabs(r_avg - r_cavg) + fabs(g_avg - g_cavg) + fabs(b_avg - b_cavg);

    return (red_ratio > 0.06f) && (contrast > 0.2f);
}


void init_buffers() {
    patch_src = (uint8_t*)heap_caps_malloc(240 * 240 * 3, MALLOC_CAP_SPIRAM);
    resized_patch = (uint8_t*)heap_caps_malloc(64 * 64 * 3, MALLOC_CAP_SPIRAM);
    if (!patch_src || !resized_patch) {
        ESP_LOGE(TAG, "Nie udało się zaalokować buforów w PSRAM!");
    }
}

bool find_red_bbox(uint8_t* patch, int patch_size, int& out_x, int& out_y, int& out_w, int& out_h) {
    int min_x = patch_size, min_y = patch_size, max_x = 0, max_y = 0;
    bool found = false;

    for (int j = 0; j < patch_size; ++j) {
        for (int i = 0; i < patch_size; ++i) {
            uint8_t* pixel = &patch[(j * patch_size + i) * 3];
            float r = pixel[0] / 255.0f;
            float g = pixel[1] / 255.0f;
            float b = pixel[2] / 255.0f;

            float max_val = std::max({r, g, b});
            float min_val = std::min({r, g, b});
            float delta = max_val - min_val;

            float h = 0.0f;
            if (delta > 0.0001f) {
                if (max_val == r)      h = fmodf((g - b) / delta, 6.0f);
                else if (max_val == g) h = ((b - r) / delta) + 2.0f;
                else                   h = ((r - g) / delta) + 4.0f;
                h *= 60.0f;
                if (h < 0) h += 360.0f;
            }

            float s = max_val == 0 ? 0 : delta / max_val;

            if (s > 0.25f && (h < 30.0f || h > 330.0f)) {
                found = true;
                if (i < min_x) min_x = i;
                if (j < min_y) min_y = j;
                if (i > max_x) max_x = i;
                if (j > max_y) max_y = j;
            }
        }
    }

    if (!found) return false;

    int pad_x = (max_x - min_x + 1) / 10;
    int pad_y = (max_y - min_y + 1) / 10;

    out_x = std::max(0, min_x - pad_x);
    out_y = std::max(0, min_y - pad_y);
    out_w = std::min(patch_size - out_x, (max_x - min_x + 1) + 2 * pad_x);
    out_h = std::min(patch_size - out_y, (max_y - min_y + 1) + 2 * pad_y);

    return true;
}


int detect_in_image(uint8_t* image_rgb888, int width, int height, float* out_confidence) {
    const int input_size = 64;

    const int num_classes = output->dims->data[1];
    float scales[] = {1.0f, 0.75f, 0.56f, 0.42f, 0.31f, 0.22f, 0.17f};

    int patch_counter = 0;

    for (int s = 0; s < sizeof(scales)/sizeof(scales[0]); ++s) {
        ESP_LOGI(TAG, "Sprawdzanie: scale=%.2f", scales[s]);
        int patch_size = static_cast<int>(height * scales[s]);
        if (patch_size < 64 || patch_size > height || patch_size > width) continue;

        int stride = patch_size / 2;

        for (int y = 0; y <= height - patch_size; y += stride) {
            for (int x = 0; x <= width - patch_size; x += stride) {

                if (!is_candidate(image_rgb888, width, x, y, patch_size))
                    continue;

                for (int j = 0; j < patch_size; ++j) {
                    for (int i = 0; i < patch_size; ++i) {
                        const uint8_t* src = &image_rgb888[((y + j) * width + (x + i)) * 3];
                        uint8_t* dst = &patch_src[(j * patch_size + i) * 3];
                        dst[0] = src[0];
                        dst[1] = src[1];
                        dst[2] = src[2];
                    }
                }

                resize_rgb888_nearest(patch_src, patch_size, patch_size, resized_patch, input_size, input_size);

                float* in = input->data.f;
                for (int j = 0; j < input_size * input_size * 3; ++j) {
                    in[j] = (resized_patch[j] / 255.0f - 0.5f) / 0.5f;  // [0–255] -> [0–1] -> [-1,1]
                }

                if (interpreter->Invoke() != kTfLiteOk) {
                    ESP_LOGW(TAG, "Interpreter failed");
                    continue;
                }

                float* out = output->data.f;
                float logits[10], probs[10];

                float max_logit = -INFINITY;
                for (int i = 0; i < num_classes; ++i) {
                    logits[i] = out[i];
                    if (logits[i] > max_logit) max_logit = logits[i];
                }

                float sum_exp = 0;
                for (int i = 0; i < num_classes; ++i) {
                    probs[i] = expf(logits[i] - max_logit);
                    sum_exp += probs[i];
                }
                for (int i = 0; i < num_classes; ++i) {
                    probs[i] /= sum_exp;
                    ESP_LOGI(TAG, "  class=%d -> prob=%.4f", i, probs[i]);
                }

                int best_class = std::max_element(probs, probs + num_classes) - probs;
                float conf = probs[best_class];

                float second = 0.0f;
                for (int i = 0; i < num_classes; ++i) {
                    if (i != best_class && probs[i] > second)
                        second = probs[i];
                }

                float margin = conf - second;

                ESP_LOGI(TAG, "Patch x=%d y=%d scale=%.2f → class=%d conf=%.2f margin=%.2f",
                         x, y, scales[s], best_class, conf, margin);

                if (++patch_counter % 10 == 0) {
                    vTaskDelay(1);
                }

                if (conf > 0.4f && margin > 0.1f) {
                    *out_confidence = conf;
                    ESP_LOGI("DETECTOR", "Patch x=%d y=%d scale=%.2f → class=%d conf=%.4f", x, y, scales[s], best_class, conf);
                    return best_class;
                }
            }
        }
    }

    *out_confidence = 0.0f;
    return -1;
}
