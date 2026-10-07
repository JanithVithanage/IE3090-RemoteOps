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
        ssize_t sent = send(sock,
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

/*
 * Execute only one of the five commands allowed by the assignment.
 */
int execute_whitelisted_command(const char *command,
                                 char *output,
                                 size_t output_size) {
    const char *shell_command = NULL;

    if (strcmp(command, "DATE") == 0) {
        shell_command = "date";
    }
    else if (strcmp(command, "UPTIME") == 0) {
        shell_command = "uptime -p";
    }
    else if (strcmp(command, "DISKFREE") == 0) {
        shell_command = "df -h / | tail -n 1";
    }
    else if (strcmp(command, "HOSTNAME") == 0) {
        shell_command = "hostname";
    }
    else if (strcmp(command, "WHOAMI") == 0) {
        shell_command = "whoami";
    }
    else {
        return -2;
    }

    FILE *file = popen(shell_command, "r");

    if (file == NULL) {
        return -1;
    }

    size_t used = fread(output,
                        1,
                        output_size - 1,
                        file);

    output[used] = '\0';

    pclose(file);

    /*
     * The RemoteOps protocol requires one-line responses.
     * Remove newlines produced by the shell commands.
     */
    for (size_t i = 0; i < used; i++) {
        if (output[i] == '\n' ||
            output[i] == '\r') {
            output[i] = ' ';
        }
    }

    /*
     * Remove trailing spaces.
     */
    while (used > 0 &&
           (output[used - 1] == ' ' ||
            output[used - 1] == '\t')) {
        output[used - 1] = '\0';
        used--;
    }

    return 0;
}

int get_sysinfo(char *response, size_t response_size) {
    FILE *file;
    double uptime;
    double cpu_load;
    long mem_total_kb = 0;
    long mem_available_kb = 0;

    file = fopen("/proc/loadavg", "r");

    if (file == NULL) {
        return -1;
    }

    if (fscanf(file, "%lf", &cpu_load) != 1) {
        fclose(file);
        return -1;
    }

    fclose(file);

    file = fopen("/proc/meminfo", "r");

    if (file == NULL) {
        return -1;
    }

    char line[256];

    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line + 9, "%ld", &mem_total_kb);
        }
        else if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line + 13, "%ld", &mem_available_kb);
        }
    }

    fclose(file);

    file = fopen("/proc/uptime", "r");

    if (file == NULL) {
        return -1;
    }

    if (fscanf(file, "%lf", &uptime) != 1) {
        fclose(file);
        return -1;
    }

    fclose(file);

    long mem_used_mb =
        (mem_total_kb - mem_available_kb) / 1024;

    long uptime_sec =
        (long)uptime;

    snprintf(response,
             response_size,
             "OK SYSINFO %.2f %ld %ld %s\n",
             cpu_load,
             mem_used_mb,
             uptime_sec,
             SID_TAG);

    return 0;
}

int get_process_list(char *response, size_t response_size) {
    FILE *file;

    file = popen("ps -e -o pid=,comm=", "r");

    if (file == NULL) {
        return -1;
    }

    char line[256];
    size_t used = 0;
    int first_process = 1;

    used += snprintf(response + used,
                     response_size - used,
                     "OK PROCS ");

    while (fgets(line, sizeof(line), file) != NULL) {
        int pid;
        char command[128];

        if (sscanf(line, "%d %127s", &pid, command) != 2) {
            continue;
        }

        int written;

        if (first_process) {
            written = snprintf(response + used,
                               response_size - used,
                               "%d/%s",
                               pid,
                               command);

            first_process = 0;
        }
        else {
            written = snprintf(response + used,
                               response_size - used,
                               ",%d/%s",
                               pid,
                               command);
        }

        if (written < 0 ||
            (size_t)written >= response_size - used) {
            break;
        }

        used += (size_t)written;
    }

    pclose(file);

    snprintf(response + used,
             response_size - used,
             " %s\n",
             SID_TAG);

    return 0;
}

