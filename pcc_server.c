#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>

#define GENERAL_ERROR (1)
#define GENERAL_SUCCESS (0)

#define LISTEN_QUEUE_SIZE (10)
#define AMOUNT_OF_PRINTABLE_CHARS (126-32+1)
#define PRINTABLE_LOWER_BOUND (32)
#define PRINTABLE_UPPER_BOUND (126)
#define PRINTABLE_TO_INDEX(c) ((c) - PRINTABLE_LOWER_BOUND)

#define BUFFER_SIZE (1024)

uint32_t pcc_total[AMOUNT_OF_PRINTABLE_CHARS] = {0};
uint32_t clients_count = 0;
bool sigint_received = false;

bool is_printable(char c) {
    return (PRINTABLE_LOWER_BOUND <= c && c <= PRINTABLE_UPPER_BOUND);
}

void print_pcc_statistics() {
    for (int c = PRINTABLE_LOWER_BOUND; c <= PRINTABLE_UPPER_BOUND; c++) {
        int count = pcc_total[PRINTABLE_TO_INDEX(c)];
        if (count > 0) {
            printf("char '%c' : %u times\n", (char)c, count);
        }
    }
}

void sigint_handler(int signum) {
    sigint_received = true;
}

void update_pcc_total(uint32_t new_pcc_count[]) {
    for (int i = 0; i < AMOUNT_OF_PRINTABLE_CHARS; i++) {
        pcc_total[i] += new_pcc_count[i];
    }
}

int handle_new_client(int server_fd) {
    int return_code = GENERAL_ERROR;
    uint32_t N = 0;
    uint32_t pcc_count = 0;
    int client_fd = -1;
    ssize_t bytes_received = 0;
    ssize_t bytes_sent = 0;
    uint32_t new_pcc_count[AMOUNT_OF_PRINTABLE_CHARS] = {0};
    char buffer[BUFFER_SIZE] = {0};

    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd == -1) {
        if (errno == EINTR) {
            // Interrupted by signal, likely SIGINT, quitely shutting down.
            return GENERAL_SUCCESS;
        }
        perror("accept failed");
        goto cleanup;
    }

    // receive N - expecting to receive an int in one call
    bytes_received = recv(client_fd, &N, sizeof(N), 0);
    if (bytes_received != sizeof(N)) {
        perror("recv N failed");
        return_code = GENERAL_SUCCESS;  // server drops connections and continues
        goto cleanup;
    }

    N = ntohl(N);

    bytes_received = 0;
    while (bytes_received < N) {
        ssize_t chunk_size = recv(client_fd, buffer, BUFFER_SIZE, 0);
        if (chunk_size == -1 || chunk_size == 0) {
            if (errno == EINTR) {
                break; // Interrupted by signal, likely SIGINT, finish handling current client.
            }
            if (errno == ETIMEDOUT || errno == ECONNRESET || errno == EPIPE) {
                perror("recv failed");
            }
            return_code = GENERAL_SUCCESS;  // client disconnected, not a server error
            goto cleanup;
        }

        // process into current statistics
        for (ssize_t i = 0; i < chunk_size; i++) {
            char c = buffer[i];
            if (is_printable(c)) {
                pcc_count++;
                new_pcc_count[PRINTABLE_TO_INDEX(c)]++;
            }
        }

        bytes_received += chunk_size;
    }

    // expecting to send an int in one call
    pcc_count = htonl(pcc_count);
    bytes_sent = send(client_fd, &pcc_count, sizeof(pcc_count), 0);
    if (bytes_sent != sizeof(pcc_count)) {
        if (errno == ETIMEDOUT || errno == ECONNRESET || errno == EPIPE) {
            perror("send pcc_count failed");
        }
        return_code = GENERAL_SUCCESS;  // not a server error
        goto cleanup;
    }

    clients_count++;
    update_pcc_total(new_pcc_count);
    return_code = GENERAL_SUCCESS;
cleanup:
    if (client_fd != -1) {
        close(client_fd);
    }

    return return_code;
}

int run_server(uint16_t port) {
    int return_code = GENERAL_ERROR;
    int server_fd = -1;
    struct sockaddr_in serv_addr = {0};
    socklen_t addrsize = sizeof(struct sockaddr_in);
    struct sigaction act = {0};
    act.sa_handler = sigint_handler;
    // act.sa_flags = SA_RESTART;  // need for send \ recv?

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket creation failed");
        goto cleanup;
    }

    int opt_reuse_addr_true = 1;
    if (0 != setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt_reuse_addr_true, sizeof(opt_reuse_addr_true))) {
        perror("setsockopt failed");
        goto cleanup;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if (0 != bind(server_fd, (struct sockaddr *)&serv_addr, addrsize)) {
        perror("bind failed");
        goto cleanup;
    }

    if (0 != listen(server_fd, LISTEN_QUEUE_SIZE)) {
        perror("listen failed");
        goto cleanup;
    }

    if (0 != sigaction(SIGINT, &act, NULL)) {
        perror("sigaction failed");
        goto cleanup;
    }

    while (!sigint_received) {
        if (GENERAL_ERROR == handle_new_client(server_fd)) {
            goto cleanup;
        }
    }

    print_pcc_statistics();
    printf("Served %u client(s) successfully\n", clients_count);

    return_code = GENERAL_SUCCESS;
cleanup:
    if (server_fd != -1) {
        close(server_fd);
    }

    return return_code;
}

int main(int argc, char *argv[]) {
    int return_code = GENERAL_ERROR;
    uint16_t port = 0;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        goto cleanup;
    }

    if (sscanf(argv[1], "%hu", &port) != 1) {
        fprintf(stderr, "Invalid port number: %s\n", argv[1]);
        goto cleanup;
    }

    if (GENERAL_ERROR == run_server(port)) {
        goto cleanup;
    }

    return_code = GENERAL_SUCCESS;

cleanup:
    return return_code;
}