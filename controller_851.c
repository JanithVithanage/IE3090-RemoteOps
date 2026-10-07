#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>

#define PORT 9410
#define AUTH_TOKEN "OPS-2851"
#define SID_TAG "SID:1582"

#define BUFFER_SIZE 65536

typedef struct {
    int active;
    int running;
    int udp_sock;
    pthread_t thread_id;
    pthread_mutex_t mutex;
} UDPReceiver;

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

int send_all(int sock, const void *data, size_t length) {
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

int send_text(int sock, const char *message) {
    return send_all(sock, message, strlen(message));
}

int recv_all(int sock, void *buffer, size_t length) {
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

/*
 * UDP receiver thread.
 *
 * The socket uses a one-second receive timeout so the thread
 * can regularly check whether monitoring is still active.
 */
void *udp_receiver_thread(void *arg) {
    UDPReceiver *receiver =
        (UDPReceiver *)arg;

    char buffer[1024];

    while (1) {
        pthread_mutex_lock(&receiver->mutex);

        int active = receiver->active;

        pthread_mutex_unlock(&receiver->mutex);

        if (!active) {
            break;
        }

        ssize_t received =
            recvfrom(receiver->udp_sock,
                     buffer,
                     sizeof(buffer) - 1,
                     0,
                     NULL,
                     NULL);

        if (received > 0) {
            buffer[received] = '\0';

            printf("UDP MONITOR: %s",
                   buffer);

            fflush(stdout);

            continue;
        }

        if (received < 0) {
            /*
             * The one-second timeout is normal.
             * Check active again and continue waiting.
             */
            if (errno == EAGAIN ||
                errno == EWOULDBLOCK ||
                errno == EINTR) {
                continue;
            }

            break;
        }

        if (received == 0) {
            continue;
        }
    }

    return NULL;
}

int start_udp_receiver(UDPReceiver *receiver,
                       int udp_port) {
    pthread_mutex_lock(&receiver->mutex);

    if (receiver->running) {
        pthread_mutex_unlock(&receiver->mutex);
        return -2;
    }

    pthread_mutex_unlock(&receiver->mutex);

    if (udp_port < 1 ||
        udp_port > 65535) {
        return -3;
    }

    receiver->udp_sock =
        socket(AF_INET,
               SOCK_DGRAM,
               0);

    if (receiver->udp_sock < 0) {
        perror("UDP socket creation failed");
        return -1;
    }

    int opt = 1;

    setsockopt(receiver->udp_sock,
               SOL_SOCKET,
               SO_REUSEADDR,
               &opt,
               sizeof(opt));

    /*
     * Use a one-second timeout so the receiver thread
     * does not remain blocked forever when monitoring stops.
     */
    struct timeval timeout;

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    if (setsockopt(receiver->udp_sock,
                   SOL_SOCKET,
                   SO_RCVTIMEO,
                   &timeout,
                   sizeof(timeout)) < 0) {

        perror("UDP receive timeout setup failed");

        close(receiver->udp_sock);

        receiver->udp_sock = -1;

        return -1;
    }

    struct sockaddr_in address;

    memset(&address,
           0,
           sizeof(address));

    address.sin_family =
        AF_INET;

    address.sin_addr.s_addr =
        INADDR_ANY;

    address.sin_port =
        htons((uint16_t)udp_port);

    if (bind(receiver->udp_sock,
             (struct sockaddr *)&address,
             sizeof(address)) < 0) {

        perror("UDP bind failed");

        close(receiver->udp_sock);

        receiver->udp_sock = -1;

        return -1;
    }

    pthread_mutex_lock(&receiver->mutex);

    receiver->active = 1;
    receiver->running = 1;

    pthread_mutex_unlock(&receiver->mutex);

    if (pthread_create(&receiver->thread_id,
                       NULL,
                       udp_receiver_thread,
                       receiver) != 0) {

        perror("UDP receiver thread creation failed");

        pthread_mutex_lock(&receiver->mutex);

        receiver->active = 0;
        receiver->running = 0;

        pthread_mutex_unlock(&receiver->mutex);

        close(receiver->udp_sock);

        receiver->udp_sock = -1;

        return -1;
    }

    printf("UDP receiver listening on port %d\n",
           udp_port);

    return 0;
}

void stop_udp_receiver(UDPReceiver *receiver) {
    pthread_mutex_lock(&receiver->mutex);

    int was_running =
        receiver->running;

    receiver->active = 0;

    pthread_mutex_unlock(&receiver->mutex);

    if (was_running) {
        pthread_join(receiver->thread_id,
                     NULL);

        close(receiver->udp_sock);

        receiver->udp_sock = -1;

        pthread_mutex_lock(&receiver->mutex);

        receiver->running = 0;

        pthread_mutex_unlock(&receiver->mutex);
    }
}

int handle_put(int sock,
               const char *filename) {
    if (!is_safe_filename(filename)) {
        printf("Invalid filename.\n");
        return 0;
    }

    struct stat file_stat;

    if (stat(filename,
             &file_stat) != 0 ||
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
            remaining >
            (long long)sizeof(file_buffer)
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
                   file)
            != chunk_size) {

            fclose(file);
            return -1;
        }

        remaining -=
            (long long)chunk_size;
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

    UDPReceiver receiver;

    receiver.active = 0;
    receiver.running = 0;
    receiver.udp_sock = -1;

    pthread_mutex_init(&receiver.mutex,
                       NULL);

    if ((sock =
         socket(AF_INET,
                SOCK_STREAM,
                0)) < 0) {

        printf("\nSocket creation error\n");

        pthread_mutex_destroy(&receiver.mutex);

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

        close(sock);

        pthread_mutex_destroy(&receiver.mutex);

        return -1;
    }

    if (connect(sock,
                (struct sockaddr *)&serv_addr,
                sizeof(serv_addr)) < 0) {

        printf("\nConnection Failed\n");

        close(sock);

        pthread_mutex_destroy(&receiver.mutex);

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

        pthread_mutex_destroy(&receiver.mutex);

        return -1;
    }

    int valread =
        recv_line(sock,
                  buffer,
                  sizeof(buffer));

    if (valread <= 0) {

        printf("No response from Agent.\n");

        close(sock);

        pthread_mutex_destroy(&receiver.mutex);

        return -1;
    }

    if (strcmp(buffer,
               "OK AUTHENTICATED SID:1582\n") != 0) {

        printf("Authentication failed. Disconnecting.\n");

        printf("Agent Response: %s",
               buffer);

        close(sock);

        pthread_mutex_destroy(&receiver.mutex);

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
         * MONITOR START
         */
        if (strncmp(buffer,
                    "MONITOR START ",
                    14) == 0) {

            char extra[64];

            int udp_port;

            int parsed =
                sscanf(buffer + 14,
                       "%d %63s",
                       &udp_port,
                       extra);

            if (parsed != 1 ||
                udp_port < 1 ||
                udp_port > 65535) {

                printf("Usage: MONITOR START <udp_port>\n");

                continue;
            }

            int receiver_result =
                start_udp_receiver(&receiver,
                                   udp_port);

            if (receiver_result == -2) {

                printf("UDP monitoring is already active.\n");

                continue;
            }

            if (receiver_result != 0) {

                printf("Could not start local UDP receiver.\n");

                continue;
            }

            char command[128];

            snprintf(command,
                     sizeof(command),
                     "MONITOR START %d\n",
                     udp_port);

            if (send_text(sock,
                          command) < 0) {

                stop_udp_receiver(&receiver);

                printf("Failed to send MONITOR START.\n");

                break;
            }

            memset(buffer,
                   0,
                   sizeof(buffer));

            valread =
                recv_line(sock,
                          buffer,
                          sizeof(buffer));

            if (valread <= 0) {

                stop_udp_receiver(&receiver);

                printf("Connection lost.\n");

                break;
            }

            printf("%s",
                   buffer);

            if (strncmp(buffer,
                        "OK MONITOR_STARTED",
                        18) != 0) {

                stop_udp_receiver(&receiver);
            }

            continue;
        }

        /*
         * MONITOR STOP
         */
        if (strcmp(buffer,
                   "MONITOR STOP") == 0) {

            if (send_text(sock,
                          "MONITOR STOP\n") < 0) {

                printf("Failed to send MONITOR STOP.\n");

                break;
            }

            memset(buffer,
                   0,
                   sizeof(buffer));

            valread =
                recv_line(sock,
                          buffer,
                          sizeof(buffer));

            if (valread <= 0) {

                stop_udp_receiver(&receiver);

                printf("Connection lost.\n");

                break;
            }

            printf("%s",
                   buffer);

            stop_udp_receiver(&receiver);

            continue;
        }

        /*
         * PUT
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
         * GET
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
         * Handle the required QUIT command.
         * Ask the Agent to terminate this session cleanly,
         * wait for the goodbye response, then stop the
         * local UDP receiver and close the connection.
         */

         if (strcmp(buffer, "QUIT") == 0) {

              if (send_text(sock,
                           "QUIT\n") < 0) {

              printf("Failed to send QUIT command.\n");
              break;
              }

              memset(buffer,
                     0,
                     sizeof(buffer));

              valread =
                 recv_line(sock,
                           buffer,
                           sizeof(buffer));

              if (valread <= 0) {

                  stop_udp_receiver(&receiver);

                  printf("Connection lost while quitting.\n");

                  break;
              }

              printf("%s",
                      buffer);

              stop_udp_receiver(&receiver);

              printf("Disconnecting from Agent...\n");

              break;
          }

        /*
         * Normal command.
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

    stop_udp_receiver(&receiver);

    close(sock);

    pthread_mutex_destroy(&receiver.mutex);

    return 0;
}
