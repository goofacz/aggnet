import socket
import select


def process_client(clinet):
    with open('dev/aggnet0', 'rw') as aggnet:
        while True:
            sockets = [aggnet, client]
            inputs, outputs, exceptions = select.select(sockets, sockets, sockets)

            if aggnet in inputs:
                pass
            if aggnet in inputs:
                pass
            if client in outputs:
                pass
            if client in outputs:
                pass

def process_server(port):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.bind(('localhost', port))
        server.listen(1)

        while True:
            with server.accept() as client:
                process_client(client)

def main():
    process_server(20000)

if __name__ == '__main__':
    main()
