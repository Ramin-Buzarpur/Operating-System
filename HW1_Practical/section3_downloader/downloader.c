#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

typedef struct {
    long long start;
    long long end;
    int       index;
    int       is_http;
    char      source[1024];
    char      part_name[64];
} task_t;

static int DEBUG_LOG = 0;
static long long get_local_size(const char *path) {
    struct stat st;
    int ok = stat(path, &st);
    if (ok == 0) {
        return st.st_size;
    }
    return -1;
}
static long long get_http_size(const char *url) {
    char command[2048];
    long long size = -1;
    FILE *fp = NULL;
    snprintf(command, sizeof(command),
             "curl -sIL --connect-timeout 5 --max-time 20 \"%s\" "
             "| tr -d '\\r' | "
             "awk -F': *' 'tolower($1)==\"content-length\"{print $2; exit}'",
             url);
    if (DEBUG_LOG) {
        fprintf(stderr, "[DBG] %s\n", command);
    }
    fp = popen(command, "r");
    if (fp != NULL) {
        if (fscanf(fp, "%lld", &size) != 1) {
            size = -1;
        }
        pclose(fp);
    }
    if (size > 0) {
        return size;
    }
    snprintf(command, sizeof(command),
             "curl -sL --connect-timeout 5 --max-time 20 "
             "-D - -o /dev/null -r 0-0 \"%s\" "
             "| tr -d '\\r' | "
             "awk -F'[/ ]' 'tolower($1$2)==\"content-range:bytes\"{print $5; exit}'",
             url);
    if (DEBUG_LOG) {
        fprintf(stderr, "[DBG] %s\n", command);
    }
    fp = popen(command, "r");
    if (fp != NULL) {
        long long tmp = -1;
        if (fscanf(fp, "%lld", &tmp) == 1) {
            size = tmp;
        }
        pclose(fp);
    }
    if (size > 0) {
        return size;
    }
    snprintf(command, sizeof(command),
             "curl -sL --connect-timeout 5 --max-time 20 "
             "-D - -o /dev/null \"%s\" "
             "| tr -d '\\r' | "
             "awk -F': *' 'tolower($1)==\"content-length\"{print $2; exit}'",
             url);
    if (DEBUG_LOG) {
        fprintf(stderr, "[DBG] %s\n", command);
    }
    fp = popen(command, "r");
    if (fp != NULL) {
        long long tmp = -1;
        if (fscanf(fp, "%lld", &tmp) == 1) {
            size = tmp;
        }
        pclose(fp);
    }
    return size;
}
static const char *base_name(const char *s) {
    const char *p = strrchr(s, '/');
    if (p != NULL) {
        return p + 1;
    }
    return s;
}
static void *worker_func(void *arg) {
    task_t *task = (task_t *)arg;
    if (task->is_http == 1) {
        char command[2048];
        printf("[Thread %d] Downloading bytes %lld-%lld\n",
               task->index + 1, task->start, task->end);
        fflush(stdout);
        snprintf(command, sizeof(command),
                 "curl -sS --fail -L "
                 "--connect-timeout 5 --max-time 20 "
                 "--range %lld-%lld -o \"%s\" \"%s\"",
                 task->start, task->end,
                 task->part_name, task->source);
        if (DEBUG_LOG) {
            fprintf(stderr, "[DBG] %s\n", command);
        }
        (void)system(command);
    }
    else {
        int in_fd = open(task->source, O_RDONLY);
        if (in_fd < 0) {
            perror("open source");
            return NULL;
        }
        int out_fd = open(task->part_name, O_CREAT | O_TRUNC | O_WRONLY, 0666);
        if (out_fd < 0) {
            perror("open part");
            close(in_fd);
            return NULL;
        }
        if (lseek(in_fd, task->start, SEEK_SET) == (off_t)-1) {
            perror("lseek");
        }
        long long remaining = task->end - task->start + 1;
        char buf[1 << 16];
        while (remaining > 0) {
            size_t chunk = sizeof(buf);
            if ((long long)chunk > remaining) {
                chunk = (size_t)remaining;
            }
            ssize_t r = read(in_fd, buf, chunk);
            if (r <= 0) {
                break;
            }
            ssize_t w = write(out_fd, buf, r);
            if (w != r) {
                perror("write");
                break;
            }
            remaining -= r;
        }
        close(in_fd);
        close(out_fd);
        printf("[Thread %d] Copied bytes %lld-%lld\n",
               task->index + 1, task->start, task->end);
        fflush(stdout);
    }
    return NULL;
}
int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <url_or_path> <num_threads>\n", argv[0]);
        return 1;
    }
    const char *source = argv[1];
    int num_threads = atoi(argv[2]);
    if (num_threads <= 0) {
        num_threads = 1;
    }
    if (num_threads > 128) {
        num_threads = 128;
    }
    if (getenv("DOWN_DEBUG") != NULL) {
        DEBUG_LOG = 1;
    }
    int is_http = 0;
    if (strncmp(source, "http://", 7) == 0 ||
        strncmp(source, "https://", 8) == 0) {
        is_http = 1;
    }
    long long total_size = -1;
    if (is_http == 1) {
        total_size = get_http_size(source);
        if (total_size <= 0) {
            const char *base = base_name(source);
            char dest[256];
            snprintf(dest, sizeof(dest), "%s", (*base) ? base : "download.bin");
            char command[2048];
            printf("[Info] Unknown Content-Length -> single-thread download to %s\n",
                   dest);
            snprintf(command, sizeof(command),
                     "curl -sS -L --connect-timeout 5 --max-time 120 "
                     "-o \"%s\" \"%s\"",
                     dest, source);
            if (DEBUG_LOG) {
                fprintf(stderr, "[DBG] %s\n", command);
            }
            int rc = system(command);
            if (rc != 0) {
                fprintf(stderr, "curl failed (rc=%d)\n", rc);
                return 1;
            }
            printf("Download complete: %s\n", dest);
            return 0;
        }
    }
    else {
        total_size = get_local_size(source);
    }
    if (total_size <= 0) {
        fprintf(stderr, "Could not determine size for %s\n", source);
        return 1;
    }
    const char *base = base_name(source);
    char dest[256];
    snprintf(dest, sizeof(dest), "%s", (*base) ? base : "download.bin");
    long long part_size = total_size / num_threads;
    long long remainder = total_size % num_threads;
    pthread_t *threads = (pthread_t *)calloc(num_threads, sizeof(pthread_t));
    task_t    *tasks   = (task_t *)calloc(num_threads, sizeof(task_t));
    int i;
    for (i = 0; i < num_threads; i += 1) {
        long long start = i * part_size + (i < remainder ? i : remainder);
        long long end   = start + part_size - 1 + (i < remainder ? 1 : 0);
        if (i == num_threads - 1) {
            end = total_size - 1;
        }
        tasks[i].start   = start;
        tasks[i].end     = end;
        tasks[i].index   = i;
        tasks[i].is_http = is_http;
        snprintf(tasks[i].source, sizeof(tasks[i].source), "%s", source);
        snprintf(tasks[i].part_name, sizeof(tasks[i].part_name),
                 "part_%d.bin", i);
        if (pthread_create(&threads[i], NULL, worker_func, &tasks[i]) != 0) {
            perror("pthread_create");
            return 1;
        }
    }
    for (i = 0; i < num_threads; i += 1) {
        (void)pthread_join(threads[i], NULL);
    }
    FILE *out = fopen(dest, "wb");
    if (out == NULL) {
        perror("fopen dest");
        return 1;
    }
    for (i = 0; i < num_threads; i += 1) {
        char part_name[64];
        snprintf(part_name, sizeof(part_name), "part_%d.bin", i);
        FILE *in = fopen(part_name, "rb");
        if (in == NULL) {
            perror("fopen part");
            continue;
        }
        char buf[1 << 16];
        size_t r = 0;
        while ((r = fread(buf, 1, sizeof(buf), in)) > 0) {
            size_t w = fwrite(buf, 1, r, out);
            if (w != r) {
                perror("fwrite");
                break;
            }
        }
        fclose(in);
        remove(part_name);
    }
    fclose(out);
    printf("Merging parts...\n");
    printf("Download complete: %s\n", dest);
    return 0;
}
