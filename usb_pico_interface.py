import usb.core
import usb.util
import signal
import sys
import traceback
import time
from time import sleep
from gbridge import GBridge, GBridgeSocket
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
        self.recieved = []
        self.daemon = True
        self.start()

    def run(self):
        while True:
            self.recieved += [input()]

    def get_input(self):
        out = self.recieved
        if(len(self.recieved) > 0):
            self.recieved = []
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
    SEND_EEPROM_CMD = 1

    for elem in key_input.get_input():
        if elem == "GET EEPROM":
            debug_send_list += [SEND_EEPROM_CMD]
        if elem.startswith("SAVE EEPROM"):
            debug_send_list += [SEND_EEPROM_CMD]
            tokens = elem.split()
            if(len(tokens) > 2):
                save_path = tokens[2].strip()
                save_requests[GBridge.GBRIDGE_CMD_DEBUG_CFG] = save_path
        if elem.startswith("SAVE DBG_IN"):
            tokens = elem.split()
            if(len(tokens) > 2):
                save_path = tokens[2].strip()
                save_requests[GBridge.GBRIDGE_CMD_DEBUG_LOG_IN] = save_path
        if elem.startswith("SAVE DBG_OUT"):
            tokens = elem.split()
            if(len(tokens) > 2):
                save_path = tokens[2].strip()
                save_requests[GBridge.GBRIDGE_CMD_DEBUG_LOG_OUT] = save_path
        if elem.startswith("SAVE TIME_TR"):
            tokens = elem.split()
            if(len(tokens) > 2):
                save_path = tokens[2].strip()
                save_requests[GBridge.GBRIDGE_CMD_DEBUG_TIME_TR] = save_path
        if elem.startswith("SAVE TIME_AC"):
            tokens = elem.split()
            if(len(tokens) > 2):
                save_path = tokens[2].strip()
                save_requests[GBridge.GBRIDGE_CMD_DEBUG_TIME_AC] = save_path

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
                    print("kernel driver detached")
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
