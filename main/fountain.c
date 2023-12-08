#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lutron.h"
#include "log_server.h"

static const char *TAG = "Fountain";
static float fountain_current_level = 0.0;

static bool fountain_change_state(float level) {
    static int64_t time_since_fountain_state_change = 0;
    bool can_change_level = true;
    int64_t current_time_us = esp_timer_get_time();

    // we let the fountain have a cool down period to put less wear and tear on pump
    if (time_since_fountain_state_change != 0) {
        int64_t time_difference_us = current_time_us - time_since_fountain_state_change;
        double time_difference_s = time_difference_us / 1e6;

        if (time_difference_s <= CONFIG_FOUNTAIN_COOLOFF_PERIOD) {
            can_change_level = false;
        }
    }

    if (can_change_level) {
        ESP_LOGI(TAG, "Changing fountain pump state");
        gpio_set_level(CONFIG_FOUNTAIN_GPIO_PIN, (int)level);
        time_since_fountain_state_change = current_time_us;
        fountain_current_level = level;
    }

    return can_change_level;
}

static void fountain_switch_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    LutronOutputState lo;

    LutronOutputState *lo_event = (LutronOutputState *)event_data;

    if (lo_event->id == CONFIG_FOUNTAIN_LUTRON_INTEGRATION_ID) {
        if (lo_event->level != fountain_current_level) {
            if (!fountain_change_state(lo_event->level)) {
                ESP_LOGE(TAG, "Cannot change fountain state, cooldown period in effect");

                lo.id = CONFIG_FOUNTAIN_LUTRON_INTEGRATION_ID;
                lo.action = 1;
                lo.level = fountain_current_level;

                lutron_apply_state(&lo);
            }
        }
    } else if (lo_event->id == CONFIG_FOUNTAIN_LIGHTS_LUTRON_INTEGRATION_ID) {
        ESP_LOGI(TAG, "Changing fountain lights state");
        gpio_set_level(CONFIG_FOUNTAIN_LIGHTS_GPIO_PIN, (int)lo_event->level);
    }
}

static void fountain_ready_to_sync_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    LutronQuery lq;
    ESP_LOGI(TAG, "Syncing with Lutron");

    lq.id = CONFIG_FOUNTAIN_LUTRON_INTEGRATION_ID;
    lq.action = 1;

    lutron_run_query(&lq);

    lq.id = CONFIG_FOUNTAIN_LIGHTS_LUTRON_INTEGRATION_ID;
    lq.action = 1;

    lutron_run_query(&lq);
}

esp_err_t fountain_init(void) {
    gpio_reset_pin(CONFIG_FOUNTAIN_GPIO_PIN);
    gpio_set_direction(CONFIG_FOUNTAIN_GPIO_PIN, GPIO_MODE_OUTPUT);
    gpio_reset_pin(CONFIG_FOUNTAIN_LIGHTS_GPIO_PIN);
    gpio_set_direction(CONFIG_FOUNTAIN_LIGHTS_GPIO_PIN, GPIO_MODE_OUTPUT);

    // listen for lutron events
    lutron_on_device_events(fountain_switch_handler);
    lutron_on_connect_events(fountain_ready_to_sync_handler);

    return ESP_OK;
}
