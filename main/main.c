#include <stdio.h>
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"

#include "http_server.h"

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

static const char* TAG = "PROGRAM";

esp_err_t init_filesystem(const char* base_path, const char* partition_label){
    ESP_LOGI(TAG, "Mounting SPIFFS...");

    esp_vfs_spiffs_conf_t config = {
        .base_path = base_path,
        .partition_label = partition_label,
        .max_files = 5,
        .format_if_mount_failed = false
    };
    
    esp_err_t result = esp_vfs_spiffs_register(&config);
    if (result == ESP_OK){
        ESP_LOGI(TAG, "SPIFFS mounted successfully");
        return ESP_OK;
    }
    
    ESP_LOGW(TAG, "Failed to mount SPIFFS (%s), trying to format...", esp_err_to_name(result));
    result = esp_spiffs_format(partition_label);
    if (result != ESP_OK){
        ESP_LOGE(TAG, "Failed to format SPIFFS (%s)", esp_err_to_name(result));
        return result;
    }

    result = esp_vfs_spiffs_register(&config);
    if (result != ESP_OK){
        ESP_LOGE(TAG, "Failed to mount SPIFFS after format (%s)", esp_err_to_name(result));
    }

    ESP_LOGI(TAG, "SPIFFS mounted successfully");
    return ESP_OK;
}

esp_err_t init_camera(){
    camera_config_t config = {
        .pin_pwdn = PWDN_GPIO_NUM,
        .pin_reset = RESET_GPIO_NUM,
        .pin_xclk = XCLK_GPIO_NUM,
        .pin_sccb_sda = SIOD_GPIO_NUM,
        .pin_sccb_scl = SIOC_GPIO_NUM,
        .pin_d7 = Y9_GPIO_NUM,
        .pin_d6 = Y8_GPIO_NUM,
        .pin_d5 = Y7_GPIO_NUM,
        .pin_d4 = Y6_GPIO_NUM,
        .pin_d3 = Y5_GPIO_NUM,
        .pin_d2 = Y4_GPIO_NUM,
        .pin_d1 = Y3_GPIO_NUM,
        .pin_d0 = Y2_GPIO_NUM,
        .pin_vsync = VSYNC_GPIO_NUM,
        .pin_href = HREF_GPIO_NUM,
        .pin_pclk = PCLK_GPIO_NUM,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_SVGA,
        .jpeg_quality = 10,
        .fb_count = 1,
        .grab_mode = CAMERA_GRAB_LATEST
    };

    esp_err_t result = esp_camera_init(&config);
    if (result != ESP_OK){
        ESP_LOGE(TAG, "Camera init failed with error %s", esp_err_to_name(result));
        return result;
    }

    ESP_LOGI(TAG, "Camera initialized successfully");
    return ESP_OK;
}

void app_main(){
    ESP_ERROR_CHECK(nvs_flash_init());

    esp_err_t result = init_filesystem("/spiflash", NULL);
    if (result != ESP_OK){
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s), cannot continue", esp_err_to_name(result));
        return;
    }

    result = init_camera();
    if (result != ESP_OK){
        ESP_LOGE(TAG, "Failed to init camera (%s), cannot continue", esp_err_to_name(result));
        return;
    }

    ESP_LOGI(TAG, "Taking picture...");
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb){
        ESP_LOGE(TAG, "Camera capture failed");
        return;
    }

    FILE* file = fopen("/spiflash/photo.jpg", "wb");
    if (!file){
        ESP_LOGE(TAG, "Failed to open file");
        esp_camera_fb_return(fb);
        return;
    }

    size_t written = fwrite(fb->buf, 1, fb->len, file);

    if (written != fb->len){
        ESP_LOGE(TAG, "Failed to write complete image");
        esp_camera_fb_return(fb);
        fclose(file);
        return;
    }

    esp_camera_fb_return(fb);
    fclose(file);
    ESP_LOGI(TAG, "File saved to /spiflash/photo.jpg");

    result = connect_wifi();
    if (result != ESP_OK){
        ESP_LOGE(TAG, "Failed to connect WIFI: %s", esp_err_to_name(result));
        return;
    }

    ESP_LOGI(TAG, "WIFI connected");
    start_http_server();
}
