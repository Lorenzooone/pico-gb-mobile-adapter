import signal
import sys
import traceback
import time
from time import sleep
from gbridge import GBridge, GBridgeSocket, GBridgeDebugCommands, GBridgeTimeResolution
from mobile_adapter_data import MobileAdapterDeviceData
import os

import threading

# Default transfer status class.
# If wait is set, the transfers are temporarily stopped.
# If end is set, the transfers are ended.
class TransferStatus:
    def __init__(self):
        self.wait = False
        self.end = False

# Default user output class.
# set_out is called, to "print" the data to the user.
# A program can intercept this, though.
class UserOutput:
    OUTPUT_DEBUG_TAG = "OUT"
    INPUT_DEBUG_TAG = "INP"
    INFO_TAG = "INF"
    PACKET_ERROR_TAG = "ERP"
    END_TAG = "END"
    USB_TAG = "USB"
    EXCEPTION_TAG = "EXC"
    WARNING_TAG = "WRN"
    DEVICE_TAG = "DVC"
    SOCKET_DEBUG_TAG = "SDB"
    DIRECT_OUTPUT_TAG = "DIR"
    SUCCESS_OPERATION_TAG = "SUC"
    UNHANDLED_INFO_DIRECT_TAG = "IND"
    TEMPORARY_TAG = "TMP"
    GBRIDGE_INFO_TAG = "GBR"
    NUMBER_SELF_TAG = "NUS"
    NUMBER_OTHER_TAG = "NUO"
    ADAPTER_STATUS_TAG = "STA"
    VERSION_MOBILE_TAG = "VRM"
    VERSION_IMPLEMENTATION_TAG = "VRI"
    ADAPTER_NAME_TAG = "NAM"
    NUMBER_REQUEST_STATE_TAG = "NRS"

    def __init__(self):
        pass

    def set_out(self, string, tag, end='\n'):
        print(string, end=end)

# Default user input class.
# get_input is called, to "get" the user's input.
# A program can use this interface, though.
# Commands are interpreted by interpret_input_keyboard.
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

    def __init__(self, user_output):
        super(SocketThread, self).__init__()
        self.daemon = True
        self.start_processing = False
        self.done_processing = False
        self.bridge = GBridge()
        self.bridge_debug = GBridge()
        self.bridge_sockets = GBridgeSocket(user_output)
        self.lock_in = threading.Lock()
        self.lock_out = threading.Lock()
        self.lock_in.acquire()
        self.lock_out.acquire()
        self.end_run = False
        self.user_output = user_output
        self.start()

    def run(self):
        TRANSFER_FLAGS_MASK = 0xC0
        DEBUG_TRANSFER_FLAG = 0x80
        print_data_in = False
        debug_print = True
        last_sent = [None, None]

        while True:
            self.lock_in.acquire()

            if self.end_run:
                break

            send_list = []
            read_data = self.data
            save_requests = self.save_requests
            ack_requests = self.ack_requests
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
                    self.user_output.set_out("IN: " + str(bytes), self.user_output.INPUT_DEBUG_TAG)
                while curr_cmd is not None:
                    curr_cmd = curr_bridge.init_cmd(bytes)
                    if(curr_cmd is not None):
                        bytes = bytes[curr_cmd.total_len - curr_cmd.old_len:]
                        curr_cmd.print_answer(save_requests, ack_requests, self.user_output)
                        if debug_print:
                            curr_cmd.do_print(self.user_output)
                        curr_cmd.check_save(save_requests, self.user_output)
                        if(curr_cmd.response_cmd is not None):
                            if(curr_cmd.process(self.bridge_sockets)):
                                curr_cmd.processed = True
                            send_list += [curr_cmd]
                        elif(curr_cmd.retry_data or curr_cmd.retry_stream):
                            send_list += [curr_cmd]

            self.out_data = []
            curr_last_sent = None
            last_sent_index = 0
            if is_debug:
                last_sent_index = 1

            for i in range(len(send_list)):
                if send_list[i].response_cmd is not None:
                    self.out_data += [send_list[i].response_cmd]
                    if send_list[i].processed:
                        self.out_data += GBridge.prepare_cmd(send_list[i].result_to_send(), False)
                        self.out_data += GBridge.prepare_cmd(send_list[i].get_if_pending(), True)
                        curr_last_sent = send_list[i]
                else:
                    if send_list[i].retry_data:
                        self.out_data += GBridge.prepare_cmd(last_sent[last_sent_index].result_to_send(), False)
                    if send_list[i].retry_stream:
                        self.out_data += GBridge.prepare_cmd(last_sent[last_sent_index].get_if_pending(), True)

            if curr_last_sent is not None:
                last_sent[last_sent_index] = curr_last_sent

            self.lock_out.release()

    def set_processing(self, data, save_requests, ack_requests):
        self.data = data
        self.save_requests = save_requests
        self.ack_requests = ack_requests

        self.lock_in.release()

    def get_processed(self):
        self.lock_out.acquire()

        return self.out_data

    def end_processing(self):
        self.end_run = True

        self.lock_in.release()

