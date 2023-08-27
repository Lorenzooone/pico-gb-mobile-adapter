from time import sleep
import socket
import errno

class GBridgeCommand:
    GBRIDGE_PROT_MA_CMD_OPEN = 0
    GBRIDGE_PROT_MA_CMD_CLOSE = 1
    GBRIDGE_PROT_MA_CMD_CONNECT = 2
    GBRIDGE_PROT_MA_CMD_LISTEN = 3
    GBRIDGE_PROT_MA_CMD_ACCEPT = 4
    GBRIDGE_PROT_MA_CMD_SEND = 5
    GBRIDGE_PROT_MA_CMD_RECV = 6
    
    last_send = None
    
    AllowedAnswers = set([GBRIDGE_PROT_MA_CMD_CLOSE, GBRIDGE_PROT_MA_CMD_OPEN, GBRIDGE_PROT_MA_CMD_CONNECT, GBRIDGE_PROT_MA_CMD_LISTEN, GBRIDGE_PROT_MA_CMD_ACCEPT, GBRIDGE_PROT_MA_CMD_SEND, GBRIDGE_PROT_MA_CMD_RECV])
    
    def __init__(self, data, success_checksum, upper_cmd, total_len, old_len):
        self.upper_cmd = upper_cmd
        self.pending = None
        self.total_len = total_len
        self.old_len = old_len
        self.is_split = False
        self.response_cmd = None
        if (not (self.upper_cmd & GBridge.GBRIDGE_CMD_REPLY_F)) and (not (self.upper_cmd in GBridge.no_reply_upper_cmds)):
            self.response_cmd = self.upper_cmd | GBridge.GBRIDGE_CMD_REPLY_F
        self.command = None
        self.answer = []
        self.success_checksum = True
        if(self.upper_cmd == GBridge.GBRIDGE_CMD_DATA):
            self.success_checksum = success_checksum
            if(self.success_checksum):
                if(len(data) > 0):
                    self.command = data[0]
                    if(self.command == GBridgeCommand.GBRIDGE_PROT_MA_CMD_SEND):
                        GBridgeCommand.last_send = self
                        self.command = None
                    self.size = len(data)
                    self.data = data[1:]
            
        if(self.upper_cmd == GBridge.GBRIDGE_CMD_STREAM):
            self.success_checksum = success_checksum
            if(self.success_checksum):
                self.command = GBridgeCommand.GBRIDGE_PROT_MA_CMD_SEND
                self.data = data
                self.size = len(data)
        if(self.upper_cmd in GBridge.no_reply_upper_cmds):
            self.success_checksum = success_checksum
            self.data = data
            self.size = len(data)
        if (self.response_cmd is not None) and (not self.success_checksum):
            self.response_cmd += 1
    
    def process(self, sockets):
        if self.command is None:
            return False
        if self.command == GBridgeCommand.GBRIDGE_PROT_MA_CMD_OPEN:
            value = 0
            if(sockets.open(self.data)):
                value = 1
            self.answer = [value]
            return True
        if self.command == GBridgeCommand.GBRIDGE_PROT_MA_CMD_CLOSE:
            sockets.close(self.data)
            return True
        if self.command == GBridgeCommand.GBRIDGE_PROT_MA_CMD_CONNECT:
            self.answer = [sockets.connect(self.data) & 0xFF]
            return True
        if self.command == GBridgeCommand.GBRIDGE_PROT_MA_CMD_LISTEN:
            value = 0
            if(sockets.listen(self.data)):
                value = 1
            self.answer = [value]
            return True
        if self.command == GBridgeCommand.GBRIDGE_PROT_MA_CMD_ACCEPT:
            value = 0
            if(sockets.accept(self.data)):
                value = 1
            self.answer = [value]
            return True
        if (self.command == GBridgeCommand.GBRIDGE_PROT_MA_CMD_SEND) and (self.upper_cmd == GBridge.GBRIDGE_CMD_STREAM):
            if GBridgeCommand.last_send is not None:
                sent = sockets.send(GBridgeCommand.last_send.data, self.data)
                self.answer = [(sent >> 8) & 0xFF, sent & 0xFF]
                return True
        if self.command == GBridgeCommand.GBRIDGE_PROT_MA_CMD_RECV:
            self.pending = None
            result = sockets.recv(self.data)
            if len(result[0]) > 0:
                self.pending = result[0]
            self.answer = result[1]
            return True
        return False

    def result_to_send(self):
        if self.command in GBridgeCommand.AllowedAnswers:
            return [self.command] + self.answer
        return []
    
    def get_if_pending(self):
        if self.pending is not None:
            answer = self.pending
            self.pending = None
            return answer
        return []
    
    def do_print(self):
        if self.upper_cmd == GBridge.GBRIDGE_CMD_DEBUG_LINE:
            if not self.success_checksum:
                print("CHECKSUM ERROR!")
                return
            print(bytes(self.data).decode('utf-8'), end='')
        if self.upper_cmd == GBridge.GBRIDGE_CMD_DEBUG_CHAR:
            if not self.success_checksum:
                print("CHECKSUM ERROR!")
                return
            print(GBridgeCommand.prepare_hex_list_str(self.data))

    def print_answer(self, save_requests, ack_requests):
        if self.upper_cmd == GBridge.GBRIDGE_CMD_DEBUG_INFO:
            if len(self.data) <= 0:
                print("SIZE ERROR!")
                return
            if not self.success_checksum:
                print("CHECKSUM ERROR!")
                return
            found = False
            if (self.upper_cmd in save_requests.keys()) and (self.data[0] in save_requests[self.upper_cmd].keys()) and (save_requests[self.upper_cmd][self.data[0]] != ""):
                found = True
            if self.data[0] == GBridgeDebugCommands.CMD_DEBUG_INFO_CFG:
                if not found:
                    print(GBridgeCommand.prepare_hex_list_str(self.data[1:]))
            if self.data[0] == GBridgeDebugCommands.CMD_DEBUG_INFO_NAME:
                print(bytes(self.data[1:]).decode('utf-8'))
            if self.data[0] == GBridgeDebugCommands.CMD_DEBUG_INFO_OTHER:
                print(bytes(self.data[1:]).decode('utf-8'))
            if self.data[0] == GBridgeDebugCommands.CMD_DEBUG_INFO_STATUS:
                str_status = "STATUS: "
                if(self.data[1] & 1):
                    str_status += "ACTIVE, "
                else:
                    str_status += "STOPPED, "
                if(self.data[1] & 2):
                    str_status += "CAN SAVE, "
                else:
                    str_status += "CANNOT SAVE, "
                if(self.data[1] & 4):
                    str_status += "AUTOMATIC SAVE: ON"
                else:
                    str_status += "AUTOMATIC SAVE: OFF"
                print(str_status)
            if self.data[0] == GBridgeDebugCommands.CMD_DEBUG_INFO_NUMBER:
                print("YOUR NUMBER: " + bytes(self.data[1:]).split(b'\0',1)[0].decode('ascii'))
            if self.data[0] == GBridgeDebugCommands.CMD_DEBUG_INFO_NUMBER_PEER:
                print("OTHER'S NUMBER: " + bytes(self.data[1:]).split(b'\0',1)[0].decode('ascii'))
            if self.data[0] == GBridgeDebugCommands.CMD_DEBUG_INFO_RELAY_TOKEN:
                if len(self.data) > 1:
                    str_out = "RELAY TOKEN: "
                    if self.data[1] == 0:
                        str_out += "None"
                    else:
                        str_out += bytes(self.data[2:]).hex().upper()
                    print(str_out)
        if self.upper_cmd == GBridge.GBRIDGE_CMD_DEBUG_ACK:
            if len(self.data) <= 0:
                print("SIZE ERROR!")
                return
            if not self.success_checksum:
                print("CHECKSUM ERROR!")
                return
            command = self.data[0]
            if command in ack_requests.keys():
                if ack_requests[command] > 0:
                    ack_requests[command] -= 1
                    if ack_requests[command] == 0:
                        print("OPERATION SUCCESSFUL")

    def prepare_str_out(self, size_entry):
        str_out = ""
        for i in range(int(len(self.data[1:]) / size_entry)):
            str_out += str(int.from_bytes(self.data[1 + (i * size_entry): 1 + ((i + 1) * size_entry)], byteorder='little')) + "\n"
        return str_out

    def save_x_size(self, size_entry):
        str_out = self.prepare_str_out(size_entry)
        with open(save_requests[self.upper_cmd][self.data[0]], "w") as f:
            f.write(str_out)
        print("Saved to: " + save_requests[self.upper_cmd][self.data[0]])
        save_requests[self.upper_cmd][self.data[0]] = ""
    
    def check_save(self, save_requests):
        if (self.upper_cmd in save_requests.keys()) and (len(self.data) > 0) and (self.data[0] in save_requests[self.upper_cmd].keys()) and (save_requests[self.upper_cmd][self.data[0]] != ""):
            if not self.success_checksum:
                print("CHECKSUM ERROR!")
                return
            single_byte_saves = {
                GBridge.GBRIDGE_CMD_DEBUG_INFO : {GBridgeDebugCommands.CMD_DEBUG_INFO_CFG},
                GBridge.GBRIDGE_CMD_DEBUG_LOG : {GBridgeDebugCommands.CMD_DEBUG_LOG_IN, GBridgeDebugCommands.CMD_DEBUG_LOG_OUT}
            }
            uint8_t_saves = {
                GBridge.GBRIDGE_CMD_DEBUG_INFO : {},
                GBridge.GBRIDGE_CMD_DEBUG_LOG : {}
            }
            uint16_t_saves = {
                GBridge.GBRIDGE_CMD_DEBUG_INFO : {},
                GBridge.GBRIDGE_CMD_DEBUG_LOG : {GBridgeDebugCommands.CMD_DEBUG_LOG_TIME_TR, GBridgeDebugCommands.CMD_DEBUG_LOG_TIME_AC, GBridgeDebugCommands.CMD_DEBUG_LOG_TIME_IR}
            }
            uint32_t_saves = {
                GBridge.GBRIDGE_CMD_DEBUG_INFO : {},
                GBridge.GBRIDGE_CMD_DEBUG_LOG : {}
            }
            uint64_t_saves = {
                GBridge.GBRIDGE_CMD_DEBUG_INFO : {},
                GBridge.GBRIDGE_CMD_DEBUG_LOG : {}
            }
            if self.data[0] in single_byte_saves[self.upper_cmd]:
                with open(save_requests[self.upper_cmd][self.data[0]], "wb") as f:
                    f.write(bytes(self.data[1:]))
                print("Saved to: " + save_requests[self.upper_cmd][self.data[0]])
                save_requests[self.upper_cmd][self.data[0]] = ""
            if self.data[0] in uint8_t_saves[self.upper_cmd]:
                self.save_x_size(1)
            if self.data[0] in uint16_t_saves[self.upper_cmd]:
                self.save_x_size(2)
            if self.data[0] in uint32_t_saves[self.upper_cmd]:
                self.save_x_size(4)
            if self.data[0] in uint64_t_saves[self.upper_cmd]:
                self.save_x_size(8)
    
    def prepare_hex_list_str(values):
        string_out = "["
        for i in range(len(values)):
            value = values[i]
            if value < 0x10:
                string_out += " "
            string_out += str(hex(value)) + ", "
        string_out = string_out[: -2]
        string_out += "]"
        return string_out
        

