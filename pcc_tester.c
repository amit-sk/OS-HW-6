#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#define GENERAL_ERROR (1)
#define GENERAL_SUCCESS (0)

#define BUFFER_SIZE (1024)
#define PRINTABLE_LOWER_BOUND (32)
#define PRINTABLE_UPPER_BOUND (126)
#define AMOUNT_OF_PRINTABLE_CHARS (PRINTABLE_UPPER_BOUND - PRINTABLE_LOWER_BOUND + 1)
#define PRINTABLE_TO_INDEX(c) ((c) - PRINTABLE_LOWER_BOUND)

uint32_t expected_totals[AMOUNT_OF_PRINTABLE_CHARS] = {0};

int is_printable(char c) {
    return (PRINTABLE_LOWER_BOUND <= c && c <= PRINTABLE_UPPER_BOUND);
}

uint32_t count_printable(const char *data, size_t len) {
    uint32_t count = 0;
    for (size_t i = 0; i < len; i++) {
        if (is_printable(data[i])) {
            count++;
        }
    }
    return count;
}

int send_data(int sock_fd, uint32_t N, const char *data) {
    ssize_t bytes_sent;
    uint32_t net_N = htonl(N);
    bytes_sent = send(sock_fd, &net_N, sizeof(net_N), 0);
    if (bytes_sent != sizeof(net_N)) {
        perror("send N failed");
        return GENERAL_ERROR;
    }

    size_t sent = 0;
    while (sent < N) {
        size_t to_send = (N - sent) > BUFFER_SIZE ? BUFFER_SIZE : (N - sent);
        bytes_sent = send(sock_fd, data + sent, to_send, 0);
        if (bytes_sent == -1) {
            perror("send data failed");
            return GENERAL_ERROR;
        }
        sent += bytes_sent;
    }
    return GENERAL_SUCCESS;
}

int receive_count(int sock_fd, uint32_t *count) {
    uint32_t net_count;
    ssize_t bytes_recv = recv(sock_fd, &net_count, sizeof(net_count), 0);
    if (bytes_recv != sizeof(net_count)) {
        perror("recv count failed");
        return GENERAL_ERROR;
    }
    *count = ntohl(net_count);
    return GENERAL_SUCCESS;
}

int run_test(const char *ip, uint16_t port, uint32_t N, const char *data, const char *test_name) {
    printf("\nRunning test: %s\n", test_name);
    uint32_t expected = count_printable(data, N);

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("socket creation failed");
        return GENERAL_ERROR;
    }

    struct sockaddr_in serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) != 1) {
        perror("inet_pton failed");
        close(sock_fd);
        return GENERAL_ERROR;
    }
    serv_addr.sin_port = htons(port);

    if (connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("connect failed");
        close(sock_fd);
        return GENERAL_ERROR;
    }

    if (send_data(sock_fd, N, data) != GENERAL_SUCCESS) {
        close(sock_fd);
        return GENERAL_ERROR;
    }
    uint32_t received;
    if (receive_count(sock_fd, &received) != GENERAL_SUCCESS) {
        close(sock_fd);
        return GENERAL_ERROR;
    }
    close(sock_fd);

    if (expected != received) {
        printf("FAIL: expected %u, received %u\n", expected, received);
        return GENERAL_ERROR;
    }
    printf("PASS: %u printable characters\n", received);
    return GENERAL_SUCCESS;
}

void accumulate_expected_totals(uint32_t N, const char *data) {
    for (size_t i = 0; i < N; i++) {
        if (is_printable(data[i])) {
            expected_totals[PRINTABLE_TO_INDEX(data[i])]++;
        }
    }
}

