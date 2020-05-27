import socket
import select
import struct


def aggnet_read(handle):
    ignored_dest_macs = [b'\xff\xff\xff\xff\xff\xff', b'\x33\x33\x00\x00\x00\x01', b'\x33\x33\x00\x00\x00\x02']

    length = struct.unpack('i', handle.read(4))[0]
    frame = handle.read(length)
    if frame[0:6] in ignored_dest_macs:
        return


def client_read(client, in_frames):
    length_bytes = client.recv(4)
    if len(length_bytes) != 4:
        raise RuntimeError(f'Partial read on TCP (read {len(length_bytes)}, expected: {4})!');

    length = struct.unpack('i', length_bytes)[0]
    frame = client.recv(length)
    if len(frame) != length:
        raise RuntimeError(f'Partial read on TCP (read {len(frame)}, expected: {length})!');

    in_frames.append(length_bytes)
    in_frames.append(frame)


def process_client(client):
    with open('/dev/aggnet0', 'r+b') as aggnet:
        in_frames = []

        inputs = [client]
        outputs = []
        exceptions = []

        while True:
            inputs, outputs, exceptions = select.select(inputs, outputs, exceptions)

            if client in inputs:
                client_read(client, in_frames)
            if client in outputs:
                pass
            if client in exceptions:
                pass
            if aggnet in inputs:
                aggnet_read(aggnet)
            if aggnet in outputs:
                pass

            inputs = [client]
            outputs = []

            if len(in_frames) > 0:
                outputs.append(aggnet)


def process_server(port):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(('localhost', port))
        server.listen(1)

        while True:
            try:
                client, _ = server.accept()
                process_client(client)
            except Exception as error:
                print(error)


def main():
    process_server(20000)

if __name__ == '__main__':
    main()
