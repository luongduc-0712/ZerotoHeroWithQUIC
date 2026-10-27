#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(){
	printf("Process IP Client: %ld\n", (long)getpid());

	//Tao socket de ket noi
	int client_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (client_fd == -1) {
		perror("socket");
		return 1;
	};

	//Yeu cau ket noi voi Server
	struct sockaddr_in server_addr = {0};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(9090);
	int result = inet_pton (
		AF_INET,
		"127.0.0.1",
		&server_addr.sin_ip
	);

	if (result == -1) {
		perror("inet_pton");
		close(client_fd);
		return 1;
	};

	int result = connect(
		client_fd.
		(const struct sockaddr*) &server_addr,
		sizeof(server_addr)
	);

	// Gui du lieu den Server
	char buffer = [1024];
	// hien thi -> nhap du lieu tu ban phim ->  send to server -> recv
	while (1) {
		printf("\nMessage: ");
		ffplush(stdout);
		if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
			break;
		}

		if (strcmp (buffer, "exit\n") == 0 || strcmp (buffer, "exit")) {
			break;
		}

		size_t message_len = strlen(buffer);
		size_t total_sent = 0;

		while (total_sent < message_len) {
			ssize_t sent = send (
				client_fd,
				buffer + total_sent,
				message_len - total_sent,
				0
			);
		if (sent <= 0) {
			if (sent < 0) {
				perror("send");
			} else {
				fprintf(stderr, "send return 0\n");
			}
			close(client_fd);
			printf("CLient Stopped!\n");
			return 0;
		}

		total_send += (size_t)sent;
		}

		size_t total_received = 0;

		while (total_received < message_len) {
			ssize_t received = recv(
				client_fd,
				buffer + total_received,
				message_len - total_received,
				0
			);
		}
		if (sent <= 0) {
                        if (sent < 0) {
                                perror("recv");
                        } else {
                                fprintf(stderr, "Server disconnected 0\n");
                        }
                        close(client_fd);
                        printf("Client stoped!\n");
			return 0;
                }
		total_received +=(size_t)received;
	}
	printf("Server echoed: ");
	fwrive(buffer, 1, total_received. stdout);

}

























4
