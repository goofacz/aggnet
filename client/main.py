import socket
import select
import struct


def aggnet_read(handle, out_frames):
    ignored_dest_macs = [b'\xff\xff\xff\xff\xff\xff', b'\x33\x33\x00\x00\x00\x01', b'\x33\x33\x00\x00\x00\x02']

    length = struct.unpack('i', handle.read(4))[0]
    frame = handle.read(length)
    if frame[0:6] in ignored_dest_macs:
        return ()

    out_frames.append(struct.pack('i', length))
    out_frames.append(frame)


def client_send(client, out_frames):
    data = out_frames.pop(0)
    sent = client.send(data)
    if sent != len(data):
        raise RuntimeError(f'Partial send over TCP! (sent {sent}, expected {len(data)})');


def process_client(address):
    out_frames = []

    client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client.connect(address)

    with open('/dev/aggnet0', 'wb+') as aggnet:
        inputs = [aggnet]
        outputs = []
        exceptions = []

        while True:
            inputs, outputs, exceptions = select.select(inputs, outputs, exceptions)

            if aggnet in inputs:
                aggnet_read(aggnet, out_frames)
            if client in inputs:
                pass
            if aggnet in outputs:
                pass
            if client in outputs:
                client_send(client, out_frames)

            inputs = [aggnet]
            outputs = []

            if len(out_frames) > 0:
                outputs.append(client)


def main():
    process_client(('localhost', 20000))

if __name__ == '__main__':
    main()