int run_all_tests(const char *ip, uint16_t port) {
    int tests_passed = 0;
    int total_tests = 0;

    // Test 1: Empty data
    total_tests++;
    char empty[1] = "";
    if (run_test(ip, port, 0, empty, "Empty data") == GENERAL_SUCCESS) {
        tests_passed++;
    }

    // Test 2: All printable characters
    total_tests++;
    char printable[95];
    for (int i = 0; i < 95; i++) {
        printable[i] = PRINTABLE_LOWER_BOUND + i;
    }
    if (run_test(ip, port, 95, printable, "All printable") == GENERAL_SUCCESS) {
        tests_passed++;
    }

    // Test 3: All non-printable (space-1 and 127-255, but limit to ASCII for simplicity)
    total_tests++;
    char non_printable[32];
    for (int i = 0; i < 32; i++) {
        non_printable[i] = i;  // 0-31
    }
    if (run_test(ip, port, 32, non_printable, "All non-printable") == GENERAL_SUCCESS) {
        tests_passed++;
    }

    // Test 4: Mixed data
    total_tests++;
    char mixed[] = "Hello World! \n\t\x01\x7F";  // printable and non-printable
    if (run_test(ip, port, strlen(mixed), mixed, "Mixed data") == GENERAL_SUCCESS) {
        tests_passed++;
    }

    // Test 5: Large data (10KB of alternating printable/non-printable)
    total_tests++;
    size_t large_size = 10240;
    char *large_data = malloc(large_size);
    if (!large_data) {
        perror("malloc failed");
        return GENERAL_ERROR;
    }
    for (size_t i = 0; i < large_size; i++) {
        large_data[i] = (i % 2 == 0) ? 'A' : '\n';
    }
    if (run_test(ip, port, large_size, large_data, "Large data") == GENERAL_SUCCESS) {
        tests_passed++;
    }
    free(large_data);

    // Test 6: Only whitespace characters
    total_tests++;
    char whitespace[] = " \t\n\r\f\v";  // all printable whitespace
    if (run_test(ip, port, strlen(whitespace), whitespace, "Only whitespace") == GENERAL_SUCCESS) {
        tests_passed++;
    }

    // Test 7: Extended ASCII (non-printable)
    total_tests++;
    char extended[128];
    for (int i = 0; i < 128; i++) {
        extended[i] = 128 + i;  // 128-255
    }
    if (run_test(ip, port, 128, extended, "Extended ASCII") == GENERAL_SUCCESS) {
        tests_passed++;
    }

    // Test 8: Single printable character
    total_tests++;
    char single_printable[] = "A";
    if (run_test(ip, port, 1, single_printable, "Single printable") == GENERAL_SUCCESS) {
        tests_passed++;
    }

    // Test 9: Single non-printable character
    total_tests++;
    char single_non_printable[] = "\x00";
    if (run_test(ip, port, 1, single_non_printable, "Single non-printable") == GENERAL_SUCCESS) {
        tests_passed++;
    }

    // Test 10: Very large data (1MB)
    total_tests++;
    size_t very_large_size = 1024 * 1024;  // 1MB
    char *very_large_data = malloc(very_large_size);
    if (!very_large_data) {
        perror("malloc failed");
        return GENERAL_ERROR;
    }
    for (size_t i = 0; i < very_large_size; i++) {
        very_large_data[i] = (i % 256);  // cycle through all bytes
    }
    if (run_test(ip, port, very_large_size, very_large_data, "Very large data (1MB)") == GENERAL_SUCCESS) {
        tests_passed++;
    }
    free(very_large_data);

    return tests_passed == total_tests ? GENERAL_SUCCESS : GENERAL_ERROR;
}

