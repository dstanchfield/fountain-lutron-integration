typedef struct {
    int id;
    int action;
    float level;

} LutronOutputState;

typedef struct {
    int id;
    int action;

} LutronQuery;

void lutron_on_device_events(esp_event_handler_t event_handler);
void lutron_on_connect_events(esp_event_handler_t event_handler);
void lutron_run_query(LutronQuery* lq);
void lutron_apply_state(LutronOutputState* lo);
void lutron_client_task(void *pvParameters);