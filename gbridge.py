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
            print(bytes(self.data).decode('utf-8'), end='')
        if self.upper_cmd == GBridge.GBRIDGE_CMD_DEBUG_CHAR:
            print(GBridgeCommand.prepare_hex_list_str(self.data))
        if self.upper_cmd == GBridge.GBRIDGE_CMD_DEBUG_CFG:
            print(GBridgeCommand.prepare_hex_list_str(self.data))
    
    def prepare_str_out(self, size_entry):
        str_out = ""
        for i in range(int(len(self.data) / size_entry)):
            str_out += str(int.from_bytes(self.data[i * size_entry: (i + 1) * size_entry], byteorder='little')) + "\n"
        return str_out
    
    def check_save(self, save_requests):
        if (self.upper_cmd in save_requests.keys()) and (save_requests[self.upper_cmd] != ""):
            single_byte_saves = {GBridge.GBRIDGE_CMD_DEBUG_CFG, GBridge.GBRIDGE_CMD_DEBUG_LOG_IN, GBridge.GBRIDGE_CMD_DEBUG_LOG_OUT}
            uint8_t_saves = {}
            uint16_t_saves = {GBridge.GBRIDGE_CMD_DEBUG_TIME_TR, GBridge.GBRIDGE_CMD_DEBUG_TIME_AC}
            uint32_t_saves = {}
            uint64_t_saves = {}
            if self.upper_cmd in single_byte_saves:
                with open(save_requests[self.upper_cmd], "wb") as f:
                    f.write(bytes(self.data))
                save_requests[self.upper_cmd] = ""
            if self.upper_cmd in uint8_t_saves:
                str_out = self.prepare_str_out(1)
                with open(save_requests[self.upper_cmd], "w") as f:
                    f.write(str_out)
                save_requests[self.upper_cmd] = ""
            if self.upper_cmd in uint16_t_saves:
                str_out = self.prepare_str_out(2)
                with open(save_requests[self.upper_cmd], "w") as f:
                    f.write(str_out)
                save_requests[self.upper_cmd] = ""
            if self.upper_cmd in uint32_t_saves:
                str_out = self.prepare_str_out(4)
                with open(save_requests[self.upper_cmd], "w") as f:
                    f.write(str_out)
                save_requests[self.upper_cmd] = ""
            if self.upper_cmd in uint64_t_saves:
                str_out = self.prepare_str_out(8)
                with open(save_requests[self.upper_cmd], "w") as f:
                    f.write(str_out)
                save_requests[self.upper_cmd] = ""
    
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
    GBRIDGE_CMD_DEBUG_CFG = 0x05
    GBRIDGE_CMD_DATA = 0x0A
    GBRIDGE_CMD_STREAM = 0x0C
    GBRIDGE_CMD_DEBUG_LOG_IN = 0x15
    GBRIDGE_CMD_DEBUG_LOG_OUT = 0x16
    GBRIDGE_CMD_DEBUG_TIME_TR = 0x17
    GBRIDGE_CMD_DEBUG_TIME_AC = 0x18
    GBRIDGE_CMD_DATA_PC = 0x4A
    GBRIDGE_CMD_STREAM_PC = 0x4C
    GBRIDGE_CMD_REPLY_F = 0x80
    
    no_reply_upper_cmds = {GBRIDGE_CMD_DEBUG_LINE, GBRIDGE_CMD_DEBUG_CHAR, GBRIDGE_CMD_DEBUG_CFG, GBRIDGE_CMD_DEBUG_LOG_IN, GBRIDGE_CMD_DEBUG_LOG_OUT, GBRIDGE_CMD_DEBUG_TIME_TR, GBRIDGE_CMD_DEBUG_TIME_AC}
    
    cmd_lens = {
        GBRIDGE_CMD_DATA: 1,
        GBRIDGE_CMD_STREAM: 2,
        GBRIDGE_CMD_DEBUG_LINE: 2,
        GBRIDGE_CMD_DEBUG_CHAR: 2,
        GBRIDGE_CMD_DEBUG_CFG: 2,
        GBRIDGE_CMD_DEBUG_LOG_IN: 2,
        GBRIDGE_CMD_DEBUG_LOG_OUT: 2,
        GBRIDGE_CMD_DEBUG_TIME_TR: 2,
        GBRIDGE_CMD_DEBUG_TIME_AC: 2
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
        self.total_len += len_length + self.curr_len
        if(len(self.curr_data) > self.curr_len + len_length):
            self.final_data = self.curr_data[len_length + 1: self.curr_len + len_length + 1]
            if(len_length == 0):
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
    
    def write_addr(data):
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
        
        out_bytes = [type_conn, (data[1] >> 8) & 0xFF, data[1] & 0xFF]
        out_bytes += socket.inet_pton(type_conn_id, data[0])
        return out_bytes

    def __init__(self):
        self.debug_prints = False
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
            result = self.socket[conn].recvfrom(size)
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

