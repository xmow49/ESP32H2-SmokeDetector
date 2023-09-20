#ifndef ZIGBEE_H
#define ZIGBEE_H

#define MILLIS xTaskGetTickCount() * portTICK_PERIOD_MS

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "ha/zb_ha_device_config.h"
#include "zcl/esp_zigbee_zcl_power_config.h"
#include <string.h>
#include "esp_zigbee_core.h"
#include "esp_sleep.h"

/* Zigbee configuration */
#define INSTALLCODE_POLICY_ENABLE false /* enable the install code policy for security */
#define ED_AGING_TIMEOUT ESP_ZB_ED_AGING_TIMEOUT_64MIN
#define ED_KEEP_ALIVE 3000                     /* 3000 millisecond */
#define HA_ESP_SMOKE_ENDPOINT 10               /* esp light bulb device endpoint, used to process light controlling commands */
#define ESP_ZB_PRIMARY_CHANNEL_MASK (1l << 15) /* Zigbee primary channel mask use in the example */

#define ESP_ZB_ZED_CONFIG()                               \
    {                                                     \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,             \
        .install_code_policy = INSTALLCODE_POLICY_ENABLE, \
        .nwk_cfg.zed_cfg = {                              \
            .ed_timeout = ED_AGING_TIMEOUT,               \
            .keep_alive = ED_KEEP_ALIVE,                  \
        },                                                \
    }

#define ESP_ZB_DEFAULT_RADIO_CONFIG()    \
    {                                    \
        .radio_mode = RADIO_MODE_NATIVE, \
    }

#define ESP_ZB_DEFAULT_HOST_CONFIG()                       \
    {                                                      \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE, \
    }

void reportAttribute(uint8_t endpoint, uint16_t clusterID, uint16_t attributeID, void *value, uint8_t value_length);
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct);
void esp_zb_task(void *pvParameters);
static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message);
void bdb_start_top_level_commissioning_cb(uint8_t mode_mask);
static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message);

#endif