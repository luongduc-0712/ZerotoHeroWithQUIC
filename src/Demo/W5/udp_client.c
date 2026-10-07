#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h> // Ham close()
#include <netinet/in.h> // Thu vien chua htons, htonl, IPv4, IPv6,...
#include <arpa/inet.h> // Chua cac ham inet_pton() inet_ntop()

#define BUFFER_SIZE 1024
#define PORT_CLIENT 9090
#define PORT_SERVER 5000
#define SERVER_IP "127.0.0.1"
#define CLIENT_IP "0.0.0.0"

int main() {
        int client_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (client_fd == -1){
                perror("socket");
                return 1;
        }

        struct sockaddr_in client_addr = {0};
        client_addr.sin_family = AF_INET;
        client_addr.sin_port = htons(PORT_CLIENT);
        int result = inet_pton(
                AF_INET,
                CLIENT_IP,
                &client_addr.sin_addr
        );
        if (result == -1) {
                perror("inet_pton");
                close(client_fd);
                return 1;
        }

        result = bind(
                client_fd,
                (const struct sockaddr*)&client_addr,
                sizeof(client_addr)
        );

        if (result == -1) {
                perror("bind");
                close(client_fd);
                return 1;
        }

        printf("Server IP = %s\n", SERVER_IP);

	char buffer[BUFFER_SIZE];
	struct sockaddr_in server_addr = {0};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT_SERVER);
	result = inet_pton(
		AF_INET,
		SERVER_IP,
		&server_addr.sin_addr
	);

	if (result == -1) {
		perror("inet_pton");
		close(client_fd);
		return 1;
	}

	if (result == 0) {
		fprintf(stderr, "Invalid server IP\n");
		close(client_fd);
		return 1;
	}

	while (1) {
		printf("Message: ");
		if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
			break;
		}
		if (strcmp(buffer,"exit\n") == 0 || strcmp(buffer, "exit") == 0) {
			break;
		}
		ssize_t sent = sendto(
				client_fd,
				buffer,
				strlen(buffer),
				0,
				(const struct sockaddr*)&server_addr, sizeof(server_addr)
		);

		if (sent == -1) {
			perror("sendto");
		} else {
			fprintf(stderr,"Sent %zd bytes\n", sent);
		}
	}
}
