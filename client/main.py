import socket
import select


def process_client(address):
    client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client.connect(address)

    with open('/dev/aggnet0', 'wb+') as aggnet:
        inputs = [aggnet, client]
        outputs = []
        exceptions = []

        while True:
            inputs, outputs, exceptions = select.select(inputs, outputs, exceptions)

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
