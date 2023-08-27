import usb.core
import usb.util
import signal
import sys
import traceback
import time
from time import sleep
from gbridge import GBridge, GBridgeSocket, GBridgeDebugCommands
import os

import threading

dev = None
epIn = None
epOut = None
max_usb_timeout = 5

def kill_function():
    os.kill(os.getpid(), signal.SIGINT)

class KeyboardThread(threading.Thread):

    def __init__(self):
        super(KeyboardThread, self).__init__()
        self.received = []
        self.daemon = True
        self.received_lock = threading.Lock()
        self.start()

    def run(self):
        while True:
            read_data = input()
            self.received_lock.acquire()
            self.received += [read_data]
            self.received_lock.release()

    def get_input(self):
        self.received_lock.acquire()
        out = self.received
        self.received = []
        self.received_lock.release()
        return out

class SocketThread(threading.Thread):

    def __init__(self):
        super(SocketThread, self).__init__()
        self.daemon = True
        self.start_processing = False
        self.done_processing = False
        self.bridge = GBridge()
        self.bridge_debug = GBridge()
        self.bridge_sockets = GBridgeSocket()
        self.lock_in = threading.Lock()
        self.lock_out = threading.Lock()
        self.lock_in.acquire()
        self.lock_out.acquire()
        self.start()

    def run(self):
        TRANSFER_FLAGS_MASK = 0xC0
        DEBUG_TRANSFER_FLAG = 0x80
        print_data_in = False
        debug_print = True

        while True:
            self.lock_in.acquire()

            send_list = []
            read_data = self.data
            save_requests = self.save_requests
            num_bytes = int.from_bytes(read_data[:1], byteorder='little')

            curr_bridge = self.bridge
            is_debug = (num_bytes & TRANSFER_FLAGS_MASK) == DEBUG_TRANSFER_FLAG
            if is_debug:
                curr_bridge = self.bridge_debug

            num_bytes &= 0x3F
            bytes = []
            if (num_bytes > 0) and (num_bytes <= (len(read_data) - 1)):
                for i in range(num_bytes):
                    bytes += [int.from_bytes(read_data[(i + 1):(i + 2)], byteorder='little')]

                curr_cmd = True
                if print_data_in and (not is_debug):
                    print("IN: " + str(bytes))
                while curr_cmd is not None:
                    curr_cmd = curr_bridge.init_cmd(bytes)
                    if(curr_cmd is not None):
                        bytes = bytes[curr_cmd.total_len - curr_cmd.old_len:]
                        curr_cmd.check_save(save_requests)
                        if debug_print:
                            curr_cmd.do_print()
                        if(curr_cmd.response_cmd is not None):
                            send_list += [curr_cmd.response_cmd]
                            if(curr_cmd.process(self.bridge_sockets)):
                                send_list += GBridge.prepare_cmd(curr_cmd.result_to_send(), False)
                                send_list += GBridge.prepare_cmd(curr_cmd.get_if_pending(), True)
            
            self.out_data = send_list

            self.lock_out.release()

    def set_processing(self, data, save_requests):
        self.data = data
        self.save_requests = save_requests

        self.lock_in.release()

    def get_processed(self):
        self.lock_out.acquire()

        return self.out_data
    
