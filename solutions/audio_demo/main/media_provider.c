#include "common.h"
#include "esp_capture.h"
#include "esp_capture_audio_enc.h"
#include "codec_init.h"
#include "codec_board.h"
#include "esp_capture_defaults.h"
#include "esp_capture_path_simple.h"
#include "esp_audio_enc_default.h"
#include "esp_log.h"
#include "av_render.h"
#include "av_render_default.h"
#include "esp_audio_dec_default.h"
#include "media_lib_os.h"
#include "esp_timer.h"

#define TAG "media_provider"

// capture handlers
esp_capture_handle_t capture_handle;
esp_capture_aenc_if_t *audio_encoder;
esp_capture_audio_src_if_t *audio_src;
esp_capture_path_if_t *path_if;

// render handlers
audio_render_handle_t audio_render;
av_render_handle_t player_handle;

static int media_provider_capture_init()
{
    audio_encoder = esp_capture_new_audio_encoder();
    if (audio_encoder == NULL)
    {
        ESP_LOGE(TAG, "Failed to create audio encoder");
        return -1;
    }

    esp_capture_audio_codec_src_cfg_t audio_codec_cfg = {
        .record_handle = get_record_handle(),
    };
    audio_src = esp_capture_new_audio_codec_src(&audio_codec_cfg);
    if (audio_src == NULL)
    {
        ESP_LOGE(TAG, "Failed to create audio codec source");
        return -1;
    }

    esp_capture_simple_path_cfg_t simple_cfg = {
        .aenc = audio_encoder,
    };
    path_if = esp_capture_build_simple_path(&simple_cfg);
    if (path_if == NULL)
    {
        ESP_LOGE(TAG, "Failed to create capture path");
        return -1;
    }

    esp_capture_cfg_t cfg = {
        .sync_mode = ESP_CAPTURE_SYNC_MODE_AUDIO,
        .audio_src = audio_src,
        .capture_path = path_if,
    };
    int ret = esp_capture_open(&cfg, &capture_handle);
    if (ret != ESP_CAPTURE_ERR_OK)
    {
        ESP_LOGE(TAG, "Failed to open capture %d", ret);
        return -1;
    }

    return 0;
}

static int player_event_callback(av_render_event_t event, void *ctx)
{
    ESP_LOGI(TAG, "Render event: %d", event);
    return 0;
}

static int player_callback(uint8_t *data, int size, void *ctx)
{
    // ESP_LOGI(TAG, "Render data size: %d", size);
    return 0;
}

int media_provider_player_init()
{
    esp_codec_dev_handle_t codec_handle = get_playback_handle();
    i2s_render_cfg_t i2s_cfg = {
        .play_handle = codec_handle,
        .cb = player_callback,
    };

    esp_codec_dev_set_out_vol(codec_handle, 100);

    audio_render = av_render_alloc_i2s_render(&i2s_cfg);
    if (audio_render == NULL)
    {
        ESP_LOGE(TAG, "Fail to create audio render");
        return -1;
    }

    av_render_cfg_t render_cfg = {
        .audio_render = audio_render,
        .audio_raw_fifo_size = 1 * 4096,
        .audio_render_fifo_size = 16 * 1024,
        .allow_drop_data = false,
        .pause_on_first_frame = false,
    };
    player_handle = av_render_open(&render_cfg);
    if (player_handle == NULL)
    {
        ESP_LOGE(TAG, "Fail to create player");
        return -1;
    }

    av_render_set_event_cb(player_handle, player_event_callback, NULL);
    av_render_audio_info_t render_aud_info = {
        .codec = AV_RENDER_AUDIO_CODEC_G711A,
        .sample_rate = 8000,
        .channel = 1,
        .bits_per_sample = 16,
    };
    av_render_add_audio_stream(player_handle, &render_aud_info);

    return 0;
}

int media_provider_init()
{
    set_codec_board_type("Interfone");

    codec_init_cfg_t codec_cfg = {
        .reuse_dev = false,
        .in_mode = CODEC_I2S_MODE_TDM,
        .in_use_tdm = true,
    };
    init_codec(&codec_cfg);

    esp_audio_dec_register_default();
    esp_audio_enc_register_default();

    int ret = 0;
    ret = media_provider_capture_init();
    if (ret < 0)
    {
        return ret;
    }

    ret = media_provider_player_init();
    return ret;
}

int media_provider_get(esp_webrtc_media_provider_t *provider)
{
    provider->capture = capture_handle;
    provider->player = player_handle;

    return 0;
}

int test_capture_to_player(void)
{
    esp_capture_sink_cfg_t sink_cfg = {
        .audio_info = {
            .codec = ESP_CAPTURE_CODEC_TYPE_G711A,
            .sample_rate = 8000,
            .channel = 1,
            .bits_per_sample = 16,
        },
    };
    // Create capture
    esp_capture_path_handle_t capture_path = NULL;
    esp_capture_setup_path(capture_handle, ESP_CAPTURE_PATH_PRIMARY, &sink_cfg, &capture_path);
    esp_capture_enable_path(capture_path, ESP_CAPTURE_RUN_TYPE_ALWAYS);
    // Create player
    av_render_audio_info_t render_aud_info = {
        .codec = AV_RENDER_AUDIO_CODEC_G711A,
        .sample_rate = 8000,
        .channel = 1,
    };
    av_render_add_audio_stream(player_handle, &render_aud_info);

    uint32_t start_time = (uint32_t)(esp_timer_get_time() / 1000);
    esp_capture_start(capture_handle);
    // while ((uint32_t)(esp_timer_get_time() / 1000) < start_time + 2000)
    while (1)
    {
        media_lib_thread_sleep(30);
        esp_capture_stream_frame_t frame = {
            .stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO,
        };
        while (esp_capture_acquire_path_frame(capture_path, &frame, true) == ESP_CAPTURE_ERR_OK)
        {
            av_render_audio_data_t audio_data = {
                .data = frame.data,
                .size = frame.size,
                .pts = frame.pts,
            };
            av_render_add_audio_data(player_handle, &audio_data);
            esp_capture_release_path_frame(capture_path, &frame);
        }
    }
    esp_capture_stop(capture_handle);
    av_render_reset(player_handle);
    return 0;
}

void media_provider_render_query()
{
    if (player_handle != NULL)
    {
        av_render_query(player_handle);
    }
}