
#include "esp_peer_signaling.h"
const esp_peer_signaling_impl_t *esp_signaling_get_mqtt_impl(void);

typedef struct _mqtt_signaling_cfg
{
    char *client_id;
    char *room_id;
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
    ROOM_ID = 1,
    TYPE = 2,
    CLIENT_ID = 3,
};
