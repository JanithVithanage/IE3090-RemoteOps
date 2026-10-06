#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 9410
#define AUTH_TOKEN "OPS-2851"

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[4096] = {0};

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    printf("Connected to Agent on port %d successfully!\n", PORT);

    send(sock, AUTH_TOKEN, strlen(AUTH_TOKEN), 0);
    
    int valread = read(sock, buffer, 1024);
    if (valread > 0) {
        if (strncmp(buffer, "AUTH_SUCCESS", 12) != 0) {
            printf("Authentication failed. Disconnecting.\n");
            close(sock);
            return -1;
        }
        printf("Authentication successful! Ready to send commands.\n");
        
        // --- NEW COMMAND LOOP LOGIC ---
        while (1) {
            printf("remote> ");
            memset(buffer, 0, 4096);
            fgets(buffer, 4096, stdin);
            
            // Remove the trailing newline character added by fgets
            buffer[strcspn(buffer, "\n")] = 0;
            
            if (strlen(buffer) == 0) continue;
            
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