void accumulate_expected() {
    // Test 1: Empty data
    char empty[1] = "";
    accumulate_expected_totals(0, empty);

    // Test 2: All printable characters
    char printable[95];
    for (int i = 0; i < 95; i++) {
        printable[i] = PRINTABLE_LOWER_BOUND + i;
    }
    accumulate_expected_totals(95, printable);

    // Test 3: All non-printable
    char non_printable[32];
    for (int i = 0; i < 32; i++) {
        non_printable[i] = i;
    }
    accumulate_expected_totals(32, non_printable);

    // Test 4: Mixed data
    char mixed[] = "Hello World! \n\t\x01\x7F";
    accumulate_expected_totals(strlen(mixed), mixed);

    // Test 5: Large data
    size_t large_size = 10240;
    char *large_data = malloc(large_size);
    for (size_t i = 0; i < large_size; i++) {
        large_data[i] = (i % 2 == 0) ? 'A' : '\n';
    }
    accumulate_expected_totals(large_size, large_data);
    free(large_data);

    // Test 6: Only whitespace
    char whitespace[] = " \t\n\r\f\v";
    accumulate_expected_totals(strlen(whitespace), whitespace);

    // Test 7: Extended ASCII
    char extended[128];
    for (int i = 0; i < 128; i++) {
        extended[i] = 128 + i;
    }
    accumulate_expected_totals(128, extended);

    // Test 8: Single printable
    char single_printable[] = "A";
    accumulate_expected_totals(1, single_printable);

    // Test 9: Single non-printable
    char single_non_printable[] = "\x00";
    accumulate_expected_totals(1, single_non_printable);

    // Test 10: Very large data
    size_t very_large_size = 1024 * 1024;
    char *very_large_data = malloc(very_large_size);
    for (size_t i = 0; i < very_large_size; i++) {
        very_large_data[i] = (i % 256);
    }
    accumulate_expected_totals(very_large_size, very_large_data);
    free(very_large_data);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return GENERAL_ERROR;
    }

    char *ip = "127.0.0.1";
    uint16_t port;

    if (sscanf(argv[1], "%hu", &port) != 1) {
        fprintf(stderr, "Invalid port number: %s\n", argv[1]);
        return GENERAL_ERROR;
    }

    // Start server in background
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe failed");
        return GENERAL_ERROR;
    }

    pid_t child_pid = fork();
    if (child_pid == -1) {
        perror("fork failed");
        return GENERAL_ERROR;
    }

    if (child_pid == 0) {
        // Child: server
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        char port_str[16];
        sprintf(port_str, "%hu", port);
        execl("./pcc_server", "pcc_server", port_str, NULL);
        perror("execl failed");
        exit(GENERAL_ERROR);
    }

    // Parent
    close(pipefd[1]);

    // Wait for server to start
    sleep(2);

    accumulate_expected();

    int num_concurrent = 3;
    pid_t pids[num_concurrent];
    for(int i = 0; i < num_concurrent; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            // child
            exit(run_all_tests(ip, port));
        } else if (pids[i] == -1) {
            perror("fork failed");
            kill(child_pid, SIGINT);
            wait(NULL);
            close(pipefd[0]);
            return GENERAL_ERROR;
        }
    }

    for(int i = 0; i < num_concurrent; i++) {
        int status;
        if (waitpid(pids[i], &status, 0) == -1) {
            perror("waitpid failed");
            kill(child_pid, SIGINT);
            wait(NULL);
            close(pipefd[0]);
            return GENERAL_ERROR;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            printf("Concurrent test set %d failed\n", i);
            kill(child_pid, SIGINT);
            wait(NULL);
            close(pipefd[0]);
            return GENERAL_ERROR;
        }
    }

    printf("All concurrent tests passed!\n");

    // Multiply expected_totals by num_concurrent
    for(int i = 0; i < AMOUNT_OF_PRINTABLE_CHARS; i++) {
        expected_totals[i] *= num_concurrent;
    }

    // Send SIGINT to server
    if (kill(child_pid, SIGINT) == -1) {
        perror("kill failed");
        return GENERAL_ERROR;
    }

    // Wait for server to finish
    int status;
    if (wait(&status) == -1) {
        perror("wait failed");
        return GENERAL_ERROR;
    }

    // Read server output
    char buffer[8192];
    ssize_t bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1);
    if (bytes_read == -1) {
        perror("read failed");
        close(pipefd[0]);
        return GENERAL_ERROR;
    }
    buffer[bytes_read] = '\0';
    close(pipefd[0]);

    // Parse output
    int clients_served = -1;
    uint32_t actual_totals[AMOUNT_OF_PRINTABLE_CHARS] = {0};

    char *line = strtok(buffer, "\n");
    while (line != NULL) {
        if (sscanf(line, "Served %d client(s) successfully", &clients_served) == 1) {
            // ok
        } else {
            char c;
            uint32_t count;
            if (sscanf(line, "char '%c' : %u times", &c, &count) == 2) {
                if (is_printable(c)) {
                    actual_totals[PRINTABLE_TO_INDEX(c)] = count;
                }
            }
        }
        line = strtok(NULL, "\n");
    }

    // Verify
    int stats_ok = 1;
    if (clients_served != 30) {
        printf("FAIL: Expected 30 clients, served %d\n", clients_served);
        stats_ok = 0;
    }
    for (int i = 0; i < AMOUNT_OF_PRINTABLE_CHARS; i++) {
        if (expected_totals[i] != actual_totals[i]) {
            printf("FAIL: Char '%c' expected %u, actual %u\n", (char)(PRINTABLE_LOWER_BOUND + i), expected_totals[i], actual_totals[i]);
            stats_ok = 0;
        }
    }

    if (stats_ok) {
        printf("Server statistics verified successfully!\n");
        return GENERAL_SUCCESS;
    } else {
        printf("Server statistics verification failed.\n");
        return GENERAL_ERROR;
    }
}