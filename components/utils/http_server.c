#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"

#include "wifi_config.h"

static const char* TAG = "HTTP SERVER";

static void wifi_event_handler(
    void* arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void* event_data
){
    if (event_id == WIFI_EVENT_STA_DISCONNECTED){
        wifi_event_sta_disconnected_t* disconnect = (wifi_event_sta_disconnected_t*)event_data;
        ESP_LOGE("wifi", "Fatal WIFI error: %d", disconnect->reason);
    }
}

esp_err_t connect_wifi(){
    esp_err_t result = esp_netif_init();
    if (result != ESP_OK){
        return result;
    }
    result = esp_event_loop_create_default();
    if (result != ESP_OK){
        return result;
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&config);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS
        }
    };

    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result != ESP_OK){
        return result;
    }
    result = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (result != ESP_OK){
        return result;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL,
        NULL
    ));

    result = esp_wifi_start();
    if (result != ESP_OK){
        return result;
    }
    result = esp_wifi_connect();
    if (result != ESP_OK){
        return result;
    }

    return ESP_OK;
}

esp_err_t send_file_handler(httpd_req_t* req){
    FILE* file = fopen("/spiflash/photo.jpg", "rb");
    if (!file){
        ESP_LOGE(TAG, "File error");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char buf[1024];
    size_t bytes;

    while ((bytes = fread(buf, 1, sizeof(buf), file)) > 0){
        httpd_resp_send_chunk(req, buf, bytes);
    }

    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t start_http_server(){
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_handle_t server = NULL;
    esp_err_t result = httpd_start(&server, &config);
    if (result != ESP_OK){
        return result;
    }

    httpd_uri_t photo_uri = {
        .uri = "/photo.jpg",
        .method = HTTP_GET,
        .handler = send_file_handler,
        .user_ctx = NULL
    };
    
    result = httpd_register_uri_handler(server, &photo_uri);
    if (result != ESP_OK){
        return result;
    }

    return ESP_OK;
}