def interpret_input_keyboard(key_input, debug_send_list, save_requests):

    basic_commands = {
        "GET EEPROM": GBridgeDebugCommands.SEND_EEPROM_CMD,
        "GET STATUS": GBridgeDebugCommands.STATUS_CMD,
        "START ADAPTER": GBridgeDebugCommands.START_CMD,
        "STOP ADAPTER": GBridgeDebugCommands.STOP_CMD,
        "GET NAME": GBridgeDebugCommands.SEND_NAME_INFO_CMD,
        "GET INFO": GBridgeDebugCommands.SEND_OTHER_INFO_CMD,
        "GET NUMBER": GBridgeDebugCommands.SEND_NUMBER_OWN_CMD,
        "GET NUMBER_PEER": GBridgeDebugCommands.SEND_NUMBER_OTHER_CMD,
        "GET RELAY_TOKEN": GBridgeDebugCommands.SEND_RELAY_TOKEN_CMD
    }
    
    mobile_adapter_commands = {
        "SET DEVICE": GBridgeDebugCommands.UPDATE_DEVICE_CMD
    }
    
    unsigned_commands = {
        "SET P2P_PORT": GBridgeDebugCommands.UPDATE_P2P_PORT_CMD
    }
    
    token_commands = {
        "SET RELAY_TOKEN": GBridgeDebugCommands.UPDATE_RELAY_TOKEN_CMD
    }
    
    RELAY_TOKEN_SIZE = 0x10
    
    address_commands = {
        "SET DNS_1": GBridgeDebugCommands.UPDATE_DNS1_CMD,
        "SET DNS_2": GBridgeDebugCommands.UPDATE_DNS2_CMD,
        "SET RELAY": GBridgeDebugCommands.UPDATE_RELAY_CMD,
    }

    path_send_commands = {
        "SAVE EEPROM": GBridgeDebugCommands.SEND_EEPROM_CMD,
        "LOAD EEPROM": GBridgeDebugCommands.UPDATE_EEPROM_CMD
    }

    loading_commands = {
        "LOAD EEPROM"
    }

    saving_commands = {
        "SAVE EEPROM": [GBridge.GBRIDGE_CMD_DEBUG_INFO, GBridgeDebugCommands.CMD_DEBUG_INFO_CFG],
        "SAVE DBG_IN": [GBridge.GBRIDGE_CMD_DEBUG_LOG, GBridgeDebugCommands.CMD_DEBUG_LOG_IN],
        "SAVE DBG_OUT": [GBridge.GBRIDGE_CMD_DEBUG_LOG, GBridgeDebugCommands.CMD_DEBUG_LOG_OUT],
        "SAVE TIME_TR": [GBridge.GBRIDGE_CMD_DEBUG_LOG, GBridgeDebugCommands.CMD_DEBUG_LOG_TIME_TR],
        "SAVE TIME_AC": [GBridge.GBRIDGE_CMD_DEBUG_LOG, GBridgeDebugCommands.CMD_DEBUG_LOG_TIME_AC],
        "SAVE TIME_IR": [GBridge.GBRIDGE_CMD_DEBUG_LOG, GBridgeDebugCommands.CMD_DEBUG_LOG_TIME_IR]
    }
    
    mobile_adapter_types = {
        "BLUE": 8,
        "YELLOW": 9,
        "GREEN": 10,
        "RED": 11
    }

    for elem in key_input.get_input():
        tokens = elem.split()
        command = ""
        if(len(tokens) >= 2):
            command = tokens[0].upper().strip() + " " + tokens[1].upper().strip()

        if command in basic_commands.keys():
            result, ack_wanted = GBridgeDebugCommands.load_command(basic_commands[command], None)
            debug_send_list += result
        
        if command in saving_commands.keys():
            if command in path_send_commands.keys():
                result, ack_wanted = GBridgeDebugCommands.load_command(path_send_commands[command], None)
                debug_send_list += result

            if len(tokens) > 2:
                save_path = tokens[2].strip()
                if saving_commands[command][0] not in save_requests.keys():
                    save_requests[saving_commands[command][0]] = dict()
                save_requests[saving_commands[command][0]][saving_commands[command][1]] = save_path
        
        if command in loading_commands:
            if len(tokens) > 2:
                load_path = tokens[2].strip()
                data = None
                with open(load_path, mode='rb') as file_read:
                    data = file_read.read()
                if data is not None:
                    if command in path_send_commands.keys():
                        result, ack_wanted = GBridgeDebugCommands.load_command(path_send_commands[command], data)
                        debug_send_list += result

        if command in address_commands:
            if len(tokens) > 2:
                mobile_type = tokens[2].upper().strip()
                metered = True
                if (len(tokens) > 3) and tokens[3] == "UNMETERED":
                    metered = False
                if mobile_type in mobile_adapter_types.keys():
                    data = mobile_adapter_types[mobile_type]
                    if not metered:
                        data |= 0x80
                    result, ack_wanted = GBridgeDebugCommands.load_command(mobile_adapter_commands[command], data)
                    debug_send_list += result

        if command in unsigned_commands:
            if len(tokens) > 2:
                value = GBridgeSocket.parse_unsigned(tokens[2])
                if value is not None:
                    result, ack_wanted = GBridgeDebugCommands.load_command(unsigned_commands[command], value)
                    debug_send_list += result

        if command in address_commands:
            if len(tokens) > 2:
                data = GBridgeSocket.parse_addr(tokens[2:])
                if data is not None:
                    result, ack_wanted = GBridgeDebugCommands.load_command(address_commands[command], data)
                    debug_send_list += result

        if command in token_commands:
            if len(tokens) > 2:
                data = []
                try:
                    data = list(bytes.fromhex(tokens[2].upper().strip()))
                except:
                    pass
                if len(data) == RELAY_TOKEN_SIZE:
                    result, ack_wanted = GBridgeDebugCommands.load_command(token_commands[command], [1] + data)
                    debug_send_list += result
                elif tokens[2].upper().strip() == "NULL":
                    result, ack_wanted = GBridgeDebugCommands.load_command(token_commands[command], [0])
                    debug_send_list += result