class GBridge:
    GBRIDGE_CMD_DEBUG_LINE = 0x02
    GBRIDGE_CMD_DEBUG_CHAR = 0x03
    GBRIDGE_CMD_DEBUG_INFO = 0x04
    GBRIDGE_CMD_DEBUG_LOG = 0x05
    GBRIDGE_CMD_DEBUG_ACK = 0x08
    GBRIDGE_CMD_DATA = 0x0A
    GBRIDGE_CMD_STREAM = 0x0C
    GBRIDGE_CMD_DATA_PC = 0x4A
    GBRIDGE_CMD_STREAM_PC = 0x4C
    GBRIDGE_CMD_REPLY_F = 0x80
    
    no_reply_upper_cmds = {
        GBRIDGE_CMD_DEBUG_LINE,
        GBRIDGE_CMD_DEBUG_CHAR,
        GBRIDGE_CMD_DEBUG_INFO,
        GBRIDGE_CMD_DEBUG_LOG,
        GBRIDGE_CMD_DEBUG_ACK
    }
    
    cmd_lens = {
        GBRIDGE_CMD_DATA: 1,
        GBRIDGE_CMD_STREAM: 2,
        GBRIDGE_CMD_DEBUG_LINE: 2,
        GBRIDGE_CMD_DEBUG_CHAR: 2,
        GBRIDGE_CMD_DEBUG_INFO: 2,
        GBRIDGE_CMD_DEBUG_LOG: 2,
        GBRIDGE_CMD_DEBUG_ACK: 0
    }
    
    fixed_lens = {
        GBRIDGE_CMD_DEBUG_ACK: 1
    }

    def __init__(self):
        self.reset_cmd()
    
    def init_cmd(self, data):
        old_len = len(self.curr_data)
        self.curr_data += data
        result = self.consume_cmd()
        if result is not None:
            result = GBridgeCommand(self.final_data, self.checksum_okay, self.curr_cmd, self.total_len, old_len)
            self.reset_cmd()
        return result

    def prepare_cmd(data, is_stream):
        if len(data) == 0:
            return []

        cmd = GBridge.GBRIDGE_CMD_DATA_PC
        size_length = 1
        if is_stream:
            cmd = GBridge.GBRIDGE_CMD_STREAM_PC
            size_length = 2

        start = [cmd] + list(len(data).to_bytes(size_length, byteorder='big'))
        checksum = GBridge.calc_checksum(data)
        return start + data + list(checksum.to_bytes(2, byteorder='big'))
           
    def consume_cmd(self):
        self.total_len = 0
        if(len(self.curr_data) > 0):
            self.curr_cmd = self.curr_data[0]
        else:
            return None
        self.total_len += 1
        len_length = 0
        if(self.curr_cmd in GBridge.cmd_lens.keys()):
            len_length = GBridge.cmd_lens[self.curr_cmd]
        if(len_length > 0) and (len(self.curr_data) > len_length):
            self.curr_len = int.from_bytes(self.curr_data[1 : 1 + len_length], byteorder='big')
        if self.curr_cmd in GBridge.fixed_lens.keys():
            self.curr_len = GBridge.fixed_lens[self.curr_cmd]
        self.total_len += len_length + self.curr_len
        if(len(self.curr_data) > self.curr_len + len_length):
            self.final_data = self.curr_data[len_length + 1: self.curr_len + len_length + 1]
            if(self.curr_cmd not in GBridge.cmd_lens.keys()):
                return True
        self.total_len += 2
        if(len(self.curr_data) > self.curr_len + len_length + 2):
            self.checksum = int.from_bytes(self.curr_data[self.curr_len + len_length + 1 : self.curr_len + len_length + 1 + 2], byteorder='big')
            self.checksum_okay = GBridge.calc_checksum(self.final_data) == self.checksum
            return True
        return None
    
    def calc_checksum(data):
        checksum = 0
        for i in range(len(data)):
            checksum += data[i]
            checksum &= 0xFFFF
        return checksum
    
    def reset_cmd(self):
        self.total_len = 0
        self.curr_cmd = None
        self.curr_index = 0
        self.curr_len = 0
        self.final_data = []
        self.curr_data = []
        self.checksum = 0
        self.checksum_okay = None

