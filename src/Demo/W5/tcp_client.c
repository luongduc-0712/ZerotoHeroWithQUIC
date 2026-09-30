
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define PORT 9090
#define BUFFER_SIZE 1024

int main(void)
{
    // 1. Create TCP socket
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (client_fd == -1) {
        perror("socket");
        return 1;
    }

    // 2. Configure server address
    struct sockaddr_in server_addr = {0};

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(
            AF_INET,
            SERVER_IP,
            &server_addr.sin_addr
        ) != 1) {
        fprintf(stderr, "Invalid server IP.\n");
        close(client_fd);
        return 1;
    }

    // 3. Connect to server
    if (connect(
            client_fd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)
        ) == -1) {
        perror("connect");
        close(client_fd);
        return 1;
    }

    printf("Connected to server!\n");
    printf("Type a message or 'exit' to quit.\n");

    // 4. Send and receive messages
    char buffer[BUFFER_SIZE];

    while (1) {
        printf("\nMessage: ");
        fflush(stdout);

        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            break;
        }

        if (strcmp(buffer, "exit\n") == 0 ||
            strcmp(buffer, "exit") == 0) {
            break;
        }

        size_t message_len = strlen(buffer);
        size_t total_sent = 0;

        // Send the entire message
        while (total_sent < message_len) {
            ssize_t sent = send(
                client_fd,
                buffer + total_sent,
                message_len - total_sent,
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

        // Receive echoed data
        size_t total_received = 0;

        while (total_received < message_len) {
            ssize_t received = recv(
                client_fd,
                buffer + total_received,
                message_len - total_received,
                0
            );

            if (received < 0) {
                perror("recv");
                goto cleanup;
            }

            if (received == 0) {
                printf("Server disconnected.\n");
                goto cleanup;
            }

            total_received += (size_t)received;
        }

        printf("Server echoed: ");
        fwrite(buffer, 1, total_received, stdout);
    }

cleanup:
    // 5. Close connection
    close(client_fd);

    printf("Client stopped.\n");

    return 0;
}