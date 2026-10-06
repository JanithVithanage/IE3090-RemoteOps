#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

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

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[4096] = {0};

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\nSocket creation error\n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported\n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed\n");
        return -1;
    }

    printf("Connected to Agent on port %d successfully!\n", PORT);

    char auth_message[128];

    snprintf(auth_message, sizeof(auth_message),
             "AUTH %s\n", AUTH_TOKEN);

    send(sock, auth_message, strlen(auth_message), 0);

    int valread = recv_line(sock, buffer, sizeof(buffer));

    if (valread > 0) {
        if (strcmp(buffer, "OK AUTHENTICATED SID:1582\n") != 0) {
            printf("Authentication failed. Disconnecting.\n");
            printf("Agent Response: %s", buffer);
            close(sock);
            return -1;
        }

        printf("Agent Response: %s", buffer);
        printf("Authentication successful! Ready to send commands.\n");

        // --- NEW COMMAND LOOP LOGIC ---
        while (1) {
            printf("remote> ");

            memset(buffer, 0, 4096);

            fgets(buffer, 4096, stdin);

            // Remove the trailing newline character added by fgets
            buffer[strcspn(buffer, "\n")] = 0;

            if (strlen(buffer) == 0) {
                continue;
            }

            send(sock, buffer, strlen(buffer), 0);

            if (strncmp(buffer, "exit", 4) == 0) {
                printf("Disconnecting from Agent...\n");
                break;
            }

            memset(buffer, 0, 4096);

            valread = read(sock, buffer, 4096);

            if (valread > 0) {
                printf("%s\n", buffer);
            } else {
                printf("Connection lost.\n");
                break;
            }
        }

    } else {
        printf("No response from Agent.\n");
    }

    close(sock);

    return 0;
}
