#include <stdio.h>

#define GENERAL_ERROR (1)
#define GENERAL_SUCCESS (0)

int main(int argc, char *argv[]) {
    int return_code = GENERAL_ERROR;

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <ip> <port> <file path>\n", argv[0]);
        goto cleanup;
    }

    return_code = GENERAL_SUCCESS;

cleanup:
    return return_code;
}