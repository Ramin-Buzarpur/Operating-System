#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

#define MAX_QUEUE    4096
#define MAX_THREADS  8

typedef struct {
    char path[PATH_MAX];
} queue_item_t;

static queue_item_t queue_buf[MAX_QUEUE];
static int queue_head = 0;
static int queue_tail = 0;

static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  queue_cv    = PTHREAD_COND_INITIALIZER;

static int should_stop = 0;
static int active_dirs = 0;
static char target_name[NAME_MAX + 1];

static int queue_is_empty(void) {
    return (queue_head == queue_tail);
}

static int queue_is_full(void) {
    int next = (queue_tail + 1) % MAX_QUEUE;
    return (next == queue_head);
}

static int queue_push(const char *path) {
    if (queue_is_full()) {
        return 0;
    }
    strncpy(queue_buf[queue_tail].path, path, PATH_MAX);
    queue_buf[queue_tail].path[PATH_MAX - 1] = '\0';
    queue_tail = (queue_tail + 1) % MAX_QUEUE;
    return 1;
}

static int queue_pop(char *out_path) {
    if (queue_is_empty()) {
        return 0;
    }
    strncpy(out_path, queue_buf[queue_head].path, PATH_MAX);
    queue_head = (queue_head + 1) % MAX_QUEUE;
    return 1;
}

static void *worker_thread(void *arg) {
    (void)arg;
    char dir_path[PATH_MAX];
    while (1) {
        pthread_mutex_lock(&queue_mutex);
        while (!should_stop && queue_is_empty()) {
            pthread_cond_wait(&queue_cv, &queue_mutex);
        }
        if (should_stop) {
            pthread_mutex_unlock(&queue_mutex);
            break;
        }
        if (!queue_pop(dir_path)) {
            pthread_mutex_unlock(&queue_mutex);
            continue;
        }
        active_dirs += 1;
        pthread_mutex_unlock(&queue_mutex);
        DIR *d = opendir(dir_path);
        if (d != NULL) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                if (strcmp(ent->d_name, ".") == 0 ||
                    strcmp(ent->d_name, "..") == 0) {
                    continue;
                }
                char child[PATH_MAX];
                int n = snprintf(child, sizeof(child), "%s/%s",
                                 dir_path, ent->d_name);
                if (n < 0 || n >= (int)sizeof(child)) {
                    continue;
                }
                struct stat st;
                if (lstat(child, &st) == -1) {
                    continue;
                }
                if (S_ISDIR(st.st_mode)) {
                    pthread_mutex_lock(&queue_mutex);
                    if (queue_push(child)) {
                        pthread_cond_signal(&queue_cv);
                    }
                    pthread_mutex_unlock(&queue_mutex);
                }
                else if (S_ISREG(st.st_mode)) {
                    if (strcmp(ent->d_name, target_name) == 0) {
                        char abs_path[PATH_MAX];
                        if (realpath(child, abs_path) == NULL) {
                            strncpy(abs_path, child, sizeof(abs_path));
                            abs_path[sizeof(abs_path) - 1] = '\0';
                        }
                        printf("Found by thread %lu: %s\n",
                               (unsigned long)pthread_self(),
                               abs_path);
                        fflush(stdout);
                    }
                }
            }
            closedir(d);
        }

        pthread_mutex_lock(&queue_mutex);
        active_dirs -= 1;
        if (queue_is_empty() && active_dirs == 0) {
            should_stop = 1;
            pthread_cond_broadcast(&queue_cv);
        }
        pthread_mutex_unlock(&queue_mutex);
    }

    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <start_dir> <target_filename>\n", argv[0]);
        return 1;
    }
    char start_dir[PATH_MAX];
    if (realpath(argv[1], start_dir) == NULL) {
        perror("realpath");
        return 1;
    }
    strncpy(target_name, argv[2], sizeof(target_name));
    target_name[sizeof(target_name) - 1] = '\0';
    pthread_mutex_lock(&queue_mutex);
    (void)queue_push(start_dir);
    pthread_mutex_unlock(&queue_mutex);
    pthread_t threads[MAX_THREADS];
    int i;
    for (i = 0; i < MAX_THREADS; i += 1) {
        if (pthread_create(&threads[i], NULL, worker_thread, NULL) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    pthread_mutex_lock(&queue_mutex);
    pthread_cond_broadcast(&queue_cv);
    pthread_mutex_unlock(&queue_mutex);
    for (i = 0; i < MAX_THREADS; i += 1) {
        (void)pthread_join(threads[i], NULL);
    }
    printf("Search complete.\n");
    return 0;
}
