#include "zigbee.h"
#include "smoke_detector.h"

#define TAG "ZIGBEE"

void reportAttribute(uint8_t endpoint, uint16_t clusterID, uint16_t attributeID, void *value, uint8_t value_length)
{
    esp_zb_zcl_report_attr_cmd_t cmd = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = 0x0000,
            .dst_endpoint = endpoint,
            .src_endpoint = endpoint,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .clusterID = clusterID,
        .attributeID = attributeID,
        .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
    };
    esp_zb_zcl_attr_t *value_r = esp_zb_zcl_get_attribute(endpoint, clusterID, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, attributeID);
    memcpy(value_r->data_p, value, value_length);
    esp_zb_zcl_report_attr_cmd_req(&cmd);
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
            esp_restart();
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
#ifdef DEBUG_SLEEP
            xTaskCreate(debugLoop, "debugLoop", 4096, NULL, 5, NULL);
#endif
        }
        else
        {
            ESP_LOGI(TAG, "Network steering was not successful (status: %d)", err_status);
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    default:
        break;
    }
}

void esp_zb_task(void *pvParameters)
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
    uint8_t batteryRatedVoltage = 90;
    uint8_t batteryMinVoltage = 70;
    uint8_t batteryQuantity = 1;
    uint8_t batterySize = 0x02;
    uint16_t batteryAhrRating = 50000;
    uint8_t batteryAlarmMask = 0;
    uint8_t batteryVoltage = 90;
    esp_zb_attribute_list_t *esp_zb_power_cluster = esp_zb_power_config_cluster_create(&power_cfg);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, &batteryVoltage);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_SIZE_ID, &batterySize);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_QUANTITY_ID, &batteryQuantity);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_RATED_VOLTAGE_ID, &batteryRatedVoltage);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_ALARM_MASK_ID, &batteryAlarmMask);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_MIN_THRESHOLD_ID, &batteryMinVoltage);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_A_HR_RATING_ID, &batteryAhrRating);
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID, &lastBatteryPercentageRemaining);
    // remove 8 first cluster: shift the pointer to the 8th cluster
    for (int i = 0; i < 7; i++)
    {
        esp_zb_power_cluster = esp_zb_power_cluster->next;
    }
    //------------------------------------------ Cluster ------------------------------------------------

    /* create cluster lists for this endpoint */
    esp_zb_cluster_list_t *esp_zb_cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(esp_zb_cluster_list, esp_zb_basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_power_config_cluster(esp_zb_cluster_list, esp_zb_power_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_ias_zone_cluster(esp_zb_cluster_list, esp_zb_smoke_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    //------------------------------------------ Endpoint ------------------------------------------------
    esp_zb_ep_list_t *esp_zb_ep_list = esp_zb_ep_list_create();
    esp_zb_ep_list_add_ep(esp_zb_ep_list, esp_zb_cluster_list, HA_ESP_SMOKE_ENDPOINT, ESP_ZB_AF_HA_PROFILE_ID, ESP_ZB_HA_IAS_ZONE_ID);
    esp_zb_device_register(esp_zb_ep_list);

    //------------------------------------------ Callback ------------------------------------------------
    esp_zb_device_add_set_attr_value_cb(attr_cb);

    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    esp_zb_set_secondary_network_channel_set(0x07FFF800);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_main_loop_iteration();
}

void attr_cb(uint8_t status, uint8_t endpoint, uint16_t cluster_id, uint16_t attr_id, void *new_value)
{
    ESP_LOGI(TAG, "cluster:0x%x, attribute:0x%x changed ", cluster_id, attr_id);
}

void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(mode_mask));
}