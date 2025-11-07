#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

static int server_fd = -1;
static int myfifo_fd = -1;
static char myfifo[512];
static char server_fifo[512];
static char myname[128];

static void cleanup(){
    if(server_fd>=0) close(server_fd);
    if(myfifo_fd>=0) close(myfifo_fd);
    unlink(myfifo);
}

static void handle_sigint(int sig){ (void)sig;
    dprintf(server_fd, "LEAVE %s\n", myname);
    cleanup();
    _exit(0);
}

int main(int argc, char **argv){
    if(argc!=3){ fprintf(stderr, "Usage: %s <name> <room>\n", argv[0]); return 1; }
    snprintf(myname,sizeof(myname),"%s", argv[1]);
    char room[256]; snprintf(room,sizeof(room), "%s", argv[2]);

    char dir[512]; snprintf(dir,sizeof(dir), "fifo_files/%s", room);
    mkdir("fifo_files", 0777); mkdir(dir, 0777);
    int n1 = snprintf(myfifo,sizeof(myfifo), "%s/%s.fifo", dir, myname);
    if (n1 < 0 || n1 >= (int)sizeof(myfifo)) { perror("fifo path too long"); return 1; }
    unlink(myfifo);
    if(mkfifo(myfifo, 0666)) { perror("mkfifo client"); return 1; }

    myfifo_fd = open(myfifo, O_RDONLY | O_NONBLOCK);

    int n2 = snprintf(server_fifo, sizeof(server_fifo), "%s/server.fifo", dir);
    if (n2 < 0 || n2 >= (int)sizeof(server_fifo)) { perror("server fifo path too long"); return 1; }
    server_fd = open(server_fifo, O_WRONLY);
    if(server_fd<0){ perror("open server fifo"); return 1; }

    dprintf(server_fd, "JOIN %s\n", myname);

    signal(SIGINT, handle_sigint);

    pid_t child = fork();
    if(child==0){
        char buf[1024];
        while(1){
            ssize_t n = read(myfifo_fd, buf, sizeof(buf)-1);
            if(n<=0){ usleep(50000); continue; }
            buf[n]='\0';
            fputs(buf, stdout); fflush(stdout);
        }
        return 0;
    }

    char line[1024];
    while(fgets(line, sizeof(line), stdin)){
        size_t len = strlen(line); while(len && (line[len-1]=='\n' || line[len-1]=='\r')) line[--len]=0;
        if(len==0) continue;
        dprintf(server_fd, "MSG %s %s\n", myname, line);
    }

    dprintf(server_fd, "LEAVE %s\n", myname);
    cleanup();
    return 0;
}
