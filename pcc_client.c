#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>

#define GENERAL_ERROR (1)
#define GENERAL_SUCCESS (0)

#define BUFFER_SIZE (1024)

int handle_client(int sock_fd, int file_fd) {
    int return_code = GENERAL_ERROR;
    uint32_t file_size = 0;
    ssize_t bytes_read = 0;
    ssize_t bytes_sent_overall = 0;
    ssize_t bytes_sent = 0;
    uint32_t pcc_count = 0;
    char buffer[BUFFER_SIZE] = {0};
    struct stat file_stat = {0};

    // Get file size
    if (fstat(file_fd, &file_stat) == -1) {
        perror("fstat failed");
        goto cleanup;
    }

    // sending N - expecting to send an int in one call
    file_size = htonl((uint32_t)file_stat.st_size);
    bytes_sent = send(sock_fd, &file_size, sizeof(file_size), 0);
    if (bytes_sent != sizeof(file_size)) {
        perror("send N failed");
        goto cleanup;
    }

    bytes_sent_overall = 0;
    while (bytes_sent_overall < file_stat.st_size) {
        // read from file into buffer
        bytes_read = read(file_fd, buffer, sizeof(buffer));
        if (bytes_read == -1) {
            perror("file read failed");
            goto cleanup;
        }

        // send buffer to server
        bytes_sent = 0;
        while (bytes_sent < bytes_read) {
            ssize_t chunk_size = send(sock_fd, buffer + bytes_sent, bytes_read - bytes_sent, 0);
            if (chunk_size == -1) {
                perror("send failed");
                goto cleanup;
            }
            bytes_sent += chunk_size;
        }

        bytes_sent_overall += bytes_read;
    }

    // read pcc_count from server - expecting to receive an int in one call
    bytes_read = recv(sock_fd, &pcc_count, sizeof(pcc_count), 0);
    if (bytes_read != sizeof(pcc_count)) {
        perror("recv pcc_count failed");
        goto cleanup;
    }

    pcc_count = ntohl(pcc_count);
    printf("# of printable characters: %u\n", pcc_count);

    return_code = GENERAL_SUCCESS;
cleanup:
    return return_code;
}

int main(int argc, char *argv[]) {
    int return_code = GENERAL_ERROR;
    int sock_fd = -1;
    int file_fd = -1;
    struct sockaddr_in serv_addr = {0};
    struct in_addr addr = {0};
    uint16_t port = 0;

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <ip> <port> <file path>\n", argv[0]);
        goto cleanup;
    }

    if (inet_pton(AF_INET, argv[1], &addr) != 1) {
        fprintf(stderr, "Invalid IP address: %s\n", argv[1]);
        goto cleanup;
    }

    if (sscanf(argv[2], "%hu", &port) != 1) {
        fprintf(stderr, "Invalid port number: %s\n", argv[2]);
        goto cleanup;
    }

    file_fd = open(argv[3], O_RDONLY);
    if (file_fd == -1) {
        perror("file open failed");
        goto cleanup;
    }

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("socket creation failed");
        goto cleanup;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr = addr;
    serv_addr.sin_port = htons(port);

    if (connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("connect failed");
        goto cleanup;
    }

    if (GENERAL_SUCCESS != handle_client(sock_fd, file_fd)) {
        goto cleanup;
    }

    return_code = GENERAL_SUCCESS;

cleanup:
    if (sock_fd != -1) {
        close(sock_fd);
    }
    if (file_fd != -1) {
        close(file_fd);
    }
    return return_code;
}