def add_result_debug_commands(actual_cmd, data, debug_send_list, ack_requests):
    result, ack_wanted = GBridgeDebugCommands.load_command(actual_cmd, data)
    debug_send_list += result
    ack_requests[actual_cmd] = ack_wanted

class InputCommand:
    def __init__(self, to_send_cmd, description, valid_inputs=[], comm_cmd=None, specific_cmd=None, show_in_all=True):
        self.description = description
        self.to_send_cmd = to_send_cmd
        self.comm_cmd = comm_cmd
        self.specific_cmd = specific_cmd
        self.valid_inputs = valid_inputs
        self.show_in_all = show_in_all
    
    def get_help(self, elem, is_all):
        if is_all and (not self.show_in_all):
            return ""
        help_string = "  " + elem
        for input_list in self.valid_inputs:
            help_string += " "
            if not input_list[0]:
                help_string += "["
            for input_element in input_list[1:]:
                if input_element.islower():
                    help_string += "$"
                help_string += input_element
                if input_element.islower():
                    help_string += "$"
                help_string += "/"
            help_string = help_string[:-1]
            if not input_list[0]:
                help_string += "]"
        help_string += "\n    " + self.description
        return help_string
        

class FullInputCommands:
    RELAY_TOKEN_SIZE = 0x10
    UNMETERED_STRING = "UNMETERED"
    ON_STRING = "ON"
    OFF_STRING = "OFF"

    basic_commands = {
        "GET EEPROM": InputCommand(GBridgeDebugCommands.SEND_EEPROM_CMD, "Outputs the EEPROM contents", show_in_all=False),
        "GET STATUS": InputCommand(GBridgeDebugCommands.STATUS_CMD, "Outputs the Adapter's state"),
        "START ADAPTER": InputCommand(GBridgeDebugCommands.START_CMD, "Starts the Adapter"),
        "STOP ADAPTER": InputCommand(GBridgeDebugCommands.STOP_CMD, "Stops the Adapter"),
        "GET INFO": InputCommand(GBridgeDebugCommands.SEND_IMPL_INFO_CMD, "Gets information about the Adapter"),
        "GET NUMBER": InputCommand(GBridgeDebugCommands.SEND_NUMBER_OWN_CMD, "Gets the current number of the Adapter"),
        "GET NUMBER_PEER": InputCommand(GBridgeDebugCommands.SEND_NUMBER_OTHER_CMD, "Gets the number of the other player"),
        "ASK NUMBER": InputCommand(GBridgeDebugCommands.ASK_NUMBER_CMD, "Requests the number, if it previously failed", show_in_all=False),
        "GET NUMBER_STATUS": InputCommand(GBridgeDebugCommands.GET_NUMBER_STATUS_CMD, "Gets the state of the number request", show_in_all=False),
        "GET RELAY_TOKEN": InputCommand(GBridgeDebugCommands.SEND_RELAY_TOKEN_CMD, "Gets the Relay Token (DO NOT SHARE)"),
        "GET GBRIDGE": InputCommand(GBridgeDebugCommands.SEND_GBRIDGE_CFG_CMD, "Gets the GBridge Configuration", show_in_all=False),
        "FORCE SAVE": InputCommand(GBridgeDebugCommands.FORCE_SAVE_CMD, "Enforces a data save")
    }
    
    on_off_commands = {
        "AUTO SAVE": InputCommand(GBridgeDebugCommands.SET_SAVE_STYLE_CMD, "Toggles automatic data saving on/off", valid_inputs = [[True, ON_STRING, OFF_STRING]])
    }
    
    mobile_adapter_commands = {
        "SET DEVICE": InputCommand(GBridgeDebugCommands.UPDATE_DEVICE_CMD, "Changes the Adapter's type", valid_inputs = [[True] + list(MobileAdapterDeviceData.mobile_adapter_device_types.keys()) + ["value"], [False, UNMETERED_STRING]])
    }
    
    unsigned_commands = {
        "SET P2P_PORT": InputCommand(GBridgeDebugCommands.UPDATE_P2P_PORT_CMD, "Sets the port used for direct P2P", valid_inputs = [[True, "port_number", GBridgeSocket.AUTO_STR]])
    }
    
    byte_commands = {
        "SET GBRIDGE_TRIES": InputCommand(GBridgeDebugCommands.UPDATE_GBRIDGE_CFG_CMD, "Sets the maximum number of tries for GBridge (0 = Infinite, 255 = Limit)", valid_inputs = [[True, "tries_number"]], show_in_all=False)
    }
    
    time_commands = {
        "SET GBRIDGE_TIMEOUT": InputCommand(GBridgeDebugCommands.UPDATE_GBRIDGE_CFG_CMD, "Sets the maximum time before timeout for GBridge (0 = Infinite)", valid_inputs = [[True, "timeout_time_seconds"]], show_in_all=False)
    }
    
    token_commands = {
        "SET RELAY_TOKEN": InputCommand(GBridgeDebugCommands.UPDATE_RELAY_TOKEN_CMD, "(Un)Sets the P2P Relay Token", valid_inputs = [[True, "relay_token", GBridgeSocket.NULL_STR]])
    }
    
    address_commands = {
        "SET DNS_1": InputCommand(GBridgeDebugCommands.UPDATE_DNS1_CMD, "(Un)Sets the Adapter's DNS 1", valid_inputs = [[True, "port_number", GBridgeSocket.AUTO_STR, GBridgeSocket.NULL_STR], [False, "ipv4_address", "ipv6_address"]]),
        "SET DNS_2": InputCommand(GBridgeDebugCommands.UPDATE_DNS2_CMD, "(Un)Sets the Adapter's DNS 2", valid_inputs = [[True, "port_number", GBridgeSocket.AUTO_STR, GBridgeSocket.NULL_STR], [False, "ipv4_address", "ipv6_address"]]),
        "SET RELAY": InputCommand(GBridgeDebugCommands.UPDATE_RELAY_CMD, "(Un)Sets the Adapter's P2P Relay Address", valid_inputs = [[True, "port_number", GBridgeSocket.AUTO_STR, GBridgeSocket.NULL_STR], [False, "ipv4_address", "ipv6_address"]])
    }

    loading_commands = {
        "LOAD EEPROM": InputCommand(GBridgeDebugCommands.UPDATE_EEPROM_CMD, "Sends an EEPROM backup to the Adapter. Only works if the adapter is STOPPED!", valid_inputs = [[True, "load_path"]])
    }

    saving_commands = {
        "SAVE EEPROM": InputCommand(GBridgeDebugCommands.SEND_EEPROM_CMD, "Saves an EEPROM backup. Might need the adapter to be STOPPED!", valid_inputs = [[True, "save_path"]], comm_cmd=GBridge.GBRIDGE_CMD_DEBUG_INFO, specific_cmd=GBridgeDebugCommands.CMD_DEBUG_INFO_CFG),
        "SAVE DBG_IN": InputCommand(None, "Saves a debug log of the data sent from the GameBoy", valid_inputs = [[True, "save_path"]], comm_cmd=GBridge.GBRIDGE_CMD_DEBUG_LOG, specific_cmd=GBridgeDebugCommands.CMD_DEBUG_LOG_IN, show_in_all=False),
        "SAVE DBG_OUT": InputCommand(None, "Saves a debug log of the data sent to the GameBoy", valid_inputs = [[True, "save_path"]], comm_cmd=GBridge.GBRIDGE_CMD_DEBUG_LOG, specific_cmd=GBridgeDebugCommands.CMD_DEBUG_LOG_OUT, show_in_all=False),
        "SAVE TIME_TR": InputCommand(None, "Saves a debug log of the time needed for a single transfer", valid_inputs = [[True, "save_path"]], comm_cmd=GBridge.GBRIDGE_CMD_DEBUG_LOG, specific_cmd=GBridgeDebugCommands.CMD_DEBUG_LOG_TIME_TR, show_in_all=False),
        "SAVE TIME_AC": InputCommand(None, "Saves a debug log of the time between transfers", valid_inputs = [[True, "save_path"]], comm_cmd=GBridge.GBRIDGE_CMD_DEBUG_LOG, specific_cmd=GBridgeDebugCommands.CMD_DEBUG_LOG_TIME_AC, show_in_all=False),
        "SAVE TIME_IR": InputCommand(None, "Saves a debug log of the time needed for the transfer's logic", valid_inputs = [[True, "save_path"]], comm_cmd=GBridge.GBRIDGE_CMD_DEBUG_LOG, specific_cmd=GBridgeDebugCommands.CMD_DEBUG_LOG_TIME_IR, show_in_all=False)
    }
    
    full_commands = [
        basic_commands,
        on_off_commands,
        byte_commands,
        time_commands,
        mobile_adapter_commands,
        address_commands,
        unsigned_commands,
        token_commands,
        saving_commands,
        loading_commands
    ]
    
