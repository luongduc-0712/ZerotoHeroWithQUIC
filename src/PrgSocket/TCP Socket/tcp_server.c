#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(){
	printf("Process ID: %ld\n",(long)getpid());

	// Tao socket listen
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		perror("socket");
		close(server_fd);
		return 1;
	}
	printf("Server FD = %d\n", server_fd);

	// Gan cac dia chi cuc bo ID cua may va Port cua chuong trinh cho socket
	struct sockaddr_in server_addr = {0};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(9090);
	server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	int result = bind(
			server_fd,
			(const struct sockaddr*) &server_addr,
			sizeof(server_addr);
			);
	if (result == -1) {
		perror("bind");
		close(server_fd);
		return 1;
	};

	// Cho phep socket cua Server bat dau tiep nhan cac ket noi tu Client
	int result = listen(server_fd, 5);
	if (result == -1) {
		perror("listen");
		close(server_fd);
		return 1;
	};

	// Socket yeu cau kernel lay mot ket noi da duoc thiet lap va tra ve connected soket
	struct sockaddr_in client_addr = {0};
	socklen_t client_len = sizeof(client_addr);

	int client_fd = accept(
		server_fd,
		(struct sockaddr*) &client_addr,
		&client_len
	);

	char client_ip[INET_ADDRSTRLEN];
	if (inet_ntop(
			AF_INET,
			&client_addr.sin_addr,
			client_ip,
			sizeof(client_ip)) == NULL) {
		perror("inet_ntop");
		close(client_fd);
		close(server_fd);
		return 1;
	}

	printf("Client connected!\n");
	printf("Client IP: %s\n", client_ip);
	printf("CLient Port: %u\n", (unsigned)ntohs(client_addr.sin_port));
	// Nhan du lieu tu Client
	char buffer = [1024];
	while (1){
		ssize_t 
	}
};














