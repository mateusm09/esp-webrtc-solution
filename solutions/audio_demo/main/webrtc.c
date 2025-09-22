#include "esp_webrtc.h"
#include "media_lib_os.h"
#include "esp_log.h"
#include "mqtt_signaling.h"
#include "esp_webrtc_defaults.h"
#include "esp_peer_default.h"
#include "common.h"
#include "settings.h"
#include "esp_peer_signaling.h"
#include "mqtt_client.h"

#define TAG "WEBRTC"

static esp_webrtc_handle_t webrtc;

static int on_data_channel(esp_webrtc_custom_data_via_t via, uint8_t *data, int size, void *ctx)
{
    if (via == ESP_WEBRTC_CUSTOM_DATA_VIA_SIGNALING)
    {
        if (strcmp((char *)data, "connected") == 0)
        {
            esp_webrtc_enable_peer_connection(webrtc, true);
        }
    }
    else if (via == ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL)
    {
        ESP_LOGI(TAG, "Data from data channel: %s", data);
    }
    return 0;
}

esp_webrtc_handle_t getWebrtc()
{
    return webrtc;
}
static int webrtc_event(esp_webrtc_event_t *event, void *ctx)
{
    if (event->type == ESP_WEBRTC_EVENT_CONNECTED)
    {
        inCall();
        ESP_LOGI(TAG, "WEBRTC Connected");
        printf("Connected!!!!!!!!!!\n");
    }
    else if (event->type == ESP_WEBRTC_EVENT_DISCONNECTED)
    {
        // outCall(webrtc);
        ESP_LOGI(TAG, "WEBRTC Disconnected");
    }
    else if (event->type == ESP_WEBRTC_EVENT_CONNECT_FAILED)
    {
        // outCall(webrtc);

        ESP_LOGI(TAG, "WEBRTC Connect Failed");
    }

    return 0;
}

int start_webrtc(char *room_id, esp_mqtt_client_handle_t handle)
{
    mqtt_signaling_cfg sig_cfg = {
        .mqtt_client = handle,
    };

    esp_peer_default_cfg_t peer_cfg = {
        .agent_recv_timeout = 500,
    };

    esp_peer_ice_server_cfg_t server_info[] = {{.stun_url = "stun:stun.l.google.com:19302"},
                                               {.stun_url = "turn:turn.ppacontatto.com.br:3478",
                                                .user = "sparta",
                                                .psw = "sucodemacaco1201"}};

    esp_webrtc_cfg_t webrtc_cfg = {
        .peer_cfg = {
            .server_lists = server_info,
            .server_num = 2,
            .audio_info = {
                .codec = ESP_PEER_AUDIO_CODEC_G711A,
                .channel = 1,
                .sample_rate = 8000,
            },
            .audio_dir = ESP_PEER_MEDIA_DIR_SEND_RECV,
            .video_dir = ESP_PEER_MEDIA_DIR_NONE,
            .enable_data_channel = false,
            .no_auto_reconnect = true,
            .on_custom_data = on_data_channel,
            .extra_cfg = &peer_cfg,
            .extra_size = sizeof(peer_cfg),
        },
        .signaling_cfg = {
            .signal_url = MQTT_URL,
            .extra_cfg = &sig_cfg,
            .extra_size = sizeof(sig_cfg),
        },
        .peer_impl = esp_peer_get_default_impl(),
        .signaling_impl = esp_signaling_get_mqtt_impl(),
    };
    int ret = esp_webrtc_open(&webrtc_cfg, &webrtc);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Fail to open webrtc");
        return ret;
    }
    esp_webrtc_media_provider_t media_provider;
    media_provider_get(&media_provider);
    esp_webrtc_set_media_provider(webrtc, &media_provider);

    esp_webrtc_set_event_handler(webrtc, webrtc_event, NULL);
    esp_webrtc_enable_peer_connection(webrtc, false);

    ret = esp_webrtc_start(webrtc);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Fail to start webrtc");
        return ret;
    }

    return ret;
}

int query_webrtc()
{
    if (webrtc)
    {
        return esp_webrtc_query(webrtc);
    }
    return 0;
}

int stop_webrtc()
{
    if (webrtc)
    {
        esp_webrtc_handle_t handle = webrtc;
        esp_webrtc_stop(handle);
        esp_webrtc_close(handle);
        webrtc = NULL;
    }
    return 0;
}