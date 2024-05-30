import socket

# 创建socket对象
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# 设置服务器地址和端口
server_address = ('localhost', 12345)
print(f"Starting UDP server at {server_address[0]}:{server_address[1]}")

# 绑定到地址和端口
sock.bind(server_address)

while True:
    # 接收数据，buffer size为1024字节
    data, address = sock.recvfrom(5120)
    
    # 打印接收到的数据和发送者地址
    print(f"Received {len(data)} from {address}")
    
    # 可以在这里处理接收到的数据
    # ...

# 关闭socket
sock.close()