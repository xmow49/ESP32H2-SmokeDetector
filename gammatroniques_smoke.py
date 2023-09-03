"""Quirk for ZLinky_TIC."""
from copy import deepcopy

from zigpy.profiles import zgp, zha
from zigpy.quirks import CustomCluster, CustomDevice
import zigpy.types as t
from zigpy.zcl.clusters.general import (
    Basic,
    GreenPowerProxy,
    Identify,
    Ota,
    PowerConfiguration,
)
from zigpy.zcl.clusters.security import IasZone
from zigpy.zcl.clusters.homeautomation import ElectricalMeasurement, MeterIdentification
from zigpy.zcl.clusters.manufacturer_specific import ManufacturerSpecificCluster
from zigpy.zcl.clusters.smartenergy import Metering

from zhaquirks.const import (
    DEVICE_TYPE,
    ENDPOINTS,
    NODE_DESCRIPTOR,
    INPUT_CLUSTERS,
    MODELS_INFO,
    OUTPUT_CLUSTERS,
    PROFILE_ID,
)
import zigpy.zdo.types

class Smoke(CustomDevice):
    """ZLinky_TIC from LiXee."""
    signature = {
        MODELS_INFO: [("GammaTroniques", "Smoke Detector")],
        ENDPOINTS: {
            10: {
                PROFILE_ID: zha.PROFILE_ID,
                DEVICE_TYPE: zha.DeviceType.IAS_ZONE,
                INPUT_CLUSTERS: [
                    Basic.cluster_id,
                    PowerConfiguration.cluster_id,
                    IasZone.cluster_id
                ],
            },
        },
    }
    replacement = {
        NODE_DESCRIPTOR: zigpy.zdo.types.NodeDescriptor(
            logical_type=2,
            complex_descriptor_available=0,
            user_descriptor_available=0,
            reserved=0,
            aps_flags=0,
            frequency_band=8,
            mac_capability_flags=132 & 0b1111_1011,
            manufacturer_code=4627,
            maximum_buffer_size=64,
            maximum_incoming_transfer_size=0,
            server_mask=0,
            maximum_outgoing_transfer_size=0,
            descriptor_capability_field=3,
        ),
        ENDPOINTS: {
            10: {
                PROFILE_ID: zha.PROFILE_ID,
                DEVICE_TYPE: zha.DeviceType.IAS_ZONE ,
                INPUT_CLUSTERS: [
                    Basic.cluster_id,
                    PowerConfiguration.cluster_id,
                    IasZone.cluster_id
                ],
            },
        },
    }
