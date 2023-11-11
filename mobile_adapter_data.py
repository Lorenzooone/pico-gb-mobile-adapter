
class MobileAdapterDeviceData:
    pdc_name = "BLUE"
    cmdaone_name = "YELLOW"
    phsnnt_name = "GREEN"
    ddi_name = "RED"
    
    released_adapters = set([
        pdc_name,
        cmdaone_name,
        ddi_name
    ])
    
    mobile_adapter_device_types = {
        pdc_name: 8,
        cmdaone_name: 9,
        phsnnt_name: 10,
        ddi_name: 11,
        "PURPLE": 12,
        "BLACK": 13,
        "PINK": 14,
        "GREY": 15
    }

    mobile_adapter_device_reverse_types = dict()
    
    for key in mobile_adapter_device_types.keys():
        mobile_adapter_device_reverse_types[mobile_adapter_device_types[key]] = key