def interpret_input_keyboard(key_input, debug_send_list, save_requests, ack_requests, user_output):
    
    close_all = False

    for elem in key_input.get_input():
        tokens = elem.split()
        command = ""
        success = False
        if(len(tokens) == 1):
            if tokens[0].upper().strip() == "HELP":

                help_string = "-----------------\nCommands:\n"
                help_string += "  HELP\n    Prints this help message\n\n"
                help_string += "  QUIT\n    Stops the application\n\n"
                for i in range(len(FullInputCommands.full_commands)):
                    command_list = FullInputCommands.full_commands[i]
                    for elem in command_list.keys():
                        cmd_help_string = command_list[elem].get_help(elem, True)
                        if cmd_help_string != "":
                            help_string += cmd_help_string + "\n\n"
                help_string = help_string[:-1] + "-----------------"
                    
                user_output.set_out(help_string, user_output.INFO_TAG)
                success = True
            if tokens[0].upper().strip() == "QUIT":
                help_string = "Stopping the application..."                    
                user_output.set_out(help_string, user_output.END_TAG)
                close_all = True
                success = True

        if(len(tokens) >= 2):
            command = tokens[0].upper().strip() + " " + tokens[1].upper().strip()

        if command in FullInputCommands.basic_commands.keys():
            add_result_debug_commands(FullInputCommands.basic_commands[command].to_send_cmd, None, debug_send_list, ack_requests)
            success = True

        if len(tokens) > 2:
            if command in FullInputCommands.saving_commands.keys():
                save_path = tokens[2].strip()
                if FullInputCommands.saving_commands[command].to_send_cmd is not None:
                    add_result_debug_commands(FullInputCommands.saving_commands[command].to_send_cmd, None, debug_send_list, ack_requests)
                if FullInputCommands.saving_commands[command].comm_cmd not in save_requests.keys():
                    save_requests[FullInputCommands.saving_commands[command].comm_cmd] = dict()
                save_requests[FullInputCommands.saving_commands[command].comm_cmd][FullInputCommands.saving_commands[command].specific_cmd] = save_path
                success = True
            
            if command in FullInputCommands.loading_commands.keys():
                load_path = tokens[2].strip()
                data = None
                try:
                    with open(load_path, mode='rb') as file_read:
                        data = file_read.read()
                except:
                    user_output.set_out("Error while reading file!", user_output.EXCEPTION_TAG)
                if data is not None:
                    add_result_debug_commands(FullInputCommands.loading_commands[command].to_send_cmd, data, debug_send_list, ack_requests)
                    success = True

            if command in FullInputCommands.mobile_adapter_commands.keys():
                mobile_type = tokens[2].upper().strip()
                value = None
                try:
                    value = int(mobile_type)
                    if (value < 0) or (value > 127):
                        value = None
                except:
                    pass
                metered = True
                if (len(tokens) > 3) and (tokens[3] == FullInputCommands.UNMETERED_STRING):
                    metered = False
                if (value is None) and (mobile_type in MobileAdapterDeviceData.mobile_adapter_device_types.keys()):
                    value = MobileAdapterDeviceData.mobile_adapter_device_types[mobile_type]
                if value is not None:
                    if not metered:
                        value |= 0x80
                    user_output.set_out("WARNING: You may need to restart the Game Boy entirely for this change to work!", user_output.WARNING_TAG)
                    add_result_debug_commands(FullInputCommands.mobile_adapter_commands[command].to_send_cmd, value, debug_send_list, ack_requests)
                    success = True

            if command in FullInputCommands.unsigned_commands.keys():
                value = GBridgeSocket.parse_unsigned(FullInputCommands.unsigned_commands[command].to_send_cmd, tokens[2])
                if value is not None:
                    add_result_debug_commands(FullInputCommands.unsigned_commands[command].to_send_cmd, value, debug_send_list, ack_requests)
                    success = True

            if command in FullInputCommands.address_commands.keys():
                data = GBridgeSocket.parse_addr(FullInputCommands.address_commands[command].to_send_cmd, tokens[2:])
                if data is not None:
                    add_result_debug_commands(FullInputCommands.address_commands[command].to_send_cmd, data, debug_send_list, ack_requests)
                    success = True

            if command in FullInputCommands.on_off_commands.keys():
                if(tokens[2].upper().strip() == FullInputCommands.ON_STRING):
                    add_result_debug_commands(FullInputCommands.on_off_commands[command].to_send_cmd, 1, debug_send_list, ack_requests)
                    success = True
                if(tokens[2].upper().strip() == FullInputCommands.OFF_STRING):
                    add_result_debug_commands(FullInputCommands.on_off_commands[command].to_send_cmd, 0, debug_send_list, ack_requests)
                    success = True

            if command in FullInputCommands.byte_commands.keys():
                value = None
                try:
                    value = int(tokens[2].upper().strip())
                    if (value < 0) or (value > 255):
                        value = None
                except:
                    pass
                if(value is not None):
                    add_result_debug_commands(FullInputCommands.byte_commands[command].to_send_cmd, [2] + [value], debug_send_list, ack_requests)
                    success = True

            if command in FullInputCommands.time_commands.keys():
                data = []
                try:
                    time_got = GBridgeTimeResolution(float(tokens[2].upper().strip()))
                    data = time_got.time_to_data()
                except:
                    pass
                if len(data) > 0:
                    add_result_debug_commands(FullInputCommands.time_commands[command].to_send_cmd, [1] + data, debug_send_list, ack_requests)
                    success = True

            if command in FullInputCommands.token_commands.keys():
                data = []
                try:
                    data = list(bytes.fromhex(tokens[2].upper().strip()))
                except:
                    pass
                if len(data) == FullInputCommands.RELAY_TOKEN_SIZE:
                    add_result_debug_commands(FullInputCommands.token_commands[command].to_send_cmd, [1] + data, debug_send_list, ack_requests)
                    success = True
                elif tokens[2].upper().strip() == GBridgeSocket.NULL_STR:
                    add_result_debug_commands(FullInputCommands.token_commands[command].to_send_cmd, [0], debug_send_list, ack_requests)
                    success = True

        if not success:
            for command_list in FullInputCommands.full_commands:
                if command in command_list.keys():
                    help_string = "\nUsage:\n" + command_list[command].get_help(command, False) + "\n"
                    user_output.set_out(help_string, user_output.INFO_TAG)
                    break

    return close_all

