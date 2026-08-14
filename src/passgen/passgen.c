#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "../libutilipc/utilipc.h"

static const char CHARSET[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()-_=+[]{};:,.<>/?";

int main(int argc, char *argv[]) {
    utilipc_init();
    int length = 16;

    if (argc > 1) {
        length = atoi(argv[1]);
        if (length <= 0 || length > 1024) {
            printf("Error: Invalid password length. Please use a number between 1 and 1024.\n");
            utilipc_close();
            return 1;
        }
    }

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        perror("Error: Failed to open /dev/urandom");
        utilipc_close();
        return 1;
    }

    int charset_size = sizeof(CHARSET) - 1;
    int limit = (256 / charset_size) * charset_size;

    char *password = malloc(length + 1);
    if (!password) {
        close(fd);
        utilipc_close();
        return 1;
    }

    unsigned char rng_buf[256];
    size_t buf_pos = 0;
    ssize_t buf_bytes = 0;
    int generated = 0;

    while (generated < length) {
        if (buf_pos >= (size_t)buf_bytes) {
            buf_bytes = read(fd, rng_buf, sizeof(rng_buf));
            buf_pos = 0;
            if (buf_bytes <= 0) {
                printf("Error: Failed to read secure random bytes.\n");
                free(password);
                close(fd);
                utilipc_close();
                return 1;
            }
        }

        unsigned char byte = rng_buf[buf_pos++];
        if (byte < limit) {
            password[generated++] = CHARSET[byte % charset_size];
        }
    }
    password[length] = '\0';
    close(fd);

    printf("======================\n");
    printf("[Password]: %s\n", password);
    printf("======================\n");

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "passgen: generated %d char password", length);
    utilipc_write_status(-1, -1, -1, log_msg);

    free(password);
    utilipc_close();
    return 0;
}
