set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

idf_component_register(
    SRCS 
        "main.cpp"
        "sign_detector.cpp"
        "sign_model.cc"
    INCLUDE_DIRS "."
    REQUIRES spiffs esp32-camera nvs_flash utils tflite-micro-esp-examples esp_jpeg
)

target_link_libraries(${COMPONENT_LIB} "-u _printf_float")
