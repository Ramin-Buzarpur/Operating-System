
// client.c — کلاینت FIFO (ساده و خوانا، بدون یک‌خطی‌های فشرده)
// Build: gcc -Wall -O2 -o client client.c
// Run (WSL بهتر است در /tmp): ./client <name> <room>

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

static char my_name[128];
static char my_fifo_path[512];
static char server_fifo_path[512];

static int my_fifo_fd = -1;
static int server_fifo_fd = -1;

static void cleanup(void) {
    if (my_fifo_fd >= 0) {
        close(my_fifo_fd);
        my_fifo_fd = -1;
    }
    if (server_fifo_fd >= 0) {
        close(server_fifo_fd);
        server_fifo_fd = -1;
    }
    unlink(my_fifo_path);
}

static void handle_sigint(int sig) {
    (void)sig;
    if (server_fifo_fd >= 0) {
        dprintf(server_fifo_fd, "LEAVE %s\n", my_name);
    }
    cleanup();
    _exit(0);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <name> <room>\n", argv[0]);
        return 1;
    }
    snprintf(my_name, sizeof(my_name), "%s", argv[1]);
    char room[256];
    snprintf(room, sizeof(room), "%s", argv[2]);
    char room_dir[512];
    snprintf(room_dir, sizeof(room_dir), "fifo_files/%s", room);
    mkdir("fifo_files", 0777);
    mkdir(room_dir, 0777);
    snprintf(my_fifo_path, sizeof(my_fifo_path),
             "%s/%s.fifo", room_dir, my_name);
    unlink(my_fifo_path);
    if (mkfifo(my_fifo_path, 0666) != 0) {
        perror("mkfifo (client)");
        return 1;
    }
    my_fifo_fd = open(my_fifo_path, O_RDONLY | O_NONBLOCK);
    if (my_fifo_fd < 0) {
        perror("open my fifo");
        return 1;
    }
    snprintf(server_fifo_path, sizeof(server_fifo_path),
             "%s/server.fifo", room_dir);
    server_fifo_fd = open(server_fifo_path, O_WRONLY);
    if (server_fifo_fd < 0) {
        perror("open server fifo");
        cleanup();
        return 1;
    }
    dprintf(server_fifo_fd, "JOIN %s\n", my_name);
    signal(SIGINT, handle_sigint);
    pid_t child = fork();
    if (child == 0) {
        char buf[1024];
        while (1) {
            ssize_t n = read(my_fifo_fd, buf, sizeof(buf) - 1);
            if (n <= 0) {
                usleep(50000);
                continue;
            }

            buf[n] = '\0';
            fputs(buf, stdout);
            fflush(stdout);
        }
        return 0;
    }
    char line[1024];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        size_t len = strlen(line);
        while (len > 0 &&
               (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[len - 1] = '\0';
            len -= 1;
        }

        if (len == 0) {
            continue;
        }
        dprintf(server_fifo_fd, "MSG %s %s\n", my_name, line);
    }
    dprintf(server_fifo_fd, "LEAVE %s\n", my_name);
    cleanup();
    return 0;
}
