#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t connect_wifi();
esp_err_t send_file_handler(httpd_req_t* req);
esp_err_t start_http_server();

#endif // HTTP_SERVER_H
