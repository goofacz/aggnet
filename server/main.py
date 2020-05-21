import socket
import select
import struct

def aggnet_read(handle):
    ignored_dest_macs = [b'\xff\xff\xff\xff\xff\xff', b'\x33\x33\x00\x00\x00\x01', b'\x33\x33\x00\x00\x00\x02']

    length = struct.unpack('i', handle.read(4))[0]
    frame = handle.read(length)
    if frame[0:6] in ignored_dest_macs:
        print('drop frame!')

def process_client(client):
    with open('/dev/aggnet0', 'r+b') as aggnet:
        inputs = [aggnet, client]
        outputs = []
        exceptions = []

        while True:
            inputs, outputs, exceptions = select.select(inputs, outputs, exceptions)

            if client in inputs:
                print('client in')
                pass
            if client in outputs:
                print('client out')
                pass
            if client in exceptions:
                print('client ex')
                pass
            if aggnet in inputs:
                aggnet_read(aggnet)
                print('aggnet in')
            if aggnet in outputs:
                print('aggnet out')
                pass

def process_server(port):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(('localhost', port))
        server.listen(1)

        while True:
            client, _ = server.accept()
            process_client(client)

def main():
    process_server(20000)

if __name__ == '__main__':
    main()
