#include "esp_event.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lutron.h"
#include "log_server.h"

static const char *TAG = "Lutron client";

ESP_EVENT_DEFINE_BASE(LUTRON_EVENTS);

static esp_event_handler_t output_event_handlers[10];
static int output_event_handlers_count = 0;
static esp_event_handler_t connected_event_handlers[10];
static int connected_event_handlers_count = 0;

static bool lutron_connected = false;
static int sock;

typedef enum {
    LUTRON_EVENT_LOGIN,          /*!< Lutron username request */
    LUTRON_EVENT_PASSWORD,       /*!< Lutron password request */
    LUTRON_EVENT_MONITOR,        /*!< Lutron event monitoring */
    LUTRON_EVENT_OUTPUT,         /*!< Lutron output event */
    LUTRON_EVENT_CONNECTED,      /*!< Lutron connected event */
} lutron_event_t;

typedef struct {
    const char* message;
} SocketEventData;

bool is_lutron_output_event(char* str) {
    int count = 0;

    for (int i = 0; str[i]; i++) {
        if (str[i] == ',') {
            count++;
        }
    }

    return count >= 3 && (strncmp(str, "~OUTPUT,", strlen("~OUTPUT,")) == 0);
}

static void parse_lutron_output_event(char* str) {
    LutronOutputState lo;

    char* token = strtok(str, ",");

    token = strtok(NULL, ",");
    lo.id = atoi(token);

    token = strtok(NULL, ",");
    lo.action = atoi(token);

    if (lo.action == 1 || lo.action == 9 || lo.action == 10 || lo.action == 32) {
        token = strtok(NULL, ",");
        lo.level = atof(token);

        esp_event_post(LUTRON_EVENTS, LUTRON_EVENT_OUTPUT, &lo, sizeof(lo), 0);
    }
}

static void lutron_login_handler(void* arg, esp_event_base_t event_base,
                                 int32_t event_id, void* event_data)
{
    char buffer[50];

    ESP_LOGI(TAG, "Lutron login event");

    sprintf(buffer, "%s\r\n", CONFIG_LUTRON_USERNAME);
    int err = send(sock, buffer, strlen(buffer), 0);
    if (err < 0) {
        ESP_LOGE(TAG, "Error occurred during sending of login: errno %d", errno);
    }
}

static void lutron_password_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    char buffer[50];

    ESP_LOGI(TAG, "Lutron password event");

    sprintf(buffer, "%s\r\n", CONFIG_LUTRON_PASSWORD);
    int err = send(sock, buffer, strlen(buffer), 0);
    if (err < 0) {
        ESP_LOGE(TAG, "Error occurred during sending of password: errno %d", errno);
    }
}

static void lutron_monitor_handler(void* arg, esp_event_base_t event_base,
                                   int32_t event_id, void* event_data)
{
    static char raw_lutron_event[128];
    static size_t line_length = 0;

    SocketEventData *socket_event = (SocketEventData *)event_data;

    for (size_t i = 0; i < strlen(socket_event->message); i++) {
        if (socket_event->message[i] == '\r') {
            // Found carriage return
            continue;
        } else if (socket_event->message[i] == '\n') {
            // Found complete line
            if (line_length > 0) {
                raw_lutron_event[line_length] = '\0';

                if (is_lutron_output_event(raw_lutron_event)) {
                    parse_lutron_output_event(raw_lutron_event);
                    
                }

                line_length = 0;
            }
        } else if (line_length < 128 - 1) {
            // Append character to the line buffer
            raw_lutron_event[line_length] = socket_event->message[i];
            line_length++;
        }
    }
}

static void lutron_output_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    LutronOutputState *lo = (LutronOutputState *)event_data;
    ESP_LOGI(TAG, "Lutron output event: id: %d, action: %d, level: %.2f", lo->id, lo->action, lo->level);
}