def prepare_out_func(analyzed_list, is_debug_cmd):
    DEBUG_CMD_TRANSFER_FLAG = 0xC0
    print_data_out = False
    limit = 0x40 - 1
    num_elems = 0
    out_buf = []

    if len(analyzed_list) == 0:
        out_buf += [num_elems]
    else:
        num_elems = len(analyzed_list)
        if(num_elems > limit):
            num_elems = limit
        out_val_elems = num_elems
        if is_debug_cmd:
            out_val_elems |= DEBUG_CMD_TRANSFER_FLAG
        out_buf += out_val_elems.to_bytes(1, byteorder='little')
        for i in range(num_elems):
            out_buf += analyzed_list[i].to_bytes(1, byteorder='little')
        if print_data_out:
            print("OUT: " + str(out_buf[1:]))
    return out_buf, num_elems

def transfer_func(sender, receiver, list_sender, raw_receiver):
    key_input = KeyboardThread()
    out_data_preparer = SocketThread()
    send_list = []
    debug_send_list = []
    save_requests = dict()
    while(1):
        interpret_input_keyboard(key_input, debug_send_list, save_requests)

        if len(send_list) == 0:
            out_buf, num_elems = prepare_out_func(debug_send_list, True)
            debug_send_list = debug_send_list[num_elems:]
        else:
            out_buf, num_elems = prepare_out_func(send_list, False)
            send_list = send_list[num_elems:]
        list_sender(out_buf, chunk_size = len(out_buf))

        out_data_preparer.set_processing(raw_receiver(0x40), save_requests)
        send_list += out_data_preparer.get_processed()
        sleep(0.01)

# Code dependant on this connection method
def sendByte(byte_to_send, num_bytes):
    if(epOut is None):
        exit_no_device()
    epOut.write(byte_to_send.to_bytes(num_bytes, byteorder='big'), timeout=max_usb_timeout * 1000)
    return

# Code dependant on this connection method
def sendList(data, chunk_size=8):
    if(epOut is None):
        exit_no_device()
    num_iters = int(len(data)/chunk_size)
    for i in range(num_iters):
        epOut.write(data[i*chunk_size:(i+1)*chunk_size], timeout=max_usb_timeout * 1000)
    #print(num_iters*chunk_size)
    #print(len(data))
    if (num_iters*chunk_size) != len(data):
        epOut.write(data[num_iters*chunk_size:], timeout=max_usb_timeout * 1000)
        

def receiveByte(num_bytes):
    if(epIn is None):
        exit_no_device()
    recv = int.from_bytes(epIn.read(num_bytes, timeout=max_usb_timeout * 1000), byteorder='big')
    return recv

