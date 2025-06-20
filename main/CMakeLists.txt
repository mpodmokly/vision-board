idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES spiffs esp32-camera nvs_flash utils
)
