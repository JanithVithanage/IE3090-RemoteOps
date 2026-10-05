#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 9410
#define AUTH_TOKEN "TOKEN_851"

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};

    // 1. Create the TCP socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // 2. Set the Agent IP address
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    // 3. Connect to the Agent
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    printf("Connected to Agent on port %d successfully!\n", PORT);

    // 4. Send the Authentication Token
    send(sock, AUTH_TOKEN, strlen(AUTH_TOKEN), 0);
    printf("Sent Authentication Token: %s\n", AUTH_TOKEN);

    // 5. Wait for Agent's response
    int valread = read(sock, buffer, 1024);
    if (valread > 0) {
        printf("Agent Response: %s\n", buffer);
        if (strncmp(buffer, "AUTH_SUCCESS", 12) != 0) {
            printf("Authentication failed. Disconnecting.\n");
            close(sock);
            return -1;
        }
        printf("Authentication successful! Ready to send commands.\n");
    } else {
        printf("No response from Agent. Disconnecting.\n");
        close(sock);
        return -1;
    }

    close(sock);
    return 0;
}