class GBridgeDebugCommands:
    SEND_EEPROM_CMD = 1
    UPDATE_EEPROM_CMD = 2
    UPDATE_RELAY_CMD = 3
    UPDATE_RELAY_TOKEN_CMD = 4
    UPDATE_DNS1_CMD = 5
    UPDATE_DNS2_CMD = 6
    UPDATE_P2P_PORT_CMD = 7
    UPDATE_DEVICE_CMD = 8
    SEND_NAME_INFO_CMD = 9
    SEND_OTHER_INFO_CMD = 10
    STOP_CMD = 11
    START_CMD = 12
    STATUS_CMD = 13
    SEND_NUMBER_OWN_CMD = 14
    SEND_NUMBER_OTHER_CMD = 15
    SEND_RELAY_TOKEN_CMD = 16
    SET_SAVE_STYLE_CMD = 17
    FORCE_SAVE_CMD = 18

    CMD_DEBUG_INFO_CFG = 0x01
    CMD_DEBUG_INFO_NAME = 0x02
    CMD_DEBUG_INFO_OTHER = 0x03
    CMD_DEBUG_INFO_STATUS = 0x04
    CMD_DEBUG_INFO_NUMBER = 0x05
    CMD_DEBUG_INFO_NUMBER_PEER = 0x06
    CMD_DEBUG_INFO_RELAY_TOKEN = 0x07
    
    CMD_DEBUG_LOG_IN = 0x01
    CMD_DEBUG_LOG_OUT = 0x02
    CMD_DEBUG_LOG_TIME_TR = 0x03
    CMD_DEBUG_LOG_TIME_AC = 0x04
    CMD_DEBUG_LOG_TIME_IR = 0x05

    MAXIMUM_LENGTH = 0x40 - 1 - 1 - 2

    def load_command(command_id, data):
        if command_id not in GBridgeDebugCommands.command_methods.keys():
            return [], 0

        ack_wanted = 0
        if command_id in GBridgeDebugCommands.wants_ack:
            ack_wanted = 1
        num_acks = 0
        total_data = GBridgeDebugCommands.command_methods[command_id](command_id, data)
        num_iters = int((len(total_data) + GBridgeDebugCommands.MAXIMUM_LENGTH - 1) / GBridgeDebugCommands.MAXIMUM_LENGTH)
        final_data = []
        if(len(total_data) == 0):
            num_iters = 1
        for i in range(num_iters):
            left_side = i * GBridgeDebugCommands.MAXIMUM_LENGTH
            right_side = (i + 1) * GBridgeDebugCommands.MAXIMUM_LENGTH
            if right_side > len(total_data):
                right_side = len(total_data)
            curr_data = [command_id] + total_data[left_side : right_side]
            checksum = GBridge.calc_checksum(curr_data)
            final_data += curr_data + list(checksum.to_bytes(2, byteorder='big'))
            num_acks += ack_wanted
        return final_data, ack_wanted

    def single_command(command_id, data):
        return []

    def byte_command(command_id, data):
        return [data & 0xFF]

    def unsigned_command(command_id, data):
        return [(data >> 8) & 0xFF, data & 0xFF]

    def send_dump_command(command_id, data):
        return GBridgeDebugCommands.prepare_offsetted_data(list(data))

    def prepare_offsetted_data(data):
        total_data = []
        single_split_length = GBridgeDebugCommands.MAXIMUM_LENGTH - 2
        num_iters = int((len(data) + single_split_length - 1) / single_split_length)
        for i in range(num_iters):
            left_side = i * single_split_length
            right_side = (i + 1) * single_split_length
            if right_side > len(data):
                right_side = len(data)
            total_data += list(left_side.to_bytes(2, byteorder='big')) + data[left_side : right_side]
        return total_data

    def send_preprocessed_data(command_id, data):
        return list(data)

    def to_implement(command_id, data):
        print("NOT IMPLEMENTED: " + command_id)
        return []
    
    command_methods = {
        SEND_EEPROM_CMD: single_command,
        UPDATE_EEPROM_CMD: send_dump_command,
        UPDATE_RELAY_CMD: send_preprocessed_data,
        UPDATE_RELAY_TOKEN_CMD: send_preprocessed_data,
        UPDATE_DNS1_CMD: send_preprocessed_data,
        UPDATE_DNS2_CMD: send_preprocessed_data,
        UPDATE_P2P_PORT_CMD: unsigned_command,
        UPDATE_DEVICE_CMD: byte_command,
        SEND_NAME_INFO_CMD: single_command,
        SEND_OTHER_INFO_CMD: single_command,
        STOP_CMD: single_command,
        START_CMD: single_command,
        STATUS_CMD: single_command,
        SEND_NUMBER_OWN_CMD: single_command,
        SEND_NUMBER_OTHER_CMD: single_command,
        SEND_RELAY_TOKEN_CMD: single_command,
        SET_SAVE_STYLE_CMD: byte_command,
        FORCE_SAVE_CMD: single_command
    }
    
    auto_unsigned_values = {
        UPDATE_RELAY_CMD: 31227,
        UPDATE_DNS1_CMD: 53,
        UPDATE_DNS2_CMD: 53,
        UPDATE_P2P_PORT_CMD: 1027
    }
    
    wants_ack = {
        UPDATE_EEPROM_CMD,
        UPDATE_RELAY_CMD,
        UPDATE_RELAY_TOKEN_CMD,
        UPDATE_DNS1_CMD,
        UPDATE_DNS2_CMD,
        UPDATE_P2P_PORT_CMD,
        UPDATE_DEVICE_CMD,
        STOP_CMD,
        START_CMD,
        SET_SAVE_STYLE_CMD,
        FORCE_SAVE_CMD
    }

