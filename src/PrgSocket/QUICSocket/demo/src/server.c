#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ngtcp2/ngtcp2.h>
#include <sys/random.h>
#include <time.h>

#define SERVER_PORT 4433
#define BUFFER_SIZE 2048

int main(void){
    // SERVER UDP THÔNG THƯỜNG
    // Khởi tạo SOCKET
    int server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_fd < 0) {
        perror("socket");
        printf("SOCKET was not working");
        return 1;
    }
    // Gắn địa chỉ IP, PORT cho SOCKET
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int result = bind(server_fd, (const struct sockaddr*)&server_addr, sizeof(server_addr));
    if (result < 0) {
        perror("bind");
        printf("BIND was not working");
        return 1;
    }
    // Nhận dữ liệu từ Client
    uint8_t buffer[BUFFER_SIZE] = {0};
    struct sockaddr_in client_addr;

    while(1){
        socklen_t client_len = sizeof(client_addr);
        ssize_t byte_received = recvfrom(
            server_fd,
            buffer,
            BUFFER_SIZE,
            0,
            (struct sockaddr*)&client_addr,
            &client_len
        );
        printf("Received data: ");
        fwrite(buffer, 1, (size_t)byte_received, stdout);
        printf("\n");

        // Nhận QUIC Initial packet và phân tích header
        ngtcp2_pkt_hd hd = {0};
        result = ngtcp2_accept(
            &hd,
            buffer,
            (size_t)byte_received
        );
        if (result < 0) {
            fprintf(stderr, "ngtcp2_accept: %s\n",
            ngtcp2_strerror(result));
            continue;
        }
        printf("QUIC packet accepted\n");
        printf("Version: 0x%08x\n", (unsigned int)hd.version);
        printf("Packet type: %u\n", (unsigned int)hd.type);
        printf("DCID length: %zu\n", hd.dcid.datalen);
        printf("SCID length: %zu\n", hd.scid.datalen);
        printf("DCID: ");
        for (size_t i = 0; i < hd.dcid.datalen; i++) {
            printf("%02x", (unsigned)hd.dcid.data[i]);
        }
        printf("SICD");
        for (size_t i = 0; i < hd.scid.data; i++) {
            printf("%02x", (unsigned int)hd.scid.data);
        }
        
        //Tìm hiểu và khởi tạo Connection ID
        ngtcp2_cid_init();

        // Cấu hình QUIC settings và Transport Parameters
        ngtcp2_settings_default();
        ngtcp2_transport_params_default();

        // Tìm hiểu và triển khai Callbacks
        ngtcp2_callbacks();
        
        // Khởi tạo QUIC Server Connection
        ngtcp2_conn_server_new();

        // Tích hợp TLS 1.3 với GnuTLS
        //ngtcp2 cryto helper

        // Xử lý QUIC packet, tạo packet phản hồi
        ngtcp2_conn_read_pkt();
        // Và các hàm ghi packet

        // Hoàn thiện handshake, timer và event loop
        // Các API xử lý handshake và timer
    }
    close(server_fd);
    return 0;
}