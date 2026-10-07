#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>

#define PORT 9410
#define AUTH_TOKEN "OPS-2851"
#define SID_TAG "SID:1582"
#define REG_NUMBER "IT24102851"

#define BUFFER_SIZE 65536
#define MAX_FILE_SIZE (10LL * 1024LL * 1024LL)

#define STORAGE_ROOT "./agentfiles"
#define STORAGE_DIR "./agentfiles/" REG_NUMBER

#define MONITOR_INTERVAL 5

typedef struct {
    int active;
    int running;
    int udp_sock;
    struct sockaddr_in destination;
    pthread_t thread_id;
    pthread_mutex_t mutex;
} MonitorContext;

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

int discard_bytes(int sock, long long length) {
    char buffer[4096];

    long long remaining = length;

    while (remaining > 0) {
        size_t chunk_size =
            remaining > (long long)sizeof(buffer)
            ? sizeof(buffer)
            : (size_t)remaining;

        ssize_t received =
            recv(sock,
                 buffer,
                 chunk_size,
                 0);

        if (received <= 0) {
            return -1;
        }

        remaining -= received;
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
 * Collect the current system statistics.
 */
int get_system_stats(double *cpu_load,
                     long *mem_used_mb,
                     long *uptime_sec) {

    FILE *file;

    long mem_total_kb = 0;
    long mem_available_kb = 0;

    double uptime;

    /*
     * CPU load from /proc/loadavg.
     */
    file = fopen("/proc/loadavg", "r");

    if (file == NULL) {
        return -1;
    }

    if (fscanf(file, "%lf", cpu_load) != 1) {
        fclose(file);
        return -1;
    }

    fclose(file);

    /*
     * Memory from /proc/meminfo.
     */
    file = fopen("/proc/meminfo", "r");

    if (file == NULL) {
        return -1;
    }

    char line[256];

    while (fgets(line,
                 sizeof(line),
                 file) != NULL) {

        if (strncmp(line,
                    "MemTotal:",
                    9) == 0) {

            sscanf(line + 9,
                   "%ld",
                   &mem_total_kb);
        }
        else if (strncmp(line,
                          "MemAvailable:",
                          13) == 0) {

            sscanf(line + 13,
                   "%ld",
                   &mem_available_kb);
        }
    }

    fclose(file);

    /*
     * Uptime from /proc/uptime.
     */
    file = fopen("/proc/uptime", "r");

    if (file == NULL) {
        return -1;
    }

    if (fscanf(file, "%lf", &uptime) != 1) {
        fclose(file);
        return -1;
    }

    fclose(file);

    *mem_used_mb =
        (mem_total_kb - mem_available_kb) / 1024;

    *uptime_sec =
        (long)uptime;

    return 0;
}

int get_sysinfo(char *response,
                size_t response_size) {

    double cpu_load;

    long mem_used_mb;
    long uptime_sec;

    if (get_system_stats(&cpu_load,
                         &mem_used_mb,
                         &uptime_sec) < 0) {

        return -1;
    }

    snprintf(response,
             response_size,
             "OK SYSINFO %.2f %ld %ld %s\n",
             cpu_load,
             mem_used_mb,
             uptime_sec,
             SID_TAG);

    return 0;
}

int get_process_list(char *response,
                     size_t response_size) {

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

    while (fgets(line,
                 sizeof(line),
                 file) != NULL) {

        int pid;

        char command[128];

        if (sscanf(line,
                   "%d %127s",
                   &pid,
                   command) != 2) {
            continue;
        }

        int written;

        if (first_process) {

            written =
                snprintf(response + used,
                         response_size - used,
                         "%d/%s",
                         pid,
                         command);

            first_process = 0;
        }
        else {

            written =
                snprintf(response + used,
                         response_size - used,
                         ",%d/%s",
                         pid,
                         command);
        }

        if (written < 0 ||
            (size_t)written >=
            response_size - used) {

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

int execute_whitelisted_command(
    const char *command,
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

    FILE *file =
        popen(shell_command, "r");

    if (file == NULL) {
        return -1;
    }

    size_t used =
        fread(output,
              1,
              output_size - 1,
              file);

    output[used] = '\0';

    pclose(file);

    for (size_t i = 0;
         i < used;
         i++) {

        if (output[i] == '\n' ||
            output[i] == '\r') {

            output[i] = ' ';
        }
    }

    while (used > 0 &&
           (output[used - 1] == ' ' ||
            output[used - 1] == '\t')) {

        output[used - 1] = '\0';
        used--;
    }

    return 0;
}

int monitor_is_active(MonitorContext *monitor) {
    int active;

    pthread_mutex_lock(&monitor->mutex);

    active = monitor->active;

    pthread_mutex_unlock(&monitor->mutex);

    return active;
}

void *monitor_thread(void *arg) {

    MonitorContext *monitor =
        (MonitorContext *)arg;

    while (monitor_is_active(monitor)) {

        double cpu_load;
        long mem_used_mb;
        long uptime_sec;

        if (get_system_stats(&cpu_load,
                             &mem_used_mb,
                             &uptime_sec) == 0) {

            char packet[512];

            snprintf(packet,
                     sizeof(packet),
                     "SYSINFO %.2f %ld %ld %s\n",
                     cpu_load,
                     mem_used_mb,
                     uptime_sec,
                     SID_TAG);

            sendto(monitor->udp_sock,
                   packet,
                   strlen(packet),
                   0,
                   (struct sockaddr *)&monitor->destination,
                   sizeof(monitor->destination));

            printf("UDP monitor sent: %s",
                   packet);
        }

        /*
         * Sleep in one-second pieces so MONITOR STOP
         * doesn't have to wait the full 5 seconds.
         */
        for (int i = 0;
             i < MONITOR_INTERVAL;
             i++) {

            if (!monitor_is_active(monitor)) {
                break;
            }

            sleep(1);
        }
    }

    return NULL;
}

int start_monitor(MonitorContext *monitor,
                  int client_socket,
                  int udp_port) {

    pthread_mutex_lock(&monitor->mutex);

    if (monitor->running) {
        pthread_mutex_unlock(&monitor->mutex);
        return -2;
    }

    pthread_mutex_unlock(&monitor->mutex);

    if (udp_port < 1 || udp_port > 65535) {
        return -3;
    }

    struct sockaddr_in client_address;

    socklen_t client_length =
        sizeof(client_address);

    memset(&client_address,
           0,
           sizeof(client_address));

    if (getpeername(client_socket,
                    (struct sockaddr *)&client_address,
                    &client_length) < 0) {

        return -1;
    }

    monitor->udp_sock =
        socket(AF_INET,
               SOCK_DGRAM,
               0);

    if (monitor->udp_sock < 0) {
        return -1;
    }

    memset(&monitor->destination,
           0,
           sizeof(monitor->destination));

    monitor->destination.sin_family =
        AF_INET;

    monitor->destination.sin_port =
        htons((uint16_t)udp_port);

    monitor->destination.sin_addr =
        client_address.sin_addr;

    pthread_mutex_lock(&monitor->mutex);

    monitor->active = 1;
    monitor->running = 1;

    pthread_mutex_unlock(&monitor->mutex);

    if (pthread_create(&monitor->thread_id,
                       NULL,
                       monitor_thread,
                       monitor) != 0) {

        pthread_mutex_lock(&monitor->mutex);

        monitor->active = 0;
        monitor->running = 0;

        pthread_mutex_unlock(&monitor->mutex);

        close(monitor->udp_sock);

        monitor->udp_sock = -1;

        return -1;
    }

    return 0;
}

void stop_monitor(MonitorContext *monitor) {

    pthread_mutex_lock(&monitor->mutex);

    int was_running =
        monitor->running;

    monitor->active = 0;

    pthread_mutex_unlock(&monitor->mutex);

    if (was_running) {
        pthread_join(monitor->thread_id,
                     NULL);

        close(monitor->udp_sock);

        monitor->udp_sock = -1;

        pthread_mutex_lock(&monitor->mutex);

        monitor->running = 0;

        pthread_mutex_unlock(&monitor->mutex);
    }
}

int handle_put(int new_socket,
               const char *filename,
               long long file_size) {

    if (!is_safe_filename(filename)) {
        return -2;
    }

    if (file_size < 0 ||
        file_size > MAX_FILE_SIZE) {

        return -3;
    }

    char path[512];

    snprintf(path,
             sizeof(path),
             "%s/%s",
             STORAGE_DIR,
             filename);

    FILE *file =
        fopen(path, "wb");

    if (file == NULL) {
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

        ssize_t received =
            recv(new_socket,
                 file_buffer,
                 chunk_size,
                 0);

        if (received <= 0) {
            fclose(file);
            return -1;
        }

        if (fwrite(file_buffer,
                   1,
                   (size_t)received,
                   file)
            != (size_t)received) {

            fclose(file);
            return -1;
        }

        remaining -= received;
    }

    fclose(file);

    return 0;
}

int handle_get(int new_socket,
               const char *filename) {

    if (!is_safe_filename(filename)) {
        return -2;
    }

    char path[512];

    snprintf(path,
             sizeof(path),
             "%s/%s",
             STORAGE_DIR,
             filename);

    struct stat file_stat;

    if (stat(path,
             &file_stat) != 0 ||
        !S_ISREG(file_stat.st_mode)) {

        return -3;
    }

    long long file_size =
        (long long)file_stat.st_size;

    FILE *file =
        fopen(path, "rb");

    if (file == NULL) {
        return -3;
    }

    char response[512];

    snprintf(response,
             sizeof(response),
             "OK FILE_SEND %s %lld %s\n",
             filename,
             file_size,
             SID_TAG);

    if (send_text(new_socket,
                  response) < 0) {

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

        if (send_all(new_socket,
                     file_buffer,
                     bytes_read) < 0) {

            fclose(file);
            return -1;
        }
    }

    fclose(file);

    return 0;
}

void *handle_client(void *arg) {

    int new_socket =
        *((int *)arg);

    free(arg);

    char buffer[BUFFER_SIZE];

    MonitorContext monitor;

    monitor.active = 0;
    monitor.running = 0;
    monitor.udp_sock = -1;

    pthread_mutex_init(&monitor.mutex,
                       NULL);

    printf("Controller connected!\n");

    int valread =
        recv_line(new_socket,
                  buffer,
                  sizeof(buffer));

    if (valread <= 0) {

        printf("Controller disconnected before authentication.\n");

        pthread_mutex_destroy(&monitor.mutex);

        close(new_socket);

        return NULL;
    }

    char expected_auth[128];

    snprintf(expected_auth,
             sizeof(expected_auth),
             "AUTH %s\n",
             AUTH_TOKEN);

    if (strcmp(buffer,
               expected_auth) != 0) {

        char response[256];

        snprintf(response,
                 sizeof(response),
                 "ERR 001 AUTH_FAILED %s\n",
                 SID_TAG);

        send_text(new_socket,
                  response);

        pthread_mutex_destroy(&monitor.mutex);

        close(new_socket);

        return NULL;
    }

    printf("Authentication successful. Ready for commands.\n");

    char response[8192];

    snprintf(response,
             sizeof(response),
             "OK AUTHENTICATED %s\n",
             SID_TAG);

    send_text(new_socket,
              response);

    while (1) {

        memset(buffer,
               0,
               sizeof(buffer));

        int cmd_read =
            recv_line(new_socket,
                      buffer,
                      sizeof(buffer));

        if (cmd_read <= 0) {

            printf("Controller disconnected.\n");

            break;
        }

        buffer[strcspn(buffer,
                       "\n")] = '\0';

        printf("Received command: %s\n",
               buffer);

        /*
         * Handle the required QUIT command.
         * Stop any active monitoring, send the goodbye response,
         * and then close this Controller session.
         */
        if (strcmp(buffer, "QUIT") == 0) {

            stop_monitor(&monitor);

            snprintf(response,
                     sizeof(response),
                     "OK BYE %s\n",
                     SID_TAG);

            send_text(new_socket,
                      response);

            printf("Controller requested QUIT.\n");

            break;
         }


        /*
         * SYSINFO
         */
        if (strcmp(buffer,
                   "SYSINFO") == 0) {

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
        if (strcmp(buffer,
                   "LISTPROC") == 0) {

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
         * EXEC
         */
        if (strncmp(buffer,
                    "EXEC ",
                    5) == 0) {

            char command_name[64];

            char extra[64];

            int parsed =
                sscanf(buffer + 5,
                       "%63s %63s",
                       command_name,
                       extra);

            if (parsed != 1) {

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
                execute_whitelisted_command(
                    command_name,
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
         * PUT
         */
        if (strncmp(buffer,
                    "PUT ",
                    4) == 0) {

            char filename[256];

            char extra[64];

            long long file_size;

            int parsed =
                sscanf(buffer + 4,
                       "%255s %lld %63s",
                       filename,
                       &file_size,
                       extra);

            if (parsed != 2 ||
                file_size < 0) {

                snprintf(response,
                         sizeof(response),
                         "ERR 006 INVALID_REQUEST %s\n",
                         SID_TAG);

                send_text(new_socket,
                          response);

                continue;
            }

            if (file_size > MAX_FILE_SIZE) {

                if (discard_bytes(new_socket,
                                  file_size) < 0) {

                    break;
                }

                snprintf(response,
                         sizeof(response),
                         "ERR 004 FILE_TOO_LARGE %s\n",
                         SID_TAG);

                send_text(new_socket,
                          response);

                continue;
            }

            if (!is_safe_filename(filename)) {

                if (discard_bytes(new_socket,
                                  file_size) < 0) {

                    break;
                }

                snprintf(response,
                         sizeof(response),
                         "ERR 006 INVALID_FILENAME %s\n",
                         SID_TAG);

                send_text(new_socket,
                          response);

                continue;
            }

            int put_result =
                handle_put(new_socket,
                           filename,
                           file_size);

            if (put_result == 0) {

                snprintf(response,
                         sizeof(response),
                         "OK FILE_RECEIVED %s %s\n",
                         filename,
                         SID_TAG);

                send_text(new_socket,
                          response);
            }
            else if (put_result == -2) {

                snprintf(response,
                         sizeof(response),
                         "ERR 006 INVALID_FILENAME %s\n",
                         SID_TAG);

                send_text(new_socket,
                          response);
            }
            else {

                snprintf(response,
                         sizeof(response),
                         "ERR 007 FILE_WRITE_FAILED %s\n",
                         SID_TAG);

                send_text(new_socket,
                          response);
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

            char extra[64];

            int parsed =
                sscanf(buffer + 4,
                       "%255s %63s",
                       filename,
                       extra);

            if (parsed != 1) {

                snprintf(response,
                         sizeof(response),
                         "ERR 006 INVALID_REQUEST %s\n",
                         SID_TAG);

                send_text(new_socket,
                          response);

                continue;
            }

            int get_result =
                handle_get(new_socket,
                           filename);

            if (get_result == -2) {

                snprintf(response,
                         sizeof(response),
                         "ERR 006 INVALID_FILENAME %s\n",
                         SID_TAG);

                send_text(new_socket,
                          response);
            }
            else if (get_result == -3) {

                snprintf(response,
                         sizeof(response),
                         "ERR 005 FILE_NOT_FOUND %s\n",
                         SID_TAG);

                send_text(new_socket,
                          response);
            }

            continue;
        }

        /*
         * MONITOR START <udp_port>
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

                snprintf(response,
                         sizeof(response),
                         "ERR 006 INVALID_REQUEST %s\n",
                         SID_TAG);

                send_text(new_socket,
                          response);

                continue;
            }

            int monitor_result =
                start_monitor(&monitor,
                              new_socket,
                              udp_port);

            if (monitor_result == -2) {

                snprintf(response,
                         sizeof(response),
                         "ERR 008 MONITOR_ALREADY_RUNNING %s\n",
                         SID_TAG);
            }
            else if (monitor_result != 0) {

                snprintf(response,
                         sizeof(response),
                         "ERR 009 MONITOR_START_FAILED %s\n",
                         SID_TAG);
            }
            else {

                snprintf(response,
                         sizeof(response),
                         "OK MONITOR_STARTED %s\n",
                         SID_TAG);
            }

            send_text(new_socket,
                      response);

            continue;
        }

        /*
         * MONITOR STOP
         */
        if (strcmp(buffer,
                   "MONITOR STOP") == 0) {

            stop_monitor(&monitor);

            snprintf(response,
                     sizeof(response),
                     "OK MONITOR_STOPPED %s\n",
                     SID_TAG);

            send_text(new_socket,
                      response);

            continue;
        }

        /*
         * Unknown command.
         */
        snprintf(response,
                 sizeof(response),
                 "ERR 002 COMMAND_NOT_ALLOWED %s\n",
                 SID_TAG);

        send_text(new_socket,
                  response);
    }

    stop_monitor(&monitor);

    pthread_mutex_destroy(&monitor.mutex);

    close(new_socket);

    return NULL;
}

int main() {

    if (mkdir(STORAGE_ROOT,
              0755) < 0 &&
        errno != EEXIST) {

        perror("Cannot create agentfiles directory");
        exit(EXIT_FAILURE);
    }

    if (mkdir(STORAGE_DIR,
              0755) < 0 &&
        errno != EEXIST) {

        perror("Cannot create personalized storage directory");
        exit(EXIT_FAILURE);
    }

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

    if (listen(server_fd,
               10) < 0) {

        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Agent started. Listening on port %d...\n",
           PORT);

    printf("File storage: %s\n",
           STORAGE_DIR);

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

        *client_socket =
            new_socket;

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
