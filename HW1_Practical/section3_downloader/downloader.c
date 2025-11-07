// downloader.c — robust HTTP/local multi-part downloader (pthread + curl)
// Build: gcc -Wall -O2 -pthread -o downloader downloader.c
// Usage: ./downloader <url_or_path> <num_threads>

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>   // <- for va_list / va_start / va_end

#define CURL_TIMEOUT_OPTS "--connect-timeout 5 --max-time 20"

static int DEBUG_LOG = 0;

typedef struct {
    long long start;
    long long end;
    int idx;
    char url[1024];
    char outname[256];
    int is_http;
} Task;

static void dbg(const char *fmt, ...) {
    if(!DEBUG_LOG) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static void *worker(void *arg){
    Task *t=(Task*)arg;

    if(t->is_http){
        char cmd[2048];
        // -sS: silent but show errors, --fail: fail non-2xx, -L: follow redirects
        snprintf(cmd,sizeof(cmd),
            "curl -sS --fail -L " CURL_TIMEOUT_OPTS " --range %lld-%lld -o \"%s\" \"%s\"",
            t->start, t->end, t->outname, t->url);

        fprintf(stdout,"[Thread %d] Downloading bytes %lld–%lld\n",
                t->idx+1, t->start, t->end);
        fflush(stdout);

        dbg("[DBG] %s\n", cmd);
        int rc = system(cmd);
        if(rc!=0){
            fprintf(stderr,"curl failed for part %d (rc=%d)\n", t->idx, rc);
        }
    } else {
        int infd = open(t->url, O_RDONLY);
        if(infd<0){ perror("open input"); pthread_exit(NULL); }

        int outfd = open(t->outname, O_CREAT|O_TRUNC|O_WRONLY, 0666);
        if(outfd<0){ perror("open output"); close(infd); pthread_exit(NULL); }

        const size_t BUFS=1<<16;
        char *buf=malloc(BUFS);
        if(!buf){ perror("malloc"); close(infd); close(outfd); pthread_exit(NULL); }

        if(lseek(infd, t->start, SEEK_SET)==(off_t)-1){ perror("lseek"); }

        long long remaining = t->end - t->start + 1;
        while(remaining>0){
            size_t chunk = remaining>BUFS?BUFS:(size_t)remaining;
            ssize_t r = read(infd, buf, chunk);
            if(r<=0){ break; }
            ssize_t w = write(outfd, buf, r);
            if(w != r){ perror("write"); break; }
            remaining -= r;
        }

        free(buf);
        close(infd);
        close(outfd);

        fprintf(stdout,"[Thread %d] Copied bytes %lld–%lld\n",
                t->idx+1, t->start, t->end);
        fflush(stdout);
    }

    return NULL;
}

static const char* base_name(const char *url){
    const char *p = strrchr(url,'/');
    return p? p+1 : url;
}

static long long http_content_length(const char *url){
    char cmd[4096];
    long long size = -1;
    FILE *p;

    // 1) HEAD (follow redirects) → Content-Length
    snprintf(cmd,sizeof(cmd),
        "curl -sIL " CURL_TIMEOUT_OPTS " \"%s\" | tr -d '\\r' | "
        "awk -F': *' 'tolower($1)==\"content-length\"{print $2; exit}'",
        url);
    dbg("[DBG] %s\n", cmd);
    p = popen(cmd, "r");
    if(p){
        if(fscanf(p, "%lld", &size)!=1) size=-1;
        pclose(p);
    }
    if(size>0) return size;

    // 2) Range 0-0 → parse Content-Range: bytes 0-0/NNN
    snprintf(cmd,sizeof(cmd),
        "curl -sL " CURL_TIMEOUT_OPTS " -D - -o /dev/null -r 0-0 \"%s\" | tr -d '\\r' | "
        "awk -F'[/ ]' 'tolower($1$2)==\"content-range:bytes\"{print $5; exit}'",
        url);
    dbg("[DBG] %s\n", cmd);
    p = popen(cmd, "r");
    if(p){
        long long tmp=-1;
        if(fscanf(p, "%lld", &tmp)==1) size=tmp;
        pclose(p);
    }
    if(size>0) return size;

    // 3) GET headers only → Content-Length
    snprintf(cmd,sizeof(cmd),
        "curl -sL " CURL_TIMEOUT_OPTS " -D - -o /dev/null \"%s\" | tr -d '\\r' | "
        "awk -F': *' 'tolower($1)==\"content-length\"{print $2; exit}'",
        url);
    dbg("[DBG] %s\n", cmd);
    p = popen(cmd, "r");
    if(p){
        long long tmp=-1;
        if(fscanf(p, "%lld", &tmp)==1) size=tmp;
        pclose(p);
    }
    return size; // may still be -1 (e.g., chunked transfer)
}

int main(int argc, char **argv){
    if(argc!=3){
        fprintf(stderr,"Usage: %s <url_or_path> <num_threads>\n", argv[0]);
        return 1;
    }
    const char *url = argv[1];
    int n = atoi(argv[2]);
    if(n<=0) n=1;
    if(n>128) n=128;

    if (getenv("DOWN_DEBUG")) DEBUG_LOG = 1;

    int is_http = (strncmp(url,"http://",7)==0 || strncmp(url,"https://",8)==0);

    long long size = -1;
    if(is_http){
        size = http_content_length(url);
        if(size<=0){
            fprintf(stderr,"Could not determine content length for %s\n", url);
            fprintf(stderr,"Tip: Try another test URL or run with DOWN_DEBUG=1 to see curl commands.\n");
            return 1;
        }
    } else {
        struct stat st;
        if(stat(url,&st)==0) size = st.st_size;
        else { perror("stat"); return 1; }
    }

    const char *base = base_name(url);
    char dest[256];
    snprintf(dest,sizeof(dest), "%s", *base?base:"download.bin");

    long long part = size / n;
    long long rem  = size % n;

    pthread_t *ths = calloc(n,sizeof(pthread_t));
    Task *tasks    = calloc(n,sizeof(Task));
    if(!ths || !tasks){ fprintf(stderr,"alloc failed\n"); return 1; }

    for(int i=0;i<n;i++){
        long long s = i*part + (i<rem? i: rem);
        long long e = s + part - 1 + (i<rem?1:0);
        if(i==n-1) e = size-1;

        tasks[i].start = s;
        tasks[i].end   = e;
        tasks[i].idx   = i;
        tasks[i].is_http = is_http;

        snprintf(tasks[i].url, sizeof(tasks[i].url), "%s", url);
        snprintf(tasks[i].outname, sizeof(tasks[i].outname), "part_%d.bin", i);

        if(pthread_create(&ths[i], NULL, worker, &tasks[i])!=0){
            perror("pthread_create");
            return 1;
        }
    }

    for(int i=0;i<n;i++){
        pthread_join(ths[i], NULL);
    }

    FILE *out = fopen(dest, "wb");
    if(!out){ perror("fopen dest"); return 1; }

    for(int i=0;i<n;i++){
        char name[64];
        snprintf(name,sizeof(name),"part_%d.bin", i);
        FILE *in = fopen(name, "rb");
        if(!in){ perror("fopen part"); continue; }

        char buf[1<<16];
        size_t r;
        while((r=fread(buf,1,sizeof(buf),in))>0){
            if(fwrite(buf,1,r,out)!=r){
                perror("fwrite");
                break;
            }
        }
        fclose(in);
        remove(name);
    }
    fclose(out);

    printf("Merging parts...\nDownload complete: %s\n", dest);
    return 0;
}