static void lutron_connect()
{
    char rx_buffer[128];
    char host_ip[] = CONFIG_LUTRON_IP_ADDRESS;
    int addr_family = 0;
    int ip_protocol = 0;

    SocketEventData socket_event_data;

    while (1) {
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(host_ip);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(CONFIG_LUTRON_PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        sock =  socket(addr_family, SOCK_STREAM, ip_protocol);
        int keepAlive = 1;
        int keepIdle = CONFIG_LUTRON_KEEPALIVE_IDLE;
        int keepInterval = CONFIG_LUTRON_KEEPALIVE_INTERVAL;
        int keepCount = CONFIG_LUTRON_KEEPALIVE_COUNT;

        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created, connecting to %s:%d", host_ip, CONFIG_LUTRON_PORT);

        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
        if (err != 0) {
            ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Successfully connected");

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));

        while (1) {
            int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            // Error occurred during receiving
            if (len < 0) {
                ESP_LOGE(TAG, "recv failed: errno %d", errno);
                break;
            }
            // Data received
            else {
                rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
                ESP_LOGI(TAG, "Received %d bytes from %s:", len, host_ip);

                socket_event_data.message = rx_buffer;

                if (strncmp(rx_buffer, "login: ", strlen("login: ")) == 0) {
                    esp_event_post(LUTRON_EVENTS, LUTRON_EVENT_LOGIN, &socket_event_data, sizeof(socket_event_data), 0);
                } else if (strncmp(rx_buffer, "password: ", strlen("password: ")) == 0) {
                    esp_event_post(LUTRON_EVENTS, LUTRON_EVENT_PASSWORD, &socket_event_data, sizeof(socket_event_data), 0);
                } else if (strncmp(rx_buffer, "\r\nGNET> \x00", strlen("\r\nGNET> \x00")) == 0) {
                    lutron_connected = true;
                    esp_event_post(LUTRON_EVENTS, LUTRON_EVENT_CONNECTED, &socket_event_data, sizeof(socket_event_data), 0);
                    ESP_LOGI(TAG, "Received Lutron command prompt");
                } else {
                    esp_event_post(LUTRON_EVENTS, LUTRON_EVENT_MONITOR, &socket_event_data, sizeof(socket_event_data), 0);
                }
            }

            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
            lutron_connected = false;
            break;
        }
    }
}

void lutron_on_device_events(esp_event_handler_t event_handler) {
    if (output_event_handlers_count < 10) {
        output_event_handlers[output_event_handlers_count++] = event_handler;
    } else {
        ESP_LOGE(TAG, "Too many Lutron output handlers defined");
    }
    
};

void lutron_on_connect_events(esp_event_handler_t event_handler) {
    if (connected_event_handlers_count < 10) {
        connected_event_handlers[connected_event_handlers_count++] = event_handler;
    } else {
        ESP_LOGE(TAG, "Too many Lutron connect handlers defined");
    }
    
};

void lutron_run_query(LutronQuery* lq) {
    char buffer[50];
    int err = 0;

    if (lutron_connected) {
        ESP_LOGI(TAG, "Querying");
        sprintf(buffer, "?OUTPUT,%d,%d\r\n", lq->id, lq->action);
        err = send(sock, buffer, strlen(buffer), 0);

        if (err < 0) {
            ESP_LOGE(TAG, "Error occurred running query: errno %d", errno);
        }
    } else {
        ESP_LOGE(TAG, "Lutron not connected, cannot run command");
    }
}

void lutron_apply_state(LutronOutputState* lo) {
    char buffer[50];
    int err = 0;

    if (lutron_connected) {
        ESP_LOGI(TAG, "Applying state");
        sprintf(buffer, "#OUTPUT,%d,%d,%f\r\n", lo->id, lo->action, lo->level);
        err = send(sock, buffer, strlen(buffer), 0);

        if (err < 0) {
            ESP_LOGE(TAG, "Error occurred applying state: errno %d", errno);
        }
    } else {
        ESP_LOGE(TAG, "Lutron not connected, cannot run command");
    }
}

void lutron_client_task(void *pvParameters)
{
    ESP_ERROR_CHECK(esp_event_handler_instance_register(LUTRON_EVENTS, LUTRON_EVENT_LOGIN, lutron_login_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(LUTRON_EVENTS, LUTRON_EVENT_PASSWORD, lutron_password_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(LUTRON_EVENTS, LUTRON_EVENT_MONITOR, lutron_monitor_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(LUTRON_EVENTS, LUTRON_EVENT_OUTPUT, lutron_output_handler, NULL, NULL));

    for(int i = 0; i < output_event_handlers_count; i++) {
        ESP_ERROR_CHECK(esp_event_handler_instance_register(LUTRON_EVENTS, LUTRON_EVENT_OUTPUT, output_event_handlers[i], NULL, NULL));
    }

    for(int i = 0; i < connected_event_handlers_count; i++) {
        ESP_ERROR_CHECK(esp_event_handler_instance_register(LUTRON_EVENTS, LUTRON_EVENT_CONNECTED, connected_event_handlers[i], NULL, NULL));
    }

    while(1) {
        // keep attempting to connect
        lutron_connect();
        vTaskDelay(CONFIG_LUTRON_RECONNECT_DELAY * 1000 / portTICK_PERIOD_MS);
    }
}