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
/**
 * @note Make sure set idf.py menuconfig in zigbee component as zigbee end device!
 */
#if !defined ZB_ED_ROLE
#error Define ZB_ED_ROLE in idf.py menuconfig to compile light (End Device) source code.
#endif

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
void smoke_detector_main()
{
    RTC_DATA_ATTR static uint8_t state = 0;
    sendSmoke(state);
    state = !state;
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    // 1h sleep
    time_t sleep_time = (time_t)3600 * (time_t)1000000;
    // start deep sleep
    ESP_LOGI(TAG, "Enabling timer wakeup, %llds", sleep_time / 1000000);
    esp_sleep_enable_ext1_wakeup(GPIO_NUM_2, ESP_EXT1_WAKEUP_ANY_HIGH);
    esp_sleep_enable_timer_wakeup(sleep_time);
    esp_deep_sleep_start();
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
        if (err_status == ESP_OK)
        {
            ESP_LOGI(TAG, "Start network steering");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        }
        else
        {
            /* commissioning failed */
            ESP_LOGW(TAG, "Failed to initialize Zigbee stack (status: %d)", err_status);
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
            smoke_detector_main();
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

uint32_t test_attr = 55;
uint32_t test_attr2 = 3;

uint32_t ZCLVersion = 0x0003;
uint32_t ApplicationVersion = 0x0001;
uint32_t StackVersion = 0x0002;
uint32_t HWVersion = 0x0002;
uint8_t ManufacturerName[] = {14, 'G', 'a', 'm', 'm', 'a', 'T', 'r', 'o', 'n', 'i', 'q', 'u', 'e', 's'};
uint8_t ModelIdentifier[] = {14, 'S', 'm', 'o', 'k', 'e', ' ', 'D', 'e', 't', 'e', 'c', 't', 'o', 'r'};
uint8_t DateCode[] = {8, '2', '0', '2', '3', '0', '5', '2', '4'};
uint32_t PowerSource = 0x03; // Mains (single phase)
uint32_t SWBuildID = 0x01;

esp_zb_attribute_list_t *esp_zb_smoke_cluster;

static void esp_zb_task(void *pvParameters)
{

    /* initialize Zigbee stack with Zigbee end-device config */
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    esp_zb_init(&zb_nwk_cfg);
    //------------------------------------------ Attribute ------------------------------------------------

    //***********************BASIC CLUSTER***************************
    /* basic cluster create with fully customized */
    esp_zb_basic_cluster_cfg_t basic_cluster_cfg = {
        .power_source = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .zcl_version = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DEFAULT_VALUE,
    };

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
    esp_zb_smoke_cluster = esp_zb_ias_zone_cluster_create(&smoke_cfg);

    //***********************BATTEY CLUSTER***************************
    esp_zb_power_config_cluster_cfg_t power_cfg = {0};
    uint8_t batteryVoltage = 50;
    uint8_t batteryPercentageRemaining = 0x80;
    uint8_t batteryQuantity = 1;
    uint8_t batterySize = 0xff;
    esp_zb_attribute_list_t *esp_zb_power_cluster = esp_zb_power_config_cluster_create(&power_cfg);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID, &batteryPercentageRemaining);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, &batteryVoltage);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_QUANTITY_ID, &batteryQuantity);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_SIZE_ID, &batterySize);

    //------------------------------------------ Cluster ------------------------------------------------

    /* create cluster lists for this endpoint */
    esp_zb_cluster_list_t *esp_zb_cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(esp_zb_cluster_list, esp_zb_basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_ias_zone_cluster(esp_zb_cluster_list, esp_zb_smoke_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_power_config_cluster(esp_zb_cluster_list, esp_zb_power_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    //------------------------------------------ Endpoint ------------------------------------------------
    esp_zb_ep_list_t *esp_zb_ep_list = esp_zb_ep_list_create();
    esp_zb_ep_list_add_ep(esp_zb_ep_list, esp_zb_cluster_list, HA_ESP_SMOKE_ENDPOINT, ESP_ZB_AF_HA_PROFILE_ID, ESP_ZB_HA_ON_OFF_OUTPUT_DEVICE_ID);
    esp_zb_device_register(esp_zb_ep_list);

    //------------------------------------------ Callback ------------------------------------------------
    esp_zb_device_add_set_attr_value_cb(attr_cb);
    esp_zb_device_add_report_attr_cb(attr_cb);

    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    esp_zb_set_secondary_network_channel_set(0x07FFF800);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_main_loop_iteration();
}

void buttonLoop(void *pvParameter)
{
    uint16_t lastState = 0;
    while (1)
    {
        if (gpio_get_level(GPIO_NUM_9) != lastState)
        {
            lastState = gpio_get_level(GPIO_NUM_9);
            // if (lastState == 0)
            // {
            //     // report smoke
            //     printf("Button pressed\n");
            //     *((uint16_t *)esp_zb_zcl_get_attribute(HA_ESP_SMOKE_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_IAS_ZONE, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_IAS_ZONE_ZONESTATUS_ID)->data_p) |= ESP_ZB_ZCL_IAS_ZONE_ZONE_STATUS_ALARM1;
            // }
            // else
            // {
            //     *((uint16_t *)esp_zb_zcl_get_attribute(HA_ESP_SMOKE_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_IAS_ZONE, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_IAS_ZONE_ZONESTATUS_ID)->data_p) &= ~ESP_ZB_ZCL_IAS_ZONE_ZONE_STATUS_ALARM1;
            //     printf("Button released\n");
            // }
            // esp_zb_zcl_ias_zone_status_change_notif_cmd_t cmd = {
            //     .zcl_basic_cmd = {
            //         .dst_addr_u.addr_short = 0x0000,
            //         .dst_endpoint = HA_ESP_SMOKE_ENDPOINT,
            //         .src_endpoint = HA_ESP_SMOKE_ENDPOINT,
            //     },
            //     .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
            //     .zone_status = ESP_ZB_ZCL_IAS_ZONE_ZONE_STATUS_ALARM1 ? !lastState : 0,
            //     .zone_id = 0,
            //     .delay = 0,
            // };

            // esp_zb_zcl_ias_zone_status_change_notif_cmd_req(&cmd);
            sendSmoke(lastState);
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(nvs_flash_init());
    /* load Zigbee light_bulb platform config to initialization */
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
    // xTaskCreate(buttonLoop, "buttonLoop", 4096, NULL, 5, NULL);
}