class GBridgeSocket:
    MOBILE_SOCKTYPE_TCP = 0
    MOBILE_SOCKTYPE_UDP = 1
    MOBILE_ADDRTYPE_NONE = 0
    MOBILE_ADDRTYPE_IPV4 = 1
    MOBILE_ADDRTYPE_IPV6 = 2
    MOBILE_MAX_CONNECTIONS = 2
    
    def read_addr(data):
        if len(data) < 1:
            return None
        type_conn = data[0]
        type_conn_id = None
        if(type_conn != GBridgeSocket.MOBILE_ADDRTYPE_IPV4) and (type_conn != GBridgeSocket.MOBILE_ADDRTYPE_IPV6):
            return None
        if(type_conn == GBridgeSocket.MOBILE_ADDRTYPE_IPV4):
            if len(data) < 7:
                return None
            type_conn_id = socket.AF_INET
        if(type_conn == GBridgeSocket.MOBILE_ADDRTYPE_IPV6):
            if len(data) < 19:
                return None
            type_conn_id = socket.AF_INET6
        port = (data[1] << 8) | data[2]
        address = socket.inet_ntop(type_conn_id, bytes(data[3:]))
        if (type_conn == GBridgeSocket.MOBILE_ADDRTYPE_IPV4):
            return (address, port)
        else:
            return (address, port, 0, 0)
    
    def write_addr(data, port=None):
        type_conn = GBridgeSocket.MOBILE_ADDRTYPE_NONE
        type_conn_id = None
        if data is not None:
            if(len(data) == 2):
                type_conn = GBridgeSocket.MOBILE_ADDRTYPE_IPV4
                type_conn_id = socket.AF_INET
            if(len(data) == 4):
                type_conn = GBridgeSocket.MOBILE_ADDRTYPE_IPV6
                type_conn_id = socket.AF_INET6

        if type_conn == GBridgeSocket.MOBILE_ADDRTYPE_NONE:
            return [type_conn]
        
        if port is None:
            port = data[1]
        out_bytes = [type_conn, (port >> 8) & 0xFF, port & 0xFF]
        out_bytes += socket.inet_pton(type_conn_id, data[0])
        return out_bytes

    def parse_unsigned(command, token):
        if token is None:
            return None

        value = None
        try:
            value = int(token.upper().strip())
        except:
            pass
        if (token.upper().strip() == "AUTO") and (command in GBridgeDebugCommands.auto_unsigned_values.keys()):
            value = GBridgeDebugCommands.auto_unsigned_values[command]
        if value is not None:
            if (value < 0) or (value >= 65536):
                value = None
        return value

    def parse_addr(command, tokens):
        if tokens is None:
            return None
        if len(tokens) <= 0:
            return None
        
        port_str = tokens[0].upper().strip()
        type_conn = GBridgeSocket.MOBILE_ADDRTYPE_NONE
        port = GBridgeSocket.parse_unsigned(command, port_str)
        if port_str == "NULL":
            port = -1
        if port is not None:
            if port == -1:
                return [type_conn]
            address = None
            if len(tokens) > 1:
                address_str = tokens[1].lower().strip()
                try:
                    address = socket.inet_pton(socket.AF_INET, address_str)
                    type_conn = GBridgeSocket.MOBILE_ADDRTYPE_IPV4
                except:
                    pass
                if address is None:
                    try:
                        address = socket.inet_pton(socket.AF_INET6, address_str)
                        type_conn = GBridgeSocket.MOBILE_ADDRTYPE_IPV6
                    except:
                        pass
            if address is not None:
                return [type_conn, (port >> 8) & 0xFF, port & 0xFF] + list(address)
        return None

    def __init__(self):
        self.debug_prints = True
        self.conn_data_last = None
        self.print_exception = True
        self.connect_socket = []
        self.socket = []
        self.socket_type = []
        self.socket_addrtype = []
        for i in range(GBridgeSocket.MOBILE_MAX_CONNECTIONS):
            self.socket += [None]
            self.connect_socket += [None]
            self.socket_type += [None]
            self.socket_addrtype += [None]
    
    def open(self, data):
        if self.debug_prints:
            print("OPEN")
        if(len(data) < 5):
            return False

        conn = data[0]
        conn_type = data[1]
        addrtype = data[2]
        bindport = (data[3] << 8) | (data[4])
        
        if conn >= GBridgeSocket.MOBILE_MAX_CONNECTIONS:
            return False
        
        if self.socket[conn] is not None:
            return False

        try:
            sock_type = None
            if(conn_type == GBridgeSocket.MOBILE_SOCKTYPE_TCP):
                sock_type = socket.SOCK_STREAM
            if(conn_type == GBridgeSocket.MOBILE_SOCKTYPE_UDP):
                sock_type = socket.SOCK_DGRAM

            sock_addrtype = None
            if(addrtype == GBridgeSocket.MOBILE_ADDRTYPE_IPV4):
                sock_addrtype = socket.AF_INET
            if(addrtype == GBridgeSocket.MOBILE_ADDRTYPE_IPV6):
                sock_addrtype = socket.AF_INET6
            
            if sock_type is None:
                return False
            if sock_addrtype is None:
                return False

            sock = socket.socket(sock_addrtype, sock_type)
            sock.setblocking(False)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            if(conn_type == GBridgeSocket.MOBILE_SOCKTYPE_TCP):
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

            sock.bind(('', socket.htons(bindport)))
        except Exception as e:
            if self.print_exception:
                print(e)
            return False

        self.socket[conn] = sock;
        self.socket_type[conn] = sock_type
        self.socket_addrtype[conn] = sock_addrtype
        return True;
    
    def close(self, data):
        if self.debug_prints:
            print("CLOSE")
        if(len(data) < 1):
            return False

        conn = data[0]
        
        if conn >= GBridgeSocket.MOBILE_MAX_CONNECTIONS:
            return False
        
        if self.socket[conn] is None:
            return False

        #self.socket[conn].shutdown(socket.SHUT_RDWR)
        self.socket[conn].close()

        self.socket[conn] = None;
        self.socket_type[conn] = None
        self.socket_addrtype[conn] = None
        return True;
    
    def connect(self, data):
        if self.debug_prints:
            print("CONNECT")
        if(len(data) < 2):
            return -1

        conn = data[0]
        
        if conn >= GBridgeSocket.MOBILE_MAX_CONNECTIONS:
            return -1
        
        if self.socket[conn] is None:
            return -1
        
        conn_data = GBridgeSocket.read_addr(data[1:])
        if conn_data is None:
            return -1
        
        done = False
        while not done:
            try:
                self.socket[conn].connect(conn_data)
                self.connect_socket[conn] = conn_data
                done = True
            except Exception as e:
                processed = False
                if isinstance(e, socket.error):
                    if e.errno == errno.EINPROGRESS:
                        sleep(0.05)
                        processed = True
                    if e.errno == errno.EALREADY:
                        sleep(0.05)
                        processed = True
                if not processed:
                    if self.print_exception:
                        print(e)
                    return -1
        return 1
    
    def listen(self, data):
        if self.debug_prints:
            print("LISTEN")
        if(len(data) < 1):
            return False

        conn = data[0]
        
        if conn >= GBridgeSocket.MOBILE_MAX_CONNECTIONS:
            return False
        
        if self.socket[conn] is None:
            return False
        
        try:
            self.socket[conn].listen(1)
        except Exception as e:
            if self.print_exception:
                print(e)
            return False
        return True
    
    def accept(self, data):
        if self.debug_prints:
            print("ACCEPT")
        if(len(data) < 1):
            return False

        conn = data[0]
        
        if conn >= GBridgeSocket.MOBILE_MAX_CONNECTIONS:
            return False
        
        if self.socket[conn] is None:
            return False
        
        try:
            new_sock = self.socket[conn].accept()
            new_sock.setblocking(False)
            #self.socket[conn].shutdown(socket.SHUT_RDWR)
            self.socket[conn].close()
            self.socket[conn] = new_sock
        except Exception as e:
            if self.print_exception:
                print(e)
            return False
        return True
    
    def send(self, data, stream):
        if self.debug_prints:
            print("SEND")
        if(len(data) < 2):
            return -1
        
        conn = data[0]
        
        if conn >= GBridgeSocket.MOBILE_MAX_CONNECTIONS:
            return -1
        
        if self.socket[conn] is None:
            return -1
        
        conn_data = GBridgeSocket.read_addr(data[1:])
        #if conn_data is None:
        #    conn_data = self.connect_socket[conn]
        
        try:
            if conn_data is None:
                sent = self.socket[conn].send(bytes(stream), 0)
            else:
                sent = self.socket[conn].sendto(bytes(stream), 0, conn_data)
        except Exception as e:
            if self.print_exception:
                print(e)
            return -1
        return int(sent)
    
    def run_recv(self, data):
        if(len(data) < 4):
            return 0

        size = (data[1] << 8) | data[2]
        
        conn = data[0]
        
        is_valid = data[3] == 1
        
        if conn >= GBridgeSocket.MOBILE_MAX_CONNECTIONS:
            return 0
        
        if self.socket[conn] is None:
            return 0

        try:
            if is_valid:
                result = self.socket[conn].recvfrom(size)
            else:
                result = self.socket[conn].recvfrom(1, socket.MSG_PEEK)
            data_recv = list(result[0])
            source_recv = result[1]
            # Make sure at least one byte is in the buffer
            if(len(data_recv) == 0):
                if self.socket_type[conn] == socket.SOCK_STREAM:
                    return -2
        except Exception as e:
            processed = False
            if isinstance(e, socket.error):
                if e.errno == errno.EWOULDBLOCK:
                    return 0
                    processed = True
            if not processed:
                if self.print_exception:
                    print(e)
                return -1

        if not is_valid:
            return [[], [(len(data_recv) >> 8) & 0xFF, len(data_recv) & 0xFF] + GBridgeSocket.write_addr(source_recv)]
        return [data_recv, [(len(data_recv) >> 8) & 0xFF, len(data_recv) & 0xFF] + GBridgeSocket.write_addr(source_recv)]
    
    def recv(self, data):
        if self.debug_prints:
            print("RECV")
        result = self.run_recv(data)
        try:
            data = result[0]
            rest = result[1]
        except TypeError:
            data = []
            rest = [(result >> 8) & 0xFF, result & 0xFF] + GBridgeSocket.write_addr([])
        return [data, rest]

