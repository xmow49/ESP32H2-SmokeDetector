#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "ha/zb_ha_device_config.h"
#include "zcl/esp_zigbee_zcl_power_config.h"

#include "smoke_detector.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_sleep.h"

// 37uA

#define DEBUG_SLEEP

#define MILLIS xTaskGetTickCount() * portTICK_PERIOD_MS
static const char *TAG = "ESP_ZB_ON_OFF_LIGHT";

/********************* Define functions **************************/
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
}

void setup_sleep(time_t time_s, uint8_t gpio_en)
{
    time_t sleep_time = time_s * (time_t)1000000;
    esp_sleep_enable_timer_wakeup(sleep_time);
    if (gpio_en)
    {
        ESP_LOGI(TAG, "Enabling timer wakeup, %llds and GPIO wake", time_s);
        esp_sleep_enable_ext1_wakeup_with_level_mask((1ULL << GPIO_NUM_8), (1ULL << GPIO_NUM_8));
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
            setup_sleep(30, 0);
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
        {
            ESP_LOGI(TAG, "Wake up from timer");
            if (state)
            {
                // check any HIGH signal from GPIO8 with a timeout of 1s
                for (int i = 0; i < 200; i++)
                {
                    ESP_LOGI(TAG, "GPIO8: %d", gpio_get_level(GPIO_NUM_8));
                    if (gpio_get_level(GPIO_NUM_8))
                    {
                        ESP_LOGI(TAG, "Smoke Still detected!");
                        setup_sleep(30, 0);
                        esp_deep_sleep_start();
                        break;
                    }
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                }
                ESP_LOGI(TAG, "Reset state");
                state = 0;
                sendSmoke(state);
            }
            // 1h sleep
            setup_sleep(3600, 1);
            break;
        }
        default:
        {
            ESP_LOGI(TAG, "Wake up reason: %d", wakeup_reason);
            setup_sleep(3600, 1);
            break;
        }
        }
#ifndef DEBUG_SLEEP
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        esp_deep_sleep_start();
#endif
    }
}

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(mode_mask));
}

void attr_cb(uint8_t status, uint8_t endpoint, uint16_t cluster_id, uint16_t attr_id, void *new_value)
{
    ESP_LOGI(TAG, "cluster:0x%x, attribute:0x%x changed ", cluster_id, attr_id);
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;

    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    switch (sig_type)
    {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Zigbee stack initialized");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        ESP_LOGI(TAG, "signal: %d, status: %d", sig_type, err_status);
        if (err_status == ESP_OK)
        {
            ESP_LOGI(TAG, "Start network steering");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        }
        else
        {
            /* commissioning failed */
            ESP_LOGW(TAG, "Failed to initialize Zigbee stack (status: %d)", err_status);
#ifndef DEBUG_SLEEP
            setup_sleep(10, 0); // reset after 10s
            esp_deep_sleep_start();
#endif
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK)
        {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel());
#ifndef DEBUG_SLEEP
            xTaskCreate(smoke_detector_main, "smoke_detector_main", 4096, NULL, 5, NULL);
#endif
        }
        else
        {
            ESP_LOGI(TAG, "Network steering was not successful (status: %d)", err_status);
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal: %d, status: %d", sig_type, err_status);
        break;
    }
}

