/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "log_server.h"
#include "lutron.h"
#include <lwip/netdb.h>

#include "protocol_examples_common.h"

esp_err_t fountain_init(void);

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(fountain_init());
    ESP_ERROR_CHECK(log_server_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    xTaskCreate(log_server_task, "log_server", 4096, (void*)AF_INET, 5, NULL);
    xTaskCreate(lutron_client_task, "lutron_client", 4096, NULL, 5, NULL);
}
