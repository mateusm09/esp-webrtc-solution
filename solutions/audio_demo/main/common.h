/* Door Bell Demo

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include "settings.h"
#include "media_sys.h"
#include "network.h"
#include "sys_state.h"
#include "esp_webrtc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define RELAY_1 GPIO_NUM_13
#define RELAY_2 GPIO_NUM_14

    /**
     * @brief  Start WebRTC
     *
     * @param[in]  url  Signaling URL
     *
     * @return
     *      - 0       On success
     *      - Others  Fail to start
     */
    int start_webrtc(char *url, esp_mqtt_client_handle_t handle);

    /**
     * @brief  Query WebRTC status
     */
    int query_webrtc(void);
    esp_webrtc_handle_t getWebrtc(void);
    void outCall();
    void inCall();

    /**
     * @brief  Stop WebRTC
     *
     * @return
     *      - 0       On success
     *      - Others  Fail to stop
     */
    int stop_webrtc(void);

    int media_provider_get(esp_webrtc_media_provider_t *provider);

    int media_provider_init();

    /**
     * @brief  Play captured media directly
     *
     * @return
     *      - 0       On success
     *      - Others  Fail to capture or play
     */
    int test_capture_to_player(void);

    void media_provider_render_query(void);
#ifdef __cplusplus
}
#endif
