/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

static const char *TAG = "Log Server";
static int connected_clients[CONFIG_LOG_SERVER_MAX_CONNECTIONS];
static int connected_client_count = 0;

static void add_connected_client(int sock) {
    connected_clients[connected_client_count++] = sock;
}

static void remove_connected_client(int sock) {
    size_t i_write = 0;
    for (size_t i_read = 0; i_read < CONFIG_LOG_SERVER_MAX_CONNECTIONS; i_read++)
    {
        if (connected_clients[i_read] != sock)
        {
            connected_clients[i_write] = connected_clients[i_read];
            i_write++;
        }
    }
    connected_client_count--;
}

static void log_client_connected_task(void *pvParameters) {
    int sock = (int)pvParameters;
    int len;
    char rx_buffer[128];

    if (connected_client_count < CONFIG_LOG_SERVER_MAX_CONNECTIONS) {
        add_connected_client(sock);

        do {
            len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            if (len < 0) {
                ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
            } else if (len == 0) {
                remove_connected_client(sock);

                ESP_LOGW(TAG, "Connection closed");
            }
        } while (len > 0);
    } else {
        ESP_LOGE(TAG, "Maximum connections reached");
    }

    shutdown(sock, 0);
    close(sock);
    vTaskDelete(NULL);
}

static void broadcast_log_item(char* log_item) {
    int err = 0;

    for (size_t i = 0; i < connected_client_count; i++) {
        err = send(connected_clients[i], log_item, strlen(log_item), 0);

        if (err < 0) {
            ESP_LOGE(TAG, "Error occurred broadcasting log item: errno %d", errno);
        }
    }
}

int ls_print(const char *v, va_list l)
{
    char buffer[255];

    vsprintf(buffer, v, l);
    broadcast_log_item(buffer);

    return vprintf(v, l);
}

void log_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = CONFIG_LOG_SERVER_KEEPALIVE_IDLE;
    int keepInterval = CONFIG_LOG_SERVER_KEEPALIVE_INTERVAL;
    int keepCount = CONFIG_LOG_SERVER_KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(CONFIG_LOG_SERVER_PORT);
        ip_protocol = IPPROTO_IP;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", CONFIG_LOG_SERVER_PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {

        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }

        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        xTaskCreate(log_client_connected_task, "log_client_connected", 4096, (void*)sock, 5, NULL);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

esp_err_t log_server_init(void) {
    esp_log_set_vprintf(ls_print);

    return ESP_OK;
}