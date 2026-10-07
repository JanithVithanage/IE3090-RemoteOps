#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 9410
#define AUTH_TOKEN "OPS-2851"
#define SID_TAG "SID:1582"

#define BUFFER_SIZE 16384

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

int send_text(int sock, const char *message) {
    size_t total = 0;
    size_t length = strlen(message);

    while (total < length) {
        ssize_t sent =
            send(sock,
                 message + total,
                 length - total,
                 0);

        if (sent <= 0) {
            return -1;
        }

        total += sent;
    }

    return 0;
}

int main() {
    int sock = 0;

    struct sockaddr_in serv_addr;

    char buffer[BUFFER_SIZE];

    if ((sock = socket(AF_INET,
                       SOCK_STREAM,
                       0)) < 0) {

        printf("\nSocket creation error\n");
        return -1;
    }

    serv_addr.sin_family =
        AF_INET;

    serv_addr.sin_port =
        htons(PORT);

    if (inet_pton(AF_INET,
                  "127.0.0.1",
                  &serv_addr.sin_addr) <= 0) {

        printf("\nInvalid address/ Address not supported\n");
        return -1;
    }

    if (connect(sock,
                (struct sockaddr *)&serv_addr,
                sizeof(serv_addr)) < 0) {

        printf("\nConnection Failed\n");
        return -1;
    }

    printf("Connected to Agent on port %d successfully!\n",
           PORT);

    char auth_message[128];

    snprintf(auth_message,
             sizeof(auth_message),
             "AUTH %s\n",
             AUTH_TOKEN);

    if (send_text(sock, auth_message) < 0) {
        printf("Failed to send authentication request.\n");
        close(sock);
        return -1;
    }

    int valread =
        recv_line(sock,
                  buffer,
                  sizeof(buffer));

    if (valread <= 0) {
        printf("No response from Agent.\n");
        close(sock);
        return -1;
    }

    if (strcmp(buffer,
               "OK AUTHENTICATED SID:1582\n") != 0) {

        printf("Authentication failed. Disconnecting.\n");
        printf("Agent Response: %s", buffer);

        close(sock);

        return -1;
    }

    printf("Agent Response: %s",
           buffer);

    printf("Authentication successful! Ready to send commands.\n");

    while (1) {
        printf("remote> ");

        memset(buffer,
               0,
               sizeof(buffer));

        if (fgets(buffer,
                  sizeof(buffer),
                  stdin) == NULL) {

            break;
        }

        buffer[strcspn(buffer,
                       "\n")] = '\0';

        if (strlen(buffer) == 0) {
            continue;
        }

        /*
         * Send command and newline separately.
         * This avoids the previous snprintf truncation warning.
         */
        if (send_text(sock, buffer) < 0 ||
            send_text(sock, "\n") < 0) {

            printf("Failed to send command.\n");
            break;
        }

        if (strcmp(buffer, "exit") == 0) {
            printf("Disconnecting from Agent...\n");
            break;
        }

        memset(buffer,
               0,
               sizeof(buffer));

        valread =
            recv_line(sock,
                      buffer,
                      sizeof(buffer));

        if (valread > 0) {
            printf("%s",
                   buffer);
        }
        else {
            printf("Connection lost.\n");
            break;
        }
    }

    close(sock);

    return 0;
}