def prepare_out_func(analyzed_list, is_debug_cmd, user_output):
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
            user_output.set_out("OUT: " + str(out_buf[1:]), user_output.OUTPUT_DEBUG_TAG)
    return out_buf, num_elems

# Main function, gets the four basic USB connection send/recv functions, the way to get the user input class,
# the transfer state's class and the user output class.
def transfer_func(sender, receiver, list_sender, raw_receiver, pc_commands, transfer_state, user_output):
    out_data_preparer = SocketThread(user_output)
    user_output.set_out("Type HELP to get a list of the available commands", user_output.INFO_TAG)
    send_list = []
    debug_send_list = []
    save_requests = dict()
    ack_requests = dict()
    while not transfer_state.end:
        while transfer_state.wait:
            pass
        asked_quit = interpret_input_keyboard(pc_commands, debug_send_list, save_requests, ack_requests, user_output)

        if asked_quit:
            transfer_state.end = True

        if len(send_list) == 0:
            if len(debug_send_list) > 0:
                out_buf, num_elems = prepare_out_func(debug_send_list[0], True, user_output)
                debug_send_list = debug_send_list[1:]
            else:
                out_buf, num_elems = prepare_out_func([], True, user_output)
        else:
            out_buf, num_elems = prepare_out_func(send_list, False, user_output)
            send_list = send_list[num_elems:]
        list_sender(out_buf, chunk_size = len(out_buf))

        out_data_preparer.set_processing(raw_receiver(0x40), save_requests, ack_requests)
        send_list += out_data_preparer.get_processed()
        sleep(0.01)

    out_data_preparer.end_processing()