void *handle_client(void *arg) {
    int new_socket = *((int *)arg);

    free(arg);

    char buffer[BUFFER_SIZE];

    printf("Controller connected!\n");

    int valread =
        recv_line(new_socket,
                  buffer,
                  sizeof(buffer));

    if (valread <= 0) {
        printf("Controller disconnected before authentication.\n");
        close(new_socket);
        return NULL;
    }

    char expected_auth[128];

    snprintf(expected_auth,
             sizeof(expected_auth),
             "AUTH %s\n",
             AUTH_TOKEN);

    if (strcmp(buffer, expected_auth) == 0) {
        printf("Authentication successful. Ready for commands.\n");

        char response[8192];

        snprintf(response,
                 sizeof(response),
                 "OK AUTHENTICATED %s\n",
                 SID_TAG);

        send_text(new_socket, response);

        while (1) {
            memset(buffer, 0, sizeof(buffer));

            int cmd_read =
                recv_line(new_socket,
                          buffer,
                          sizeof(buffer));

            if (cmd_read <= 0) {
                printf("Controller disconnected.\n");
                break;
            }

            buffer[strcspn(buffer, "\n")] = '\0';

            printf("Received command: %s\n", buffer);

            /*
             * Temporary exit handling.
             * QUIT will be implemented later.
             */
            if (strcmp(buffer, "exit") == 0) {
                printf("Controller disconnected.\n");
                break;
            }

            /*
             * SYSINFO
             */
            if (strcmp(buffer, "SYSINFO") == 0) {
                char sysinfo_response[512];

                if (get_sysinfo(sysinfo_response,
                                sizeof(sysinfo_response)) == 0) {

                    send_text(new_socket,
                              sysinfo_response);
                }
                else {
                    snprintf(response,
                             sizeof(response),
                             "ERR 003 SYSINFO_FAILED %s\n",
                             SID_TAG);

                    send_text(new_socket,
                              response);
                }

                continue;
            }

            /*
             * LISTPROC
             */
            if (strcmp(buffer, "LISTPROC") == 0) {
                char process_response[BUFFER_SIZE];

                if (get_process_list(process_response,
                                     sizeof(process_response)) == 0) {

                    send_text(new_socket,
                              process_response);
                }
                else {
                    snprintf(response,
                             sizeof(response),
                             "ERR 003 PROCESS_LIST_FAILED %s\n",
                             SID_TAG);

                    send_text(new_socket,
                              response);
                }

                continue;
            }

            /*
             * EXEC command.
             */
            if (strncmp(buffer, "EXEC ", 5) == 0) {
                char command_name[64];

                if (sscanf(buffer + 5,
                           "%63s",
                           command_name) != 1) {

                    snprintf(response,
                             sizeof(response),
                             "ERR 002 COMMAND_NOT_ALLOWED %s\n",
                             SID_TAG);

                    send_text(new_socket,
                              response);

                    continue;
                }

                /*
                 * Reject anything with extra arguments.
                 * Example:
                 * EXEC DATE something
                 */
                char extra[64];

                if (sscanf(buffer + 5,
                           "%63s %63s",
                           command_name,
                           extra) == 2) {

                    snprintf(response,
                             sizeof(response),
                             "ERR 002 COMMAND_NOT_ALLOWED %s\n",
                             SID_TAG);

                    send_text(new_socket,
                              response);

                    continue;
                }

                char output[4096];

                int result =
                    execute_whitelisted_command(command_name,
                                                output,
                                                sizeof(output));

                if (result == -2) {
                    snprintf(response,
                             sizeof(response),
                             "ERR 002 COMMAND_NOT_ALLOWED %s\n",
                             SID_TAG);

                    send_text(new_socket,
                              response);
                }
                else if (result == -1) {
                    snprintf(response,
                             sizeof(response),
                             "ERR 003 COMMAND_EXECUTION_FAILED %s\n",
                             SID_TAG);

                    send_text(new_socket,
                              response);
                }
                else {
                    snprintf(response,
                             sizeof(response),
                             "OK EXEC_RESULT %s %s\n",
                             output,
                             SID_TAG);

                    send_text(new_socket,
                              response);
                }

                continue;
            }

            /*
             * Any command not defined by the current protocol
             * is rejected.
             */
            snprintf(response,
                     sizeof(response),
                     "ERR 002 COMMAND_NOT_ALLOWED %s\n",
                     SID_TAG);

            send_text(new_socket,
                      response);
        }
    }
    else {
        printf("Authentication failed. Dropping connection.\n");

        char response[256];

        snprintf(response,
                 sizeof(response),
                 "ERR 001 AUTH_FAILED %s\n",
                 SID_TAG);

        send_text(new_socket,
                  response);
    }

    close(new_socket);

    return NULL;
}

int main() {
    int server_fd;

    struct sockaddr_in address;

    int opt = 1;

    int addrlen =
        sizeof(address);

    server_fd =
        socket(AF_INET,
               SOCK_STREAM,
               0);

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

    address.sin_family =
        AF_INET;

    address.sin_addr.s_addr =
        INADDR_ANY;

    address.sin_port =
        htons(PORT);

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

    printf("Agent started. Listening on port %d...\n",
           PORT);

    while (1) {
        int new_socket =
            accept(server_fd,
                   (struct sockaddr *)&address,
                   (socklen_t *)&addrlen);

        if (new_socket < 0) {
            perror("Accept failed");
            continue;
        }

        int *client_socket =
            malloc(sizeof(int));

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
