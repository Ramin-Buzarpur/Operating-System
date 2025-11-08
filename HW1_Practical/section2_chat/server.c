#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#define MAX_USERS 64

typedef struct {
    char name[128];
    char fifo_path[512];
    int  fifo_fd;
    int  used;
} user_t;
static user_t users[MAX_USERS];
static char server_fifo_path[512];
static int  server_fd_read  = -1;
static int  server_fd_write = -1;
static int  running = 1;

static void handle_sigint(int sig) {
    (void)sig;
    running = 0;
}
static int find_user_index(const char *name) {
    int i;
    for (i = 0; i < MAX_USERS; i += 1) {
        if (users[i].used == 1) {
            if (strcmp(users[i].name, name) == 0) {
                return i;
            }
        }
    }
    return -1;
}
static int add_user(const char *name, const char *room_dir) {
    int i;
    for (i = 0; i < MAX_USERS; i += 1) {
        if (users[i].used == 0) {
            snprintf(users[i].name, sizeof(users[i].name), "%s", name);
            snprintf(users[i].fifo_path, sizeof(users[i].fifo_path),
                     "%s/%s.fifo", room_dir, name);
            users[i].fifo_fd = open(users[i].fifo_path, O_WRONLY);
            if (users[i].fifo_fd < 0) {
                return -1;
            }
            users[i].used = 1;
            return i;
        }
    }
    return -1;
}
static void remove_user(const char *name) {
    int idx = find_user_index(name);
    if (idx >= 0) {
        if (users[idx].fifo_fd >= 0) {
            close(users[idx].fifo_fd);
        }
        users[idx].fifo_fd = -1;
        users[idx].used = 0;
    }
}
static void broadcast_message(const char *msg) {
    int i;
    for (i = 0; i < MAX_USERS; i += 1) {
        if (users[i].used == 1 && users[i].fifo_fd >= 0) {
            dprintf(users[i].fifo_fd, "%s\n", msg);
        }
    }
}
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <room>\n", argv[0]);
        return 1;
    }
    char room[256];
    snprintf(room, sizeof(room), "%s", argv[1]);
    char room_dir[512];
    snprintf(room_dir, sizeof(room_dir), "fifo_files/%s", room);
    mkdir("fifo_files", 0777);
    mkdir(room_dir, 0777);
    snprintf(server_fifo_path, sizeof(server_fifo_path),
             "%s/server.fifo", room_dir);
    unlink(server_fifo_path);
    if (mkfifo(server_fifo_path, 0666) != 0 && errno != EEXIST) {
        perror("mkfifo(server)");
        return 1;
    }
    signal(SIGINT, handle_sigint);
    server_fd_read = open(server_fifo_path, O_RDONLY | O_NONBLOCK);
    if (server_fd_read < 0) {
        perror("open server fifo for read");
        return 1;
    }
    server_fd_write = open(server_fifo_path, O_WRONLY);
    printf("[server] room '%s' ready. FIFO: %s\n", room, server_fifo_path);
    fflush(stdout);
    char buffer[2048];
    while (running) {
        ssize_t n = read(server_fd_read, buffer, sizeof(buffer) - 1);
        if (n <= 0) {
            usleep(50000);
            continue;
        }
        buffer[n] = '\0';
        char *save = NULL;
        char *line = strtok_r(buffer, "\n", &save);
        while (line != NULL) {
            if (strncmp(line, "JOIN ", 5) == 0) {
                char name[128];
                int ok = sscanf(line + 5, "%127s", name);
                if (ok == 1) {
                    if (find_user_index(name) < 0) {
                        int idx = add_user(name, room_dir);
                        if (idx >= 0) {
                            char msg[256];
                            snprintf(msg, sizeof(msg),
                                     "[server] %s joined.", name);
                            broadcast_message(msg);
                        }
                    }
                }
            }
            else if (strncmp(line, "LEAVE ", 6) == 0) {
                char name[128];
                int ok = sscanf(line + 6, "%127s", name);
                if (ok == 1) {
                    char msg[256];
                    snprintf(msg, sizeof(msg),
                             "[server] %s left.", name);
                    broadcast_message(msg);
                    remove_user(name);
                }
            }
            else if (strncmp(line, "MSG ", 4) == 0) {
                char name[128];
                int consumed = 0;
                int ok = sscanf(line + 4, "%127s %n", name, &consumed);
                if (ok == 1) {
                    const char *text = line + 4 + consumed;
                    char msg[1600];
                    snprintf(msg, sizeof(msg), "%s: %s", name, text);
                    broadcast_message(msg);
                }
            }
            line = strtok_r(NULL, "\n", &save);
        }
    }
    broadcast_message("[server] shutting down...");
    int i;
    for (i = 0; i < MAX_USERS; i += 1) {
        if (users[i].used == 1 && users[i].fifo_fd >= 0) {
            close(users[i].fifo_fd);
        }
        users[i].used = 0;
    }
    if (server_fd_read >= 0) {
        close(server_fd_read);
    }
    if (server_fd_write >= 0) {
        close(server_fd_write);
    }
    unlink(server_fifo_path);
    return 0;
}