class LibUSBSendRecv:
    def __init__(self, epOut, epIn, dev, reattach, max_usb_timeout):
        self.epOut = epOut
        self.epIn = epIn
        self.dev = dev
        self.reattach = reattach
        self.max_usb_timeout = max_usb_timeout

    # Code dependant on this connection method
    def sendByte(self, byte_to_send, num_bytes):
        self.epOut.write(byte_to_send.to_bytes(num_bytes, byteorder='big'), timeout=self.max_usb_timeout * 1000)
        return

    # Code dependant on this connection method
    def sendList(self, data, chunk_size=8):
        num_iters = int(len(data)/chunk_size)
        for i in range(num_iters):
            self.epOut.write(data[i*chunk_size:(i+1)*chunk_size], timeout=self.max_usb_timeout * 1000)
        if (num_iters*chunk_size) != len(data):
            self.epOut.write(data[num_iters*chunk_size:], timeout=self.max_usb_timeout * 1000)

    def receiveByte(self, num_bytes):
        recv = int.from_bytes(self.epIn.read(num_bytes, timeout=self.max_usb_timeout * 1000), byteorder='big')
        return recv

    def receiveByte_raw(self, num_bytes):
        return self.epIn.read(num_bytes, timeout=self.max_usb_timeout * 1000)
    
    def kill_function(self):
        import usb.util
        usb.util.dispose_resources(self.dev)
        if(os.name != "nt"):
            if self.reattach:
                self.dev.attach_kernel_driver(0)

