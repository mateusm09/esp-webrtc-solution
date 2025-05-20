#include "mqtt_client.h"
#include <esp_log.h>
#include "mqtt_signaling.h"
#include <string.h>
#include "freertos/task.h"
#include "freertos/FreeRTOS.h"
#include "cJSON.h"
#include "settings.h"

#define TAG "MQTT_SIG"

static esp_peer_signaling_cfg_t peer_cfg;
static mqtt_signaling_message current_msg = {
    .data = NULL,
    .type = "",
    .data_len = 0,
    .data_offset = 0,
    .total_data_len = 0,
};

static int split_topic(char *topic, char **splitted_topic, char *delimiter, int max_parts)
{
    int i = 0;
    char *token = strtok(topic, delimiter);
    while (token != NULL && i < max_parts)
    {
        splitted_topic[i] = strdup(token);
        token = strtok(NULL, delimiter);
        i++;
    }
    return i;
}

static void free_array(char **array, int size)
{
    if (array == NULL)
        return;

    for (int i = 0; i < size; i++)
    {
        if (array[i] != NULL)
            free(array[i]);
    }
    free(array);
}

static void process_message(mqtt_signaling_message msg, esp_mqtt_client_handle_t *mqtt_client)
{
    if (current_msg.data == NULL)
    {
        current_msg.data = (char *)malloc(msg.total_data_len + 1);
        current_msg.total_data_len = msg.total_data_len;
    }
    memcpy(current_msg.data + msg.data_offset, msg.data, msg.data_len);
    current_msg.data[current_msg.total_data_len] = '\0';

    if (msg.type != NULL)
    {
        strcpy(current_msg.type, msg.type);
    }

    if (strlen(current_msg.data) != current_msg.total_data_len)
    {
        return;
    }

    ESP_LOGI(TAG, "Message received: %s", current_msg.data);

    cJSON *json = cJSON_Parse(current_msg.data);
    if (json == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            ESP_LOGE(TAG, "JSON Parse Error: %s", error_ptr);
        }
        return;
    }

    if (strcmp(current_msg.type, "16") == 0)
    {
        cJSON *sdp = cJSON_GetObjectItem(json, "sdp");
        esp_peer_signaling_msg_t peer_msg = {
            .type = ESP_PEER_SIGNALING_MSG_SDP,
            .data = (uint8_t *)sdp->valuestring,
            .size = strlen(sdp->valuestring),
        };

        peer_cfg.on_msg(&peer_msg, peer_cfg.ctx);
    }
    else if (strcmp(current_msg.type, "17") == 0)
    {
        cJSON *candidate = cJSON_GetObjectItem(json, "candidate");
        esp_peer_signaling_msg_t peer_msg = {
            .type = ESP_PEER_SIGNALING_MSG_CANDIDATE,
            .data = (uint8_t *)candidate->valuestring,
            .size = strlen(candidate->valuestring),
        };

        peer_cfg.on_msg(&peer_msg, peer_cfg.ctx);
    }
    else
    {
        ESP_LOGE(TAG, "Invalid topic type");
    }

    cJSON_Delete(json);
    current_msg.data_len = 0;
    current_msg.data_offset = 0;
    current_msg.type = "";
    current_msg.total_data_len = 0;
    free(current_msg.data);
    current_msg.data = NULL;

    char topic[128];
    sprintf(topic, "0/INABC123/%s/3", current_msg.type);

    const char data[] = "ok";
    esp_mqtt_client_publish(*mqtt_client, topic, data, 2, 2, false);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);

    const esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    const esp_mqtt_event_id_t typed_event_id = (esp_mqtt_event_id_t)event_id;
    esp_mqtt_client_handle_t mqtt_client = event->client;

    switch (typed_event_id)
    {
    case MQTT_EVENT_CONNECTED:
    {
        char *offer_topic = "0/INABC123/16/2";
        // sprintf(offer_topic, "0/%s//+", "123456");

        // char answer_topic[128];
        // sprintf(answer_topic, "room/%s/answer/+", "123456");

        char *ice_topic = "0/INABC123/17/2";
        // sprintf(ice_topic, "room/%s/ice-candidate/+", "123456");

        esp_mqtt_topic_t topics[] = {
            {.filter = offer_topic, .qos = 2},
            // {.filter = answer_topic, .qos = 2},
            {.filter = ice_topic, .qos = 2},
        };

        esp_mqtt_client_subscribe(mqtt_client, topics, 2);
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        if (peer_cfg.on_connected != NULL)
        {
            peer_cfg.on_connected(peer_cfg.ctx);
        }

        if (peer_cfg.on_ice_info != NULL)
        {
            ESP_LOGI(TAG, "Sending ICE info, event handler address: %p", peer_cfg.on_ice_info);
            esp_peer_signaling_ice_info_t ice_info = {
                .is_initiator = true,
                .server_info = {
                    .stun_url = "stun:stun.l.google.com:19302",
                },
            };
            peer_cfg.on_ice_info(&ice_info, peer_cfg.ctx);

            esp_peer_signaling_msg_t msg = {
                .data = (uint8_t *)"connected",
                .size = strlen("connected"),
                .type = ESP_PEER_SIGNALING_MSG_CUSTOMIZED,
            };
            peer_cfg.on_msg(&msg, peer_cfg.ctx);
        }

        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_DATA:
    {
        if (event->topic == NULL && event->data != NULL && strlen(current_msg.type) > 0)
        {
            process_message((mqtt_signaling_message){
                                .type = current_msg.type,
                                .data = event->data,
                                .data_len = event->data_len,
                                .data_offset = event->current_data_offset,
                                .total_data_len = event->total_data_len,
                            },
                            &mqtt_client);
            break;
        }
        else if (event->topic == NULL)
        {
            ESP_LOGE(TAG, "Invalid topic");
            break;
        }

        ESP_LOGI(TAG, "MQTT_EVENT_DATA, topic=%.*s,", event->topic_len, event->topic);
        // esperamos tópicos com o seguinte formato:
        // room/<room_id>/<type: offer|answer>/<client_id>
        // então devem ser 4 partes apenas.
        char **splitted_topic = malloc(4 * sizeof(char *));

        char sub_topic[event->topic_len + 1];
        strncpy(sub_topic, event->topic, event->topic_len);
        sub_topic[event->topic_len] = '\0';

        int n = split_topic(sub_topic, splitted_topic, "/", 4);
        // não tratar as próprias mensagens
        if (n < 4)
        {
            ESP_LOGE(TAG, "Invalid topic format");
            free_array(splitted_topic, 4);
            break;
        }
        if (strcmp(splitted_topic[CLIENT_ID], "esp_peer") == 0)
        {
            free_array(splitted_topic, 4);
            break;
        }

        char data[event->data_len + 1];
        strncpy(data, event->data, event->data_len);
        data[event->data_len] = '\0';

        process_message((mqtt_signaling_message){
                            .type = splitted_topic[TYPE],
                            .data = data,
                            .data_len = event->data_len,
                            .data_offset = 0,
                            .total_data_len = event->total_data_len,
                        },
                        &mqtt_client);

        free_array(splitted_topic, 4);
        break;
    }
    default:
        break;
    }
}

