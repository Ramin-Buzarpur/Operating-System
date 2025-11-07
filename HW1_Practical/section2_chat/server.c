#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

typedef struct Member {
    char name[128];
    char fifo_path[512];
    int fd;
    struct Member *next;
} Member;

static Member *members = NULL;
static char server_fifo[512];
static int server_fd_r = -1, server_fd_w = -1;
static int running = 1;

static void broadcast(const char *msg){
    for(Member *m = members; m; m=m->next){
        if(m->fd >= 0){
            dprintf(m->fd, "%s\n", msg);
        }
    }
}

static Member* find_member(const char *name){
    for(Member *m=members;m;m=m->next){ if(strcmp(m->name, name)==0) return m; }
    return NULL;
}

static void remove_member(const char *name){
    Member **pp = &members; Member *cur;
    while((cur=*pp)){
        if(strcmp(cur->name,name)==0){
            *pp = cur->next;
            if(cur->fd>=0) close(cur->fd);
            free(cur);
            break;
        }
        pp = &cur->next;
    }
}

static void handle_sigint(int sig){ (void)sig; running=0; }

int main(int argc, char **argv){
    if(argc!=2){ fprintf(stderr, "Usage: %s <room>\n", argv[0]); return 1; }
    char room[256]; snprintf(room,sizeof(room),"%s", argv[1]);
    char dir[512]; snprintf(dir,sizeof(dir),"fifo_files/%s", room);
    mkdir("fifo_files", 0777);
    mkdir(dir, 0777);
    int n0 = snprintf(server_fifo, sizeof(server_fifo), "%s/server.fifo", dir);
    if (n0 < 0 || n0 >= (int)sizeof(server_fifo)) { fprintf(stderr,"room path too long\n"); return 1; }
    unlink(server_fifo);
    if(mkfifo(server_fifo, 0666) && errno!=EEXIST){ perror("mkfifo server"); return 1; }

    signal(SIGINT, handle_sigint);

    server_fd_r = open(server_fifo, O_RDONLY | O_NONBLOCK);
    if(server_fd_r<0){ perror("open server fifo R"); return 1; }
    server_fd_w = open(server_fifo, O_WRONLY);

    char buf[2048];
    printf("[server] room '%s' ready. FIFO: %s\n", room, server_fifo);
    fflush(stdout);

    while(running){
        ssize_t n = read(server_fd_r, buf, sizeof(buf)-1);
        if(n<=0){ usleep(50000); continue; }
        buf[n]='\0';
        char *saveptr; char *line = strtok_r(buf, "\n", &saveptr);
        while(line){
            if(strncmp(line,"JOIN ",5)==0){
                char name[128];
                if(sscanf(line+5, "%127s", name)==1){
                    if(find_member(name)){
                        /* already joined */
                    } else {
                        char mfifo[512]; int n1 = snprintf(mfifo,sizeof(mfifo), "%s/%s.fifo", dir, name);
                        if (n1 < 0 || n1 >= (int)sizeof(mfifo)) { line = strtok_r(NULL, "\n", &saveptr); continue; }
                        int fd = open(mfifo, O_WRONLY);
                        if(fd>=0){
                            Member *m = calloc(1,sizeof(Member));
                            snprintf(m->name,sizeof(m->name),"%s", name);
                            snprintf(m->fifo_path,sizeof(m->fifo_path),"%s", mfifo);
                            m->fd = fd; m->next = members; members = m;
                            char notice[256]; snprintf(notice,sizeof(notice),"[server] %s joined.", name);
                            broadcast(notice);
                        }
                    }
                }
            } else if(strncmp(line,"LEAVE ",6)==0){
                char name[128]; if(sscanf(line+6, "%127s", name)==1){
                    char notice[256]; snprintf(notice,sizeof(notice),"[server] %s left.", name);
                    broadcast(notice);
                    remove_member(name);
                }
            } else if(strncmp(line, "MSG ", 4)==0){
                char name[128];
                const char *p = line+4; while(*p==' ') p++;
                int consumed=0; if(sscanf(p, "%127s %n", name, &consumed)==1){
                    const char *text = p + consumed;
                    char msg[1600]; snprintf(msg,sizeof(msg),"%s: %s", name, text);
                    broadcast(msg);
                }
            }
            line = strtok_r(NULL, "\n", &saveptr);
        }
    }

    broadcast("[server] shutting down...");
    for(Member *m = members; m; ){
        Member *n = m->next; if(m->fd>=0) close(m->fd); free(m); m=n;
    }
    if(server_fd_r>=0) close(server_fd_r);
    if(server_fd_w>=0) close(server_fd_w);
    unlink(server_fifo);
    return 0;
}