class PySerialSendRecv:
    def __init__(self, serial_port):
        self.serial_port = serial_port

    # Code dependant on this connection method
    def sendByte(self, byte_to_send, num_bytes):
        self.serial_port.write(byte_to_send.to_bytes(num_bytes, byteorder='big'))
        return

    # Code dependant on this connection method
    def sendList(self, data, chunk_size=8):
        num_iters = int(len(data)/chunk_size)
        for i in range(num_iters):
            self.serial_port.write(bytes(data[i*chunk_size:(i+1)*chunk_size]))
        if (num_iters*chunk_size) != len(data):
            self.serial_port.write(bytes(data[num_iters*chunk_size:]))

    def receiveByte(self, num_bytes):
        recv = int.from_bytes(self.serial_port.read(num_bytes), byteorder='big')
        return recv

    def receiveByte_raw(self, num_bytes):
        return self.serial_port.read(num_bytes)
    
    def kill_function(self):
        self.serial_port.reset_input_buffer()
        self.serial_port.reset_output_buffer()
        self.serial_port.close()

class WinUSBCDCSendRecv:
    def __init__(self, p):
        self.p = p
        
    # Code dependant on this connection method
    def sendByte(self, byte_to_send, num_bytes):
        self.p.write(byte_to_send.to_bytes(num_bytes, byteorder='big'))

    # Code dependant on this connection method
    def sendList(self, data, chunk_size=8):
        num_iters = int(len(data)/chunk_size)
        for i in range(num_iters):
            self.p.write(bytes(data[i*chunk_size:(i+1)*chunk_size]))
        if (num_iters*chunk_size) != len(data):
            self.p.write(bytes(data[num_iters*chunk_size:]))

    def receiveByte(self, num_bytes):
        recv = int.from_bytes(self.p.read(size=num_bytes), byteorder='big')
        return recv

    def receiveByte_raw(self, num_bytes):
        return self.p.read(size=num_bytes)
    
    def kill_function(self):
        pass