def receiveByte_raw(num_bytes):
    if(epIn is None):
        exit_no_device()
    return epIn.read(num_bytes, timeout=max_usb_timeout * 1000)

# Code dependant on this connection method
def sendByte_win(byte_to_send, num_bytes):
    p.write(byte_to_send.to_bytes(num_bytes, byteorder='big'), timeout=max_usb_timeout)

# Code dependant on this connection method
def sendList_win(data, chunk_size=8):
    num_iters = int(len(data)/chunk_size)
    for i in range(num_iters):
        p.write(bytes(data[i*chunk_size:(i+1)*chunk_size]), timeout=max_usb_timeout)
    #print(num_iters*chunk_size)
    #print(len(data))
    if (num_iters*chunk_size) != len(data):
        p.write(bytes(data[num_iters*chunk_size:]), timeout=max_usb_timeout)

def receiveByte_win(num_bytes):
    recv = int.from_bytes(p.read(size=num_bytes, timeout=max_usb_timeout), byteorder='big')
    return recv

def receiveByte_raw_win(num_bytes):
    return p.read(size=num_bytes, timeout=max_usb_timeout)

# Things for the USB connection part
def exit_gracefully():
    if dev is not None:
        usb.util.dispose_resources(dev)
        if(os.name != "nt"):
            if reattach:
                dev.attach_kernel_driver(0)
    os._exit(1)

def exit_no_device():
    print("Device not found!")
    exit_gracefully()

def signal_handler(sig, frame):
    print("You pressed Ctrl+C!")
    exit_gracefully()

signal.signal(signal.SIGINT, signal_handler)

# The execution path
try:
    try:
        devices = list(usb.core.find(find_all=True,idVendor=0xcafe, idProduct=0x4011))
        for d in devices:
            #print('Device: %s' % d.product)
            dev = d
    except usb.core.NoBackendError as e:
        pass
    
    sender = sendByte
    receiver = receiveByte
    list_sender = sendList
    raw_receiver = receiveByte_raw

    if dev is None:
        if(os.name == "nt"):
            from winusbcdc import ComPort
            print("Trying WinUSB CDC")
            p = ComPort(vid=0xcafe, pid=0x4011)
            if not p.is_open:
                exit_no_device()
            #p.baudrate = 115200
            sender = sendByte_win
            receiver = receiveByte_win
            list_sender = sendList_win
            raw_receiver = receiveByte_raw_win
    else:
        reattach = False
        if(os.name != "nt"):
            if dev.is_kernel_driver_active(0):
                try:
                    reattach = True
                    dev.detach_kernel_driver(0)
                    #print("kernel driver detached")
                except usb.core.USBError as e:
                    sys.exit("Could not detach kernel driver: %s" % str(e))
            else:
                print("no kernel driver attached")

        dev.reset()

        dev.set_configuration()

        cfg = dev.get_active_configuration()

        #print('Configuration: %s' % cfg)

        intf = cfg[(2,0)]   # Or find interface with class 0xff

        #print('Interface: %s' % intf)

        epIn = usb.util.find_descriptor(
            intf,
            custom_match = \
            lambda e: \
                usb.util.endpoint_direction(e.bEndpointAddress) == \
                usb.util.ENDPOINT_IN)

        assert epIn is not None

        #print('EP In: %s' % epIn)

        epOut = usb.util.find_descriptor(
            intf,
            # match the first OUT endpoint
            custom_match = \
            lambda e: \
                usb.util.endpoint_direction(e.bEndpointAddress) == \
                usb.util.ENDPOINT_OUT)

        assert epOut is not None

        #print('EP Out: %s' % epOut)

        # Control transfer to enable webserial on device
        #print("control transfer out...")
        dev.ctrl_transfer(bmRequestType = 1, bRequest = 0x22, wIndex = 2, wValue = 0x01)

    transfer_func(sender, receiver, list_sender, raw_receiver)
    
    exit_gracefully()
except:
    traceback.print_exc()
    print("Unexpected exception: ", sys.exc_info()[0])
    exit_gracefully()
