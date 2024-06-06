import socket
import signal
import struct
import sys
import time
from collections import defaultdict

run = True
mem_tracer = {}

class MemoryItem:
    def __init__(self, addr, size, call_stack):
        self.addr = addr
        self.size = size
        self.call_stack = call_stack

def catch_signal(signum, frame):
    global run
    run = False

signal.signal(signal.SIGINT, catch_signal)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
server_address = ('', int(sys.argv[1]))
print(f"Starting UDP server at {server_address[0]}:{server_address[1]}")

sock.bind(server_address)
sock.setblocking(False)

while run:
    try:
        data, address = sock.recvfrom(5120)
        print(f"Received {len(data)} from {address}")
        mem_addr, mem_size, *call_stack = struct.unpack('=QQ20Q', data)
        if mem_size == 0:
            if mem_addr in mem_tracer:
                del mem_tracer[mem_addr]
            else:
                print(f"not find address: 0x{mem_addr: x}")
        else:
            item = MemoryItem(mem_addr, mem_size, call_stack)
            mem_tracer[mem_addr] = item
    except BlockingIOError as e:
        time.sleep(1)


print("exit recv data, now summary memory...")

leak_size = 0
for item in mem_tracer.values():
    leak_size += item.size

print(f"memory leak size: {leak_size}")

if leak_size > 0:
    d = defaultdict(list)
    for key, item in mem_tracer.items():
        d[str(item.call_stack)].append(item)

    for leak_list in d.values():
        leak_size = 0
        for item in leak_list:
            leak_size += item.size
        print(f"leak {leak_size} on: \n")
        for addr in leak_list[0].call_stack:
            print(f"\t0x{addr:x}")


# 关闭socket
sock.close()