# Things for the USB connection part
def exit_gracefully(usb_handler):
    if usb_handler is not None:
        usb_handler.kill_function()
    os._exit(1)

def libusb_method(VID, PID, max_usb_timeout, user_output):
    import usb.core
    import usb.util
    dev = None
    try:
        devices = list(usb.core.find(find_all=True,idVendor=VID, idProduct=PID))
        for d in devices:
            #user_output.set_out("Device: " + str(d.product), user_output.USB_TAG)
            dev = d
        if dev is None:
            return None
        reattach = False
        if(os.name != "nt"):
            if dev.is_kernel_driver_active(0):
                try:
                    reattach = True
                    dev.detach_kernel_driver(0)
                except usb.core.USBError as e:
                    sys.exit("Could not detach kernel driver: %s" % str(e))
            else:
                pass
                #user_output.set_out("no kernel driver attached", user_output.USB_TAG)
        
        dev.reset()

        dev.set_configuration()

        cfg = dev.get_active_configuration()

        intf = cfg[(2,0)]   # Or find interface with class 0xff

        epIn = usb.util.find_descriptor(
            intf,
            custom_match = \
            lambda e: \
                usb.util.endpoint_direction(e.bEndpointAddress) == \
                usb.util.ENDPOINT_IN)

        assert epIn is not None

        epOut = usb.util.find_descriptor(
            intf,
            custom_match = \
            lambda e: \
                usb.util.endpoint_direction(e.bEndpointAddress) == \
                usb.util.ENDPOINT_OUT)

        assert epOut is not None

        dev.ctrl_transfer(bmRequestType = 1, bRequest = 0x22, wIndex = 2, wValue = 0x01)
    except:
        return None
    return LibUSBSendRecv(epOut, epIn, dev, reattach, max_usb_timeout)

