#include "esp_log.h"
#include "network.h"
#include "settings.h"
#include "media_lib_os.h"
#include "media_lib_adapter.h"
#include "common.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "mqtt_signaling.h"

unsigned long last_millis = 0;
#define RUN_ASYNC(name, body)           \
    void run_async##name(void *arg)     \
    {                                   \
        body;                           \
        media_lib_thread_destroy(NULL); \
    }                                   \
    media_lib_thread_create_from_scheduler(NULL, #name, run_async##name, NULL);

#define TAG "MAIN"

// #define RELAY_PIN GPIO_NUM_20
#define ESP_INTR_FLAG_DEFAULT 0

#define GPIO_PUSH_BUTTON_PIN_SEL (1ULL << PUSH_BUTTON_PIN)
bool is_bell = false;
bool in_call = false;
static QueueHandle_t gpio_evt_queue = NULL;
typedef struct
{
    uint32_t pin;
    int state;
} gpio_event_t;

// # Input
#define GANCHO_IO GPIO_NUM_20       // Generic button
#define DETETEC_BELL_IO GPIO_NUM_12 // Bell button
#define GPIO_INPUT_PIN_SEL ((1ULL << DETETEC_BELL_IO))

#define DEFAULT_DEBOUNCING_TIME 500

static bool debounce_flag = false;

static esp_mqtt_client_handle_t mqtt_client = NULL;

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    int64_t current_time = esp_timer_get_time() / 1000; // Convert microseconds to milliseconds
    if ((current_time - last_millis) >= DEFAULT_DEBOUNCING_TIME)
    {
        uint32_t gpio_num = (uint32_t)arg;
        gpio_event_t evt;
        evt.pin = gpio_num;
        evt.state = gpio_get_level((gpio_num_t)gpio_num);
        xQueueSendFromISR(gpio_evt_queue, &evt, NULL);
        last_millis = current_time;
    }
}

void inCall()
{
    ESP_LOGI(TAG, "In Call");
    in_call = true;
    gpio_set_level(GANCHO_IO, true);
}

void outCall()
{
    ESP_LOGI(TAG, "Out Call");
    gpio_set_level(GANCHO_IO, false);
    in_call = false;
    stop_webrtc();
}

static int wifi_event_handler(bool connected)
{
    if (!connected)
    {
        return 0;
    }

    ESP_LOGI("wifi", "Connected to wifi");

    mqtt_start(&mqtt_client);

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

void control_service(void *parameters)
{
    ESP_LOGI(TAG, "[ * ] Control Service started");
    gpio_event_t evt;
    TickType_t lastButtonPressTime = 0;
    TickType_t lastGanchoPressTime = 0;
    const TickType_t debounceDelay = pdMS_TO_TICKS(300);

    gpio_config_t intr_gpio = {};
    intr_gpio.intr_type = GPIO_INTR_NEGEDGE;
    intr_gpio.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    intr_gpio.mode = GPIO_MODE_INPUT;
    intr_gpio.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&intr_gpio);

    // inicia o controle do gancho
    gpio_config_t gancho_config;
    gancho_config.intr_type = GPIO_INTR_DISABLE;
    gancho_config.pin_bit_mask = (1ULL << GANCHO_IO);
    gancho_config.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&gancho_config));

    gpio_set_level(GANCHO_IO, 0);

    // inicia o controle da fechadura 1
    gpio_config_t relay_1_config;
    relay_1_config.intr_type = GPIO_INTR_DISABLE;
    relay_1_config.pin_bit_mask = (1ULL << RELAY_1);
    relay_1_config.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&relay_1_config));

    // inicia o controle da fechadura 2
    gpio_config_t relay_2_config;
    relay_2_config.intr_type = GPIO_INTR_DISABLE;
    relay_2_config.pin_bit_mask = (1ULL << RELAY_2);
    relay_2_config.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&relay_2_config));

    gpio_set_intr_type(DETETEC_BELL_IO, GPIO_INTR_POSEDGE);
    gpio_install_isr_service(GPIO_INTR_POSEDGE);

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(DETETEC_BELL_IO, gpio_isr_handler, (void *)DETETEC_BELL_IO);

    for (;;)
    {
        if (xQueueReceive(gpio_evt_queue, &evt, portMAX_DELAY))
        {
            uint32_t pinNumber = evt.pin;

            if (pinNumber == DETETEC_BELL_IO && gpio_get_level(DETETEC_BELL_IO) == 0)
            {
                ESP_LOGI(TAG, "Bell pressed");
                is_bell = true;
            }
        }
    }
    vTaskDelete(NULL);
}

void app_main()
{
    media_lib_add_default_adapter();
    media_lib_thread_set_schedule_cb(thread_scheduler);

    xTaskCreate(control_service, "ControlService", 4096, NULL, 5, NULL);

    int ret = media_provider_init();
    if (ret < 0)
    {
        ESP_LOGE(TAG, "Failed to initialize media provider");
        esp_restart();
        return;
    }

    network_init(WIFI_SSID, WIFI_PASSWORD, wifi_event_handler);

    while (1)
    {
        if (is_bell && !in_call)
        {
            is_bell = false;
            RUN_ASYNC(webrtc_task, { start_webrtc(MQTT_URL, mqtt_client); });
        }
        media_lib_thread_sleep(2000);
    }
}