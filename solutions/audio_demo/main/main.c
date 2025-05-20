#include "esp_log.h"
#include "network.h"
#include "settings.h"
#include "media_lib_os.h"
#include "media_lib_adapter.h"
#include "common.h"
#include "driver/gpio.h"
#include "esp_system.h"

#define RUN_ASYNC(name, body)           \
    void run_async##name(void *arg)     \
    {                                   \
        body;                           \
        media_lib_thread_destroy(NULL); \
    }                                   \
    media_lib_thread_create_from_scheduler(NULL, #name, run_async##name, NULL);

#define RELAY_PIN GPIO_NUM_20
#define PUSH_BUTTON_PIN GPIO_NUM_7

static int relay_state = 0;

static void IRAM_ATTR push_button_isr_handler(void *arg)
{
    relay_state = !relay_state;
    ESP_LOGI("push_button", "Button pressed, relay state: %d", relay_state);
    gpio_set_level(RELAY_PIN, relay_state);
}

static int wifi_event_handler(bool connected)
{
    if (!connected)
    {
        return 0;
    }

    ESP_LOGI("wifi", "Connected to wifi");

    RUN_ASYNC(webrtc_task, { start_webrtc(MQTT_URL); });

    return 0;
}

static void thread_scheduler(const char *thread_name, media_lib_thread_cfg_t *thread_cfg)
{
    if (strcmp(thread_name, "webrtc_task") == 0)
    {
        thread_cfg->stack_size = 6 * 1024;
    }
    if (strcmp(thread_name, "test_audio") == 0)
    {
        thread_cfg->stack_size = 25 * 1024;
    }
    if (strcmp(thread_name, "pc_task") == 0)
    {
        thread_cfg->stack_size = 32 * 1024;
        thread_cfg->priority = 18;
        thread_cfg->core_id = 1;
    }
    if (strcmp(thread_name, "pc_send") == 0)
    {
        thread_cfg->stack_size = 12 * 1024;
        thread_cfg->priority = 15;
        thread_cfg->core_id = 1;
    }
    if (strcmp(thread_name, "Adec") == 0)
    {
        thread_cfg->stack_size = 24 * 1024;
        thread_cfg->priority = 12;
        thread_cfg->core_id = 1;
    }
    if (strcmp(thread_name, "aenc") == 0)
    {
        thread_cfg->stack_size = 32 * 1024;
        thread_cfg->priority = 11;
    }
    if (strcmp(thread_name, "ARender") == 0)
    {
        thread_cfg->stack_size = 20 * 1024;
        thread_cfg->priority = 14;
        thread_cfg->core_id = 1;
    }
}

void app_main()
{
    media_lib_add_default_adapter();
    media_lib_thread_set_schedule_cb(thread_scheduler);

    media_provider_init();
    // RUN_ASYNC(test_audio, { test_capture_to_player(); });

    gpio_reset_pin(RELAY_PIN); // o pino 20 é utilizado no JTAG, então é necessário resetar o pino
    gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(PUSH_BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(PUSH_BUTTON_PIN);

    // gpio_set_intr_type(PUSH_BUTTON_PIN, GPIO_INTR_POSEDGE);
    // gpio_install_isr_service(0);
    // gpio_isr_handler_add(PUSH_BUTTON_PIN, push_button_isr_handler, NULL);

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("i2c.master", ESP_LOG_DEBUG);

    // network_init(WIFI_SSID, WIFI_PASSWORD, wifi_event_handler);
    gpio_set_level(RELAY_PIN, 1);

    int level = 0;
    int last_level = 0;
    while (1)
    {
        // level = gpio_get_level(PUSH_BUTTON_PIN);
        // if (level == 0 && last_level == 1)
        // {
        //     last_level = 0;
        //     relay_state = !relay_state;
        //     ESP_LOGI("push_button", "Button pressed, relay state: %d", relay_state);
        //     gpio_set_level(RELAY_PIN, relay_state);
        // }
        // else
        // {
        //     last_level = 1;
        // }

        media_lib_thread_sleep(2000);

        // media_lib_thread_sleep(2000);
        // query_webrtc();
        // media_provider_render_query();
    }
}