def winusbcdc_method(VID, PID, max_usb_timeout, user_output):
    if(os.name == "nt"):
        from winusbcdc import ComPort
        try:
            user_output.set_out("Trying WinUSB CDC", user_output.USB_TAG)
            p = ComPort(vid=VID, pid=PID)
            if not p.is_open:
                return None
            #p.baudrate = 115200
            p.settimeout(max_usb_timeout)
        except:
            return None
    else:
        return None
    return WinUSBCDCSendRecv(p)

def serial_method(VID, PID, max_usb_timeout, user_output):
    import serial
    import serial.tools.list_ports
    try:
        ports = list(serial.tools.list_ports.comports())
        serial_success = False
        port = None
        for device in ports:
            if(device.vid is not None) and (device.pid is not None):
                if(device.vid == VID) and (device.pid == PID):
                    port = device.device
                    break
        if port is None:
            return None
        serial_port = serial.Serial(port=port, bytesize=8, timeout=0.05, write_timeout = max_usb_timeout)
    except Exception as e:
        return None
    return PySerialSendRecv(serial_port)

# Initial function which sets up the USB connection and then calls the Main function.
# Gets the ending function once the connection ends, then the USB identifiers, and the USB Timeout.
# Also receives the user input class, the transfer state's class and the user output class.
def start_usb_transfer(end_function, VID, PID, max_usb_timeout, pc_commands, transfer_state, user_output, do_ctrl_c_handling=False):
    try_serial = False
    try_libusb = False
    try_winusbcdc = False
    try:
        import usb.core
        import usb.util
        try_libusb = True
    except:
        pass

    if(os.name == "nt"):
        try:
            from winusbcdc import ComPort
            try_winusbcdc = True
        except:
            pass
    try:
        import serial
        import serial.tools.list_ports
        try_serial = True
    except:
        pass

    usb_handler = None

    def signal_handler_ctrl_c(sig, frame):
        user_output.set_out("You pressed Ctrl+C!", user_output.END_TAG)
        transfer_state.end = True

    if do_ctrl_c_handling:
        signal.signal(signal.SIGINT, signal_handler_ctrl_c)

    # The execution path
    try:
        if(usb_handler is None) and try_libusb:
            usb_handler = libusb_method(VID, PID, max_usb_timeout, user_output)
        if (usb_handler is None) and try_winusbcdc:
            usb_handler = winusbcdc_method(VID, PID, max_usb_timeout, user_output)
        if (usb_handler is None) and try_serial:
            usb_handler = serial_method(VID, PID, max_usb_timeout, user_output)

        if usb_handler is not None:
            user_output.set_out("USB connection established!", user_output.USB_TAG)
            transfer_func(usb_handler.sendByte, usb_handler.receiveByte, usb_handler.sendList, usb_handler.receiveByte_raw, pc_commands, transfer_state, user_output)
        else:
            user_output.set_out("Couldn't find USB device!", user_output.USB_TAG)
            missing = ""
            if not try_serial:
                missing += "PySerial, "
            if not try_libusb:
                missing += "PyUSB, "
            if(os.name == "nt") and (not try_winusbcdc):
                missing += "WinUsbCDC, "
            if missing != "":
                user_output.set_out("If the device is attached, try installing " + missing[:-2], user_output.USB_TAG)
        
        end_function(usb_handler)
    except:
        #traceback.print_exc()
        user_output.set_out("Unexpected exception: " + str(sys.exc_info()[0]), user_output.EXCEPTION_TAG)
        end_function(usb_handler)

if __name__ == "__main__":
    VID = 0xcafe
    PID = 0x4011
    max_usb_timeout = 5
    start_usb_transfer(exit_gracefully, VID, PID, max_usb_timeout, KeyboardThread(), TransferStatus(), UserOutput(), do_ctrl_c_handling=True)
