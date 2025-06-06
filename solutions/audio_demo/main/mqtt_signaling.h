
#include "esp_peer_signaling.h"
#include "mqtt_client.h"
const esp_peer_signaling_impl_t *esp_signaling_get_mqtt_impl(void);
int mqtt_start(esp_mqtt_client_handle_t *mqtt_client);

typedef struct _mqtt_signaling_cfg
{
    esp_mqtt_client_handle_t mqtt_client; /*!< MQTT client handle */
    esp_peer_signaling_cfg_t cfg;
} mqtt_signaling_cfg;

typedef struct _mqtt_signaling_message
{
    char *type;
    char *data;
    int data_len;
    int data_offset;
    int total_data_len;
} mqtt_signaling_message;

enum mqtt_topic_position
{
    NS = 0,
    SERIAL = 1,
    FUNCTION = 2,
    VERB = 3,
};

#define MQTT_FUNCTION_TERMINATE "12"
#define MQTT_FUNCTION_HARDWARE "13"
#define MQTT_FUNCTION_SDP "16"
#define MQTT_FUNCTION_ICE "17"

#define MQTT_VERB_REQUEST "0"
#define MQTT_VERB_RESPONSE "1"
#define MQTT_VERB_WRITE "2"
#define MQTT_VERB_CONFIRMATION "3"