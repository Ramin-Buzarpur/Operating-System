#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

typedef struct DirNode {
    char path[PATH_MAX];
    struct DirNode *next;
} DirNode;

static DirNode *q_head = NULL, *q_tail = NULL;
static pthread_mutex_t q_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t q_cv = PTHREAD_COND_INITIALIZER;
static int active_workers = 0;
static int stop_all = 0;

static char target_name[NAME_MAX+1];

static void enqueue(const char *path) {
    DirNode *n = (DirNode*)malloc(sizeof(DirNode));
    if(!n) { perror("malloc"); exit(1);}
    strncpy(n->path, path, sizeof(n->path));
    n->path[sizeof(n->path)-1] = '\0';
    n->next = NULL;
    if(!q_tail) { q_head = q_tail = n; } else { q_tail->next = n; q_tail = n; }
}

static int dequeue(char *out) {
    if(!q_head) return 0;
    DirNode *n = q_head; q_head = n->next; if(!q_head) q_tail = NULL;
    strncpy(out, n->path, PATH_MAX);
    free(n);
    return 1;
}

static void *worker(void *arg){
    (void)arg;
    char dirpath[PATH_MAX];
    while(1){
        pthread_mutex_lock(&q_mtx);
        while(!stop_all && !q_head){
            pthread_cond_wait(&q_cv, &q_mtx);
        }
        if(stop_all){
            pthread_mutex_unlock(&q_mtx);
            break;
        }
        if(!dequeue(dirpath)){
            pthread_mutex_unlock(&q_mtx);
            continue;
        }
        active_workers++;
        pthread_mutex_unlock(&q_mtx);

        DIR *d = opendir(dirpath);
        if(!d){
            goto done_dir;
        }
        struct dirent *ent;
        while((ent = readdir(d)) != NULL){
            if(strcmp(ent->d_name, ".")==0 || strcmp(ent->d_name, "..")==0) continue;
            char child[PATH_MAX];
            int n = snprintf(child, sizeof(child), "%s/%s", dirpath, ent->d_name);
            if(n < 0 || n >= (int)sizeof(child)) continue; // avoid truncation
            struct stat st;
            if(lstat(child, &st) == -1) continue;
            if(S_ISDIR(st.st_mode)){
                pthread_mutex_lock(&q_mtx);
                enqueue(child);
                pthread_cond_signal(&q_cv);
                pthread_mutex_unlock(&q_mtx);
            } else if(S_ISREG(st.st_mode)) {
                if(strcmp(ent->d_name, target_name) == 0){
                    char real[PATH_MAX];
                    if(realpath(child, real) == NULL){
                        strncpy(real, child, sizeof(real)); real[sizeof(real)-1]='\0';
                    }
                    printf("Found by thread %lu: %s\n", (unsigned long)pthread_self(), real);
                    fflush(stdout);
                }
            }
        }
        closedir(d);
    done_dir:
        pthread_mutex_lock(&q_mtx);
        active_workers--;
        if(!q_head && active_workers==0){
            stop_all = 1;
            pthread_cond_broadcast(&q_cv);
        }
        pthread_mutex_unlock(&q_mtx);
    }
    return NULL;
}

int main(int argc, char **argv){
    if(argc != 3){
        fprintf(stderr, "Usage: %s <start_directory> <target_filename>\n", argv[0]);
        return 1;
    }
    char start[PATH_MAX];
    if(realpath(argv[1], start) == NULL){
        perror("realpath");
        return 1;
    }
    strncpy(target_name, argv[2], sizeof(target_name));
    target_name[sizeof(target_name)-1] = '\0';

    pthread_mutex_lock(&q_mtx);
    enqueue(start);
    pthread_mutex_unlock(&q_mtx);

    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    int nthreads = (ncpu>0 && ncpu<64) ? (int)ncpu*2 : 8;

    pthread_t *ths = calloc(nthreads, sizeof(pthread_t));
    if(!ths){ perror("calloc"); return 1; }

    for(int i=0;i<nthreads;i++){
        if(pthread_create(&ths[i], NULL, worker, NULL)!=0){
            perror("pthread_create"); return 1;
        }
    }
    pthread_mutex_lock(&q_mtx);
    pthread_cond_broadcast(&q_cv);
    pthread_mutex_unlock(&q_mtx);

    for(int i=0;i<nthreads;i++) pthread_join(ths[i], NULL);

    printf("Search complete.\n");
    return 0;
}
