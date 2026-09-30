#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 9090
#define BUFFER_SIZE 1024

int main(void)
{
    // 1. Create TCP socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1) {
        perror("socket");
        return 1;
    }

    // 2. Configure server address
    struct sockaddr_in server_addr = {0};

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // 3. Bind socket to IP and port
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    // 4. Listen for incoming connections
    if (listen(server_fd, 5) == -1) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Server listening on 127.0.0.1:%d\n", PORT);

    // 5. Prepare client address
    struct sockaddr_in client_addr = {0};
    socklen_t client_len = sizeof(client_addr);

    // 6. Accept one client connection
    printf("Waiting for client...\n");

    int client_fd = accept(
        server_fd,
        (struct sockaddr *)&client_addr,
        &client_len
    );

    if (client_fd == -1) {
        perror("accept");
        close(server_fd);
        return 1;
    }

    // 7. Display client information
    char client_ip[INET_ADDRSTRLEN];

    if (inet_ntop(AF_INET,
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
    printf("Client port: %u\n",
           (unsigned)ntohs(client_addr.sin_port));

    printf("Server FD: %d\n", server_fd);
    printf("Client FD: %d\n", client_fd);

    // 8. Receive and echo messages
    char buffer[BUFFER_SIZE];

    while (1) {
        ssize_t bytes_received = recv(
            client_fd,
            buffer,
            sizeof(buffer),
            0
        );

        if (bytes_received < 0) {
            perror("recv");
            break;
        }

        if (bytes_received == 0) {
            printf("Client disconnected.\n");
            break;
        }

        printf("Received %zd bytes: ",
               bytes_received);

        fwrite(buffer, 1, (size_t)bytes_received, stdout);
        printf("\n");

        // Send all received bytes back to client
        size_t total_sent = 0;
        size_t total_bytes = (size_t)bytes_received;

        while (total_sent < total_bytes) {
            ssize_t sent = send(
                client_fd,
                buffer + total_sent,
                total_bytes - total_sent,
                0
            );

            if (sent <= 0) {
                if (sent < 0) {
                    perror("send");
                } else {
                    fprintf(stderr, "send returned 0\n");
                }
                goto cleanup;
            }

            total_sent += (size_t)sent;
        }
    }

cleanup:
    // 9. Close sockets
    close(client_fd);
    close(server_fd);

    printf("Server stopped.\n");

    return 0;
}