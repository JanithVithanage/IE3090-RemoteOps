#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>

#define PORT 9410
#define AUTH_TOKEN "OPS-2851"
#define SID_TAG "SID:1582"

#define BUFFER_SIZE 65536

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

int send_all(int sock,
             const void *data,
             size_t length) {

    size_t total = 0;

    while (total < length) {
        ssize_t sent =
            send(sock,
                 (const char *)data + total,
                 length - total,
                 0);

        if (sent <= 0) {
            return -1;
        }

        total += (size_t)sent;
    }

    return 0;
}

int send_text(int sock,
              const char *message) {

    return send_all(sock,
                    message,
                    strlen(message));
}

int recv_all(int sock,
             void *buffer,
             size_t length) {

    size_t total = 0;

    while (total < length) {
        ssize_t received =
            recv(sock,
                 (char *)buffer + total,
                 length - total,
                 0);

        if (received <= 0) {
            return -1;
        }

        total += (size_t)received;
    }

    return 0;
}

int is_safe_filename(const char *filename) {
    if (filename == NULL ||
        filename[0] == '\0') {
        return 0;
    }

    if (strcmp(filename, ".") == 0 ||
        strcmp(filename, "..") == 0) {
        return 0;
    }

    if (strstr(filename, "..") != NULL) {
        return 0;
    }

    if (strchr(filename, '/') != NULL ||
        strchr(filename, '\\') != NULL) {
        return 0;
    }

    return 1;
}

int handle_put(int sock,
               const char *filename) {

    if (!is_safe_filename(filename)) {
        printf("Invalid filename.\n");
        return 0;
    }

    struct stat file_stat;

    if (stat(filename, &file_stat) != 0 ||
        !S_ISREG(file_stat.st_mode)) {

        printf("Local file not found: %s\n",
               filename);

        return 0;
    }

    long long file_size =
        (long long)file_stat.st_size;

    FILE *file =
        fopen(filename, "rb");

    if (file == NULL) {
        perror("Could not open local file");
        return 0;
    }

    char command[512];

    snprintf(command,
             sizeof(command),
             "PUT %s %lld\n",
             filename,
             file_size);

    if (send_text(sock,
                  command) < 0) {

        fclose(file);
        return -1;
    }

    char file_buffer[8192];

    size_t bytes_read;

    while ((bytes_read =
            fread(file_buffer,
                  1,
                  sizeof(file_buffer),
                  file)) > 0) {

        if (send_all(sock,
                     file_buffer,
                     bytes_read) < 0) {

            fclose(file);
            return -1;
        }
    }

    fclose(file);

    char response[BUFFER_SIZE];

    int result =
        recv_line(sock,
                  response,
                  sizeof(response));

    if (result <= 0) {
        printf("Connection lost while waiting for PUT response.\n");
        return -1;
    }

    printf("%s",
           response);

    return 0;
}

int handle_get(int sock,
               const char *filename) {

    if (!is_safe_filename(filename)) {
        printf("Invalid filename.\n");
        return 0;
    }

    char command[512];

    snprintf(command,
             sizeof(command),
             "GET %s\n",
             filename);

    if (send_text(sock,
                  command) < 0) {

        return -1;
    }

    char response[BUFFER_SIZE];

    int result =
        recv_line(sock,
                  response,
                  sizeof(response));

    if (result <= 0) {
        printf("Connection lost while waiting for GET response.\n");
        return -1;
    }

    if (strncmp(response,
                "OK FILE_SEND ",
                13) != 0) {

        printf("%s",
               response);

        return 0;
    }

    char received_filename[256];

    char sid[64];

    long long file_size;

    int parsed =
        sscanf(response,
               "OK FILE_SEND %255s %lld %63s",
               received_filename,
               &file_size,
               sid);

    if (parsed != 3 ||
        file_size < 0) {

        printf("Invalid file response from Agent.\n");
        return 0;
    }

    char download_name[512];

    snprintf(download_name,
             sizeof(download_name),
             "downloaded_%s",
             received_filename);

    FILE *file =
        fopen(download_name, "wb");

    if (file == NULL) {
        perror("Could not create downloaded file");
        return -1;
    }

    char file_buffer[8192];

    long long remaining =
        file_size;

    while (remaining > 0) {
        size_t chunk_size =
            remaining > (long long)sizeof(file_buffer)
            ? sizeof(file_buffer)
            : (size_t)remaining;

        if (recv_all(sock,
                     file_buffer,
                     chunk_size) < 0) {

            fclose(file);
            return -1;
        }

        if (fwrite(file_buffer,
                   1,
                   chunk_size,
                   file) != chunk_size) {

            fclose(file);
            return -1;
        }

        remaining -= (long long)chunk_size;
    }

    fclose(file);

    printf("OK FILE_RECEIVED %s %lld bytes\n",
           download_name,
           file_size);

    return 0;
}

int main() {
    int sock = 0;

    struct sockaddr_in serv_addr;

    char buffer[BUFFER_SIZE];

    if ((sock =
         socket(AF_INET,
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

    if (send_text(sock,
                  auth_message) < 0) {

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
        printf("Agent Response: %s",
               buffer);

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
         * PUT filename
         *
         * The Controller determines the local file size
         * and sends the exact protocol message:
         *
         * PUT filename filesize\n
         */
        if (strncmp(buffer,
                    "PUT ",
                    4) == 0) {

            char filename[256];

            char extra[256];

            int parsed =
                sscanf(buffer + 4,
                       "%255s %255s",
                       filename,
                       extra);

            if (parsed != 1) {
                printf("Usage: PUT <filename>\n");
                continue;
            }

            if (handle_put(sock,
                           filename) < 0) {
                break;
            }

            continue;
        }

        /*
         * GET filename
         */
        if (strncmp(buffer,
                    "GET ",
                    4) == 0) {

            char filename[256];

            char extra[256];

            int parsed =
                sscanf(buffer + 4,
                       "%255s %255s",
                       filename,
                       extra);

            if (parsed != 1) {
                printf("Usage: GET <filename>\n");
                continue;
            }

            if (handle_get(sock,
                           filename) < 0) {
                break;
            }

            continue;
        }

        /*
         * Temporary exit handling.
         * QUIT will be implemented later.
         */
        if (strcmp(buffer,
                   "exit") == 0) {

            if (send_text(sock,
                          "exit\n") < 0) {
                break;
            }

            printf("Disconnecting from Agent...\n");
            break;
        }

        /*
         * Normal one-line command.
         */
        if (send_text(sock,
                      buffer) < 0 ||
            send_text(sock,
                      "\n") < 0) {

            printf("Failed to send command.\n");
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