int mqtt_signal_start(esp_peer_signaling_cfg_t *cfg, esp_peer_signaling_handle_t *handle)
{
    // Start MQTT signaling

    if (cfg->signal_url == NULL || cfg == NULL || handle == NULL)
    {
        return -1;
    }

    mqtt_signaling_cfg *extra_cfg = (mqtt_signaling_cfg *)cfg->extra_cfg;

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = cfg->signal_url,
        .credentials = {
            .client_id = extra_cfg->client_id,
            .username = MQTT_USER,
            .authentication = {
                .password = MQTT_PASS,
            },
        },
    };

    ESP_LOGI(TAG, "Extra configs client: %s room id: %s address: %p, COREID: %d", extra_cfg->client_id, extra_cfg->room_id, extra_cfg, xPortGetCoreID());

    esp_mqtt_client_handle_t mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    if (cfg == NULL)
    {
        ESP_LOGE(TAG, "Invalid configuration");
        return -1;
    }

    // copia a config para evitar problemas de permissão de acesso de memória
    peer_cfg = *cfg;
    esp_mqtt_client_start(mqtt_client);
    *handle = mqtt_client;

    return 0;
}

int mqtt_signal_stop(esp_peer_signaling_handle_t handle)
{
    // Stop MQTT signaling
    const esp_mqtt_client_handle_t mqtt_client = handle;
    esp_mqtt_client_destroy(mqtt_client);
    return 0;
}

int mqtt_signal_send_msg(esp_peer_signaling_handle_t handle, esp_peer_signaling_msg_t *msg)
{
    char topic[128];
    char *str_msg;
    if (msg->type == ESP_PEER_SIGNALING_MSG_SDP)
    {
        sprintf(topic, "0/%s/16/1", "INABC123");
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "type", "offer");
        cJSON_AddStringToObject(json, "sdp", (char *)msg->data);

        str_msg = cJSON_PrintUnformatted(json);
    }
    else if (msg->type == ESP_PEER_SIGNALING_MSG_CANDIDATE)
    {
        sprintf(topic, "0/%s/17/1", "IN123ABC");
        cJSON *json = cJSON_CreateObject();

        cJSON_AddStringToObject(json, "type", "ice-candidate");
        cJSON_AddStringToObject(json, "candidate", (char *)msg->data);

        str_msg = cJSON_PrintUnformatted(json);
    }
    else
    {
        ESP_LOGE(TAG, "Invalid message type");
        return -1;
    }

    esp_mqtt_client_publish(handle, topic, str_msg, strlen(str_msg), 2, 0);

    return 0;
}

const esp_peer_signaling_impl_t *esp_signaling_get_mqtt_impl()
{
    static const esp_peer_signaling_impl_t impl = {
        .start = mqtt_signal_start,
        .send_msg = mqtt_signal_send_msg,
        .stop = mqtt_signal_stop,
    };
    return &impl;
}