
class MobileAdapterDeviceData:
    mobile_adapter_device_types = {
        "BLUE": 8,
        "YELLOW": 9,
        "GREEN": 10,
        "RED": 11,
        "PURPLE": 12,
        "BLACK": 13,
        "PINK": 14,
        "GREY": 15
    }

    mobile_adapter_device_reverse_types = dict()
    
    for key in mobile_adapter_device_types.keys():
        mobile_adapter_device_reverse_types[mobile_adapter_device_types[key]] = key
