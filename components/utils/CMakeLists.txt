idf_component_register(
    SRCS "http_server.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_http_server
)
