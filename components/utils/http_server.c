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

void connect_wifi(){
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&config);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS
        }
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();
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

void start_http_server(){
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_handle_t server = NULL;
    httpd_start(&server, &config);

    httpd_uri_t photo_uri = {
        .uri = "/photo.jpg",
        .method = HTTP_GET,
        .handler = send_file_handler,
        .user_ctx = NULL
    };
    
    httpd_register_uri_handler(server, &photo_uri);
}
