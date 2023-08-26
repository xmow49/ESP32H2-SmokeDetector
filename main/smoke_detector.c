#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "ha/zb_ha_device_config.h"
#include "zcl/esp_zigbee_zcl_power_config.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <string.h>
// 37uA
#include "smoke_detector.h"
#include "zigbee.h"

#define TAG "MAIN"

/********************* Define functions **************************/
RTC_DATA_ATTR uint8_t lastBatteryPercentageRemaining = 0x8C;

void updateBattery(void)
{
    // gpio 4
    esp_log_level_set("gpio", ESP_LOG_ERROR);
    adc_oneshot_unit_init_cfg_t init_config1 = {};
    adc_oneshot_unit_handle_t adc1_handle;
    init_config1.unit_id = ADC_UNIT_1;

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    int raw = 0;
    uint8_t max = 0;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_3, &config));
    do
    {
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_3, &raw));
    } while ((raw == 0) && max++ < 10);
    adc_oneshot_del_unit(adc1_handle);

    int tempCal = raw - ADC_RAW_OFFSET;
    // max 1.8 4081
    // 1M / 11M
    // 0 -> 0v
    // 1793 --> 1.8V
    // float vADC = (float)tempCal * 1.8 / (4081 - OFFSET);
    // float vIN = (vADC * 11) + 1; //+1 calibration

#ifdef DEBUG_SLEEP
    ESP_LOGI(TAG, "Battery Raw: %d, TempCal: %d", raw, tempCal);
#endif

    float vIN = (ADC_EQUATION_COEF * tempCal) + ADC_EQUATION_OFFSET;
    // ESP_LOGI(TAG, "Battery Raw: %d, TempCal: %d, vIN: %f", raw, tempCal, vIN);
    // ------- percentage ----------
    // 0% 7v
    // 100% 9.5V

    uint8_t percentage = 0;
    if (vIN < 7)
    {
        percentage = 0;
    }
    else if (vIN > 9.5)
    {
        percentage = 100;
    }
    else
    {
        percentage = (uint8_t)((vIN - 7) * 100 / 2.5);
    }
    ESP_LOGI(TAG, "vIN: %f, percentage: %d", vIN, percentage);
    percentage = percentage * 2; // zigbee scale
    lastBatteryPercentageRemaining = percentage;
    reportAttribute(HA_ESP_SMOKE_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID, &percentage, 1);
    return;
}

void debugLoop(void *arg)
{
    while (1)
    {
        updateBattery();
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

void sendSmoke(uint8_t smoke)
{

    esp_zb_zcl_ias_zone_status_change_notif_cmd_t cmd = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = 0x0000,
            .dst_endpoint = HA_ESP_SMOKE_ENDPOINT,
            .src_endpoint = HA_ESP_SMOKE_ENDPOINT,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .zone_status = ESP_ZB_ZCL_IAS_ZONE_ZONE_STATUS_ALARM1 ? smoke : 0,
        .zone_id = 0,
        .delay = 0,
    };
    esp_zb_zcl_ias_zone_status_change_notif_cmd_req(&cmd);
    updateBattery();
}

void setup_sleep(time_t time_s, uint8_t gpio_en)
{
    time_t sleep_time = time_s * (time_t)1000000;
    esp_sleep_enable_timer_wakeup(sleep_time);
    if (gpio_en)
    {
        ESP_LOGI(TAG, "Enabling timer wakeup, %llds and GPIO wake", time_s);
        esp_sleep_enable_ext1_wakeup_with_level_mask((1ULL << SMOKE_PIN), (1ULL << SMOKE_PIN));
    }
    else
    {
        ESP_LOGI(TAG, "Enabling timer wakeup, %llds", time_s);
    }
}

void smoke_detector_main(void *arg)
{
    while (1)
    {

        RTC_DATA_ATTR static uint8_t state = 0;
        esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
        switch (wakeup_reason)
        {
        case ESP_SLEEP_WAKEUP_EXT1:
            if (!state)
            {
                ESP_LOGI(TAG, "Alert! Smoke detected!");
                state = 1;
                sendSmoke(state);
            }
            else
            {
                ESP_LOGI(TAG, "Alert already sent!");
            }
            // recheck after 30s without gpio
            setup_sleep(TIME_SMOKE_CHECK, 0);
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
        {
            ESP_LOGI(TAG, "Wake up from timer");
            if (state)
            {
                // check any HIGH signal from GPIO8 with a timeout of 1s
                for (int i = 0; i < 200; i++)
                {
                    if (gpio_get_level(SMOKE_PIN))
                    {
                        ESP_LOGI(TAG, "Smoke Still detected!");
                        setup_sleep(TIME_SMOKE_CHECK, 0);
                        esp_deep_sleep_start();
                        break;
                    }
                    vTaskDelay(1 / portTICK_PERIOD_MS);
                }
                ESP_LOGI(TAG, "Reset state");
                state = 0;
                sendSmoke(state);
            }
            // 1h sleep
            setup_sleep(TIME_BATTERY_UPDATE, 1);
            break;
        }
        default:
        {
            uint64_t pin = esp_sleep_get_ext1_wakeup_status();
            ESP_LOGI(TAG, "Wake up reason: %d, pin: %lld", wakeup_reason, pin);
            updateBattery();
            sendSmoke(0);
            setup_sleep(TIME_BATTERY_UPDATE, 1);
            break;
        }
        }
#ifndef DEBUG_SLEEP
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_deep_sleep_start();
#endif
    }
}

void sleep_watchdog(void *arg)
{
    while (1)
    {
        if (MILLIS > 5000)
        {
            ESP_LOGI(TAG, "Sleep watchdog timeout");
            setup_sleep(TIME_SMOKE_CHECK, 0); // force sleep
            esp_deep_sleep_start();
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
void app_main(void)
{
    // set gpio 4 as input
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_NUM_4); // ADC1_CHANNEL_3 --> VIN
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

#ifndef DEBUG_SLEEP
    xTaskCreate(sleep_watchdog, "sleep_watchdog", 4096, NULL, 5, NULL);
#endif

    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
