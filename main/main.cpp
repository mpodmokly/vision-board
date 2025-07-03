#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"

#include "sign_detector.h"
#include "sign_model.h"

extern "C" {
#include "http_server.h"
#include "jpeg_decoder.h"
}

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/micro_interpreter.h"

#define TAG "MAIN"

static const char* class_names[] = {
    "50 speed limit",
    "give way",
    "STOP",
    "no vehicles",
    "no entry",
    "pedestrian crossing"
};

#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 21
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 19
#define Y4_GPIO_NUM 18
#define Y3_GPIO_NUM 5
#define Y2_GPIO_NUM 4
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

esp_err_t init_filesystem(const char* base_path, const char* partition_label) {
    ESP_LOGI(TAG, "Mounting SPIFFS...");

    esp_vfs_spiffs_conf_t config = {
        .base_path = base_path,
        .partition_label = partition_label,
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t result = esp_vfs_spiffs_register(&config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(result));
        return result;
    }

    ESP_LOGI(TAG, "SPIFFS mounted successfully");
    return ESP_OK;
}

esp_err_t init_camera() {
    camera_config_t config = {
        .pin_pwdn       = PWDN_GPIO_NUM,
        .pin_reset      = RESET_GPIO_NUM,
        .pin_xclk       = XCLK_GPIO_NUM,
        .pin_sccb_sda   = SIOD_GPIO_NUM,
        .pin_sccb_scl   = SIOC_GPIO_NUM,
        .pin_d7         = Y9_GPIO_NUM,
        .pin_d6         = Y8_GPIO_NUM,
        .pin_d5         = Y7_GPIO_NUM,
        .pin_d4         = Y6_GPIO_NUM,
        .pin_d3         = Y5_GPIO_NUM,
        .pin_d2         = Y4_GPIO_NUM,
        .pin_d1         = Y3_GPIO_NUM,
        .pin_d0         = Y2_GPIO_NUM,
        .pin_vsync      = VSYNC_GPIO_NUM,
        .pin_href       = HREF_GPIO_NUM,
        .pin_pclk       = PCLK_GPIO_NUM,
        .xclk_freq_hz   = 20000000,
        .ledc_timer     = LEDC_TIMER_0,
        .ledc_channel   = LEDC_CHANNEL_0,
        .pixel_format   = PIXFORMAT_JPEG,
        .frame_size     = FRAMESIZE_QVGA,
        .jpeg_quality   = 12,
        .fb_count       = 1,
        .fb_location    = CAMERA_FB_IN_PSRAM,
        .grab_mode      = CAMERA_GRAB_LATEST,
        .sccb_i2c_port  = 0
    };
    return esp_camera_init(&config);
}

void detect_task(void* arg) {
    uint8_t* rgb_buffer = (uint8_t*)arg;

    float confidence = 0.0f;
    int prediction = detect_in_image(rgb_buffer, 320, 240, &confidence);
    free(rgb_buffer);

    if (prediction >= 0 && prediction < (int)(sizeof(class_names) / sizeof(class_names[0]))) {
        ESP_LOGI("DETECTOR", "Znak: %s (conf: %.2f)", class_names[prediction], confidence);
    } else {
        ESP_LOGI("DETECTOR", "Brak znaku");
    }

    vTaskDelete(NULL);
}

extern "C" void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());

    if (init_filesystem("/spiffs", nullptr) != ESP_OK) return;
    if (init_camera() != ESP_OK) return;
    if (connect_wifi() != ESP_OK) return;
    if (start_http_server() != ESP_OK) return;

    ESP_LOGI(TAG, "Free RAM before capture: %d bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        return;
    }

    ESP_LOGI(TAG, "Free RAM after capture: %d bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));

    FILE* file = fopen("/spiffs/photo.jpg", "wb");
    if (file) {
        fwrite(fb->buf, 1, fb->len, file);
        fclose(file);
        ESP_LOGI(TAG, "Picture saved as /spiffs/photo.jpg");
    } else {
        ESP_LOGW(TAG, "Failed to save picture in SPIFFS");
    }

    int width = fb->width;
    int height = fb->height;
    uint8_t* rgb_buffer = (uint8_t*)malloc(width * height * 3);
    if (!rgb_buffer) {
        ESP_LOGE(TAG, "Failed to allocate RGB buffer");
        esp_camera_fb_return(fb);
        return;
    }

    esp_jpeg_image_cfg_t jpeg_cfg = {
        .indata = fb->buf,
        .indata_size = fb->len,
        .outbuf = rgb_buffer,
        .outbuf_size = static_cast<uint32_t>(width * height * 3),
        .out_format = JPEG_IMAGE_FORMAT_RGB888,
        .out_scale = JPEG_IMAGE_SCALE_0,
        .flags = {
            .swap_color_bytes = false
        },
        .advanced = {
            .working_buffer = NULL,
            .working_buffer_size = 0
        },
        .priv = {
            .read = 0
        }
    };

    esp_jpeg_image_output_t jpeg_out;

    esp_err_t decode_result = esp_jpeg_decode(&jpeg_cfg, &jpeg_out);

    if (decode_result != ESP_OK) {
        ESP_LOGE(TAG, "esp_jpeg_decode() failed");
        free(rgb_buffer);
        return;
    }

    esp_camera_fb_return(fb);

    ESP_LOGI(TAG, "Free RAM before model load: %d bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));

    const tflite::Model* model = tflite::GetModel(sign_model_tflite);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema mismatch!");
        free(rgb_buffer);
        return;
    }

    static tflite::MicroMutableOpResolver<14> resolver;
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddFullyConnected();
    resolver.AddSoftmax();
    resolver.AddReshape();
    resolver.AddAveragePool2D();
    resolver.AddPad();
    resolver.AddTranspose();
    resolver.AddMaxPool2D();
    resolver.AddMean();

    constexpr size_t tensor_arena_size = 384 * 1024;
    static uint8_t* tensor_arena = (uint8_t*)heap_caps_malloc(tensor_arena_size, MALLOC_CAP_SPIRAM);
    if (!tensor_arena) {
        ESP_LOGE(TAG, "Failed to allocate tensor_arena");
        return;
    }
    static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena, tensor_arena_size);
    interpreter = &static_interpreter;

    if (interpreter->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "Failed to allocate tensors");
        free(rgb_buffer);
        return;
    }

    input = interpreter->input(0);
    output = interpreter->output(0);

    ESP_LOGI(TAG, "Input tensor shape: %d x %d x %d",
         input->dims->data[1],  // height
         input->dims->data[2],  // width
         input->dims->data[3]); // channels

    ESP_LOGI(TAG, "Input: type=%" PRId32 ", scale=%.5f, zero_point=%" PRId32,
             (int32_t)input->type, input->params.scale, (int32_t)input->params.zero_point);
    ESP_LOGI(TAG, "Output: type=%" PRId32 ", scale=%.5f, zero_point=%" PRId32,
             (int32_t)output->type, output->params.scale, (int32_t)output->params.zero_point);

    init_buffers();

    xTaskCreatePinnedToCore(
        detect_task,
        "detect_task",
        8192,
        rgb_buffer,
        5,
        nullptr,
        1
    );
}