static void esp_zb_task(void *pvParameters)
{

    /* initialize Zigbee stack with Zigbee end-device config */
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    esp_zb_init(&zb_nwk_cfg);
    //------------------------------------------ Attribute ------------------------------------------------
    //***********************BASIC CLUSTER***************************
    /* basic cluster create with fully customized */
    esp_zb_basic_cluster_cfg_t basic_cluster_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = 0x03,
    };
    uint32_t ApplicationVersion = 0x0001;
    uint32_t StackVersion = 0x0002;
    uint32_t HWVersion = 0x0002;
    uint8_t ManufacturerName[] = {14, 'G', 'a', 'm', 'm', 'a', 'T', 'r', 'o', 'n', 'i', 'q', 'u', 'e', 's'};
    uint8_t ModelIdentifier[] = {14, 'S', 'm', 'o', 'k', 'e', ' ', 'D', 'e', 't', 'e', 'c', 't', 'o', 'r'};
    uint8_t DateCode[] = {8, '2', '0', '2', '3', '0', '5', '2', '4'};
    uint32_t SWBuildID = 0x01;
    esp_zb_attribute_list_t *esp_zb_basic_cluster = esp_zb_basic_cluster_create(&basic_cluster_cfg);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_APPLICATION_VERSION_ID, &ApplicationVersion);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_STACK_VERSION_ID, &StackVersion);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_HW_VERSION_ID, &HWVersion);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, ManufacturerName);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, ModelIdentifier);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_DATE_CODE_ID, DateCode);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, &SWBuildID);

    //***********************SMOKE CLUSTER***************************
    esp_zb_ias_zone_cluster_cfg_t smoke_cfg = {
        .zone_state = ESP_ZB_ZCL_IAS_ZONE_ZONESTATE_NOT_ENROLLED,
        .zone_type = ESP_ZB_ZCL_IAS_ZONE_ZONETYPE_FIRE_SENSOR,
        .zone_status = 0,
        .ias_cie_addr = ESP_ZB_ZCL_ZONE_IAS_CIE_ADDR_DEFAULT,
        .zone_id = 0,
    };
    esp_zb_attribute_list_t *esp_zb_smoke_cluster = esp_zb_ias_zone_cluster_create(&smoke_cfg);

    //***********************BATTEY CLUSTER***************************
    esp_zb_power_config_cluster_cfg_t power_cfg = {0};
    uint8_t batteryVoltage = 90;
    uint8_t batteryRatedVoltage = 90;
    uint8_t batteryMinVoltage = 70;
    uint8_t batteryPercentageRemaining = 0x64;
    uint8_t batteryQuantity = 1;
    uint8_t batterySize = 0x02;
    uint16_t batteryAhrRating = 50000;
    uint8_t batteryAlarmMask = 0;
    esp_zb_attribute_list_t *esp_zb_power_cluster = esp_zb_power_config_cluster_create(&power_cfg);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, &batteryVoltage);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_SIZE_ID, &batterySize);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_QUANTITY_ID, &batteryQuantity);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_RATED_VOLTAGE_ID, &batteryRatedVoltage);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_ALARM_MASK_ID, &batteryAlarmMask);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_MIN_THRESHOLD_ID, &batteryMinVoltage);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_A_HR_RATING_ID, &batteryAhrRating);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID, &batteryPercentageRemaining);

    //------------------------------------------ Cluster ------------------------------------------------

    /* create cluster lists for this endpoint */
    esp_zb_cluster_list_t *esp_zb_cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(esp_zb_cluster_list, esp_zb_basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_power_config_cluster(esp_zb_cluster_list, esp_zb_power_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_ias_zone_cluster(esp_zb_cluster_list, esp_zb_smoke_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    //------------------------------------------ Endpoint ------------------------------------------------
    esp_zb_ep_list_t *esp_zb_ep_list = esp_zb_ep_list_create();
    esp_zb_ep_list_add_ep(esp_zb_ep_list, esp_zb_cluster_list, HA_ESP_SMOKE_ENDPOINT, ESP_ZB_AF_HA_PROFILE_ID, ESP_ZB_HA_ON_OFF_OUTPUT_DEVICE_ID);
    esp_zb_device_register(esp_zb_ep_list);

    //------------------------------------------ Callback ------------------------------------------------
    esp_zb_device_add_set_attr_value_cb(attr_cb);

    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    esp_zb_set_secondary_network_channel_set(0x07FFF800);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_main_loop_iteration();
}

void sleep_watchdog(void *arg)
{
    while (1)
    {
        if (MILLIS > 5000)
        {
            ESP_LOGI(TAG, "Sleep watchdog timeout");
            setup_sleep(10, 0); // force sleep
            esp_deep_sleep_start();
        }
    }
}

void app_main(void)
{
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
