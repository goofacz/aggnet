import socket
import select


def process_client(address):
    client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client.connect(address)

    with open('/dev/aggnet0', 'wb+') as aggnet:
        while True:
            sockets = [aggnet, client]
            inputs, outputs, exceptions = select.select(sockets, sockets, sockets)

            if aggnet in inputs:
                pass
            if client in inputs:
                pass
            if aggnet in outputs:
                pass
            if client in outputs:
                pass

def main():
    process_client(('localhost', 20000))

if __name__ == '__main__':
    main()
