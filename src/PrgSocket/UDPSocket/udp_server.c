#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h> // Ham close()
#include <netinet/in.h> // Thu vien chua htons, htonl, IPv4, IPv6,...
#include <arpa/inet.h> // Chua cac ham inet_pton() inet_ntop()

#define BUFFER_SIZE 1024
#define PORT_SERVER 5000
#define PORT_CLIENT 9090
#define SERVER_IP "127.0.0.1"
#define CLIENT_IP "0.0.0.0"

int main() {
	int server_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (server_fd == -1){
		perror("socket");
		return 1;
	}

	struct sockaddr_in server_addr = {0};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT_SERVER);
	int result = inet_pton(
		AF_INET,
		SERVER_IP,
		&server_addr.sin_addr
	);
	if (result == -1) {
		perror("inet_pton");
		close(server_fd);
		return 1;
	}

	result = bind(
		server_fd,
		(const struct sockaddr*)&server_addr,
		sizeof(server_addr)
	);

	if (result == -1) {
		perror("bind");
		close(server_fd);
		return 1;
	}

	printf("Server IP = %s\n", SERVER_IP);

	struct sockaddr_in client_addr = {0};
	client_addr.sin_family = AF_INET;
	client_addr.sin_port = htons(PORT_CLIENT);
	result = inet_pton(
		AF_INET,
		CLIENT_IP,
		&client_addr.sin_addr
	);

	if (result == -1) {
		perror("inet_pton");
		close(server_fd);
		return 1;
	}

	char buffer[BUFFER_SIZE];
	socklen_t client_len = sizeof(client_addr);

	while(1) {
		ssize_t received = recvfrom(
			server_fd,
			buffer,
			sizeof(buffer) - 1,
			0,
			(struct sockaddr*)&client_addr,
			&client_len
		);
		if (received == -1) {
			perror("recv");
		} else {
			buffer[received] = '\0';
			fprintf(stderr,"Received %zd bytes \n", received);
		}
		printf("Message: %s", buffer);
	}
}

