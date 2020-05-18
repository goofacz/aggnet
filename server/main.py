import socket
import select


def process_client(client):
    with open('/dev/aggnet0', 'wb+') as aggnet:
        while True:
            sockets = [client, aggnet]
            inputs, outputs, exceptions = select.select(sockets, sockets, sockets)

            if client in inputs:
                pass
            if client in outputs:
                pass
            if client in exceptions:
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
