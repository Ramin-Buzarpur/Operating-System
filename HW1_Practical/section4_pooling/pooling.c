```c
// pooling.c — Local Gradient Pooling (multi-process + shared memory)
// Build: gcc -Wall -O2 -o pooling pooling.c
// Run:   echo "M N K L\n<matrix MxN>" | ./pooling
// Output: MxN matrix where M2[i][j] = max(window) - min(window)

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX 100

static inline int clamp(int v, int lo, int hi){ if(v<lo) return lo; if(v>hi) return hi; return v; }

int main(void){
    int M,N,K,L;
    if(scanf("%d %d %d %d", &M,&N,&K,&L)!=4){
        fprintf(stderr,"Provide M N K L then MxN matrix values.\n");
        return 1;
    }
    if(M<=0||N<=0||K<=0||L<=0||M>MAX||N>MAX||K>MAX||L>MAX){
        fprintf(stderr,"Invalid sizes.\n");
        return 1;
    }

    // Shared memory for input and output matrices
    double (*M1)[MAX] = mmap(NULL, sizeof(double)*MAX*MAX, PROT_READ|PROT_WRITE,
                             MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    double (*M2)[MAX] = mmap(NULL, sizeof(double)*MAX*MAX, PROT_READ|PROT_WRITE,
                             MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    if(M1==MAP_FAILED||M2==MAP_FAILED){ perror("mmap"); return 1; }

    // Read matrix
    for(int i=0;i<M;i++)
        for(int j=0;j<N;j++)
            if(scanf("%lf", &M1[i][j])!=1){ fprintf(stderr,"Not enough data.\n"); return 1; }

    // One process per output row
    for(int i=0;i<M;i++){
        pid_t pid = fork();
        if(pid<0){ perror("fork"); return 1; }
        if(pid==0){
            fprintf(stdout,"[PID %d] Processing row %d...\n", getpid(), i);
            int kh = K/2, kw = L/2;
            for(int j=0;j<N;j++){
                int r0 = clamp(i-kh, 0, M-1);
                int r1 = clamp(i+(K-1-kh), 0, M-1);
                int c0 = clamp(j-kw, 0, N-1);
                int c1 = clamp(j+(L-1-kw), 0, N-1);
                double mn = M1[r0][c0], mx = M1[r0][c0];
                for(int r=r0;r<=r1;r++)
                    for(int c=c0;c<=c1;c++){
                        if(M1[r][c]<mn) mn=M1[r][c];
                        if(M1[r][c]>mx) mx=M1[r][c];
                    }
                M2[i][j] = mx - mn;
            }
            fprintf(stdout,"[PID %d] Finished row %d\n", getpid(), i);
            fflush(stdout);           // ensure logs appear when captured via pipe/$(...)
            _exit(0);
        }
    }

    // Wait for all children
    for(int i=0;i<M;i++){ int st; wait(&st); (void)st; }

    // Print result matrix
    for(int i=0;i<M;i++){
        for(int j=0;j<N;j++){
            if(j) printf(" ");
            printf("%.6f", M2[i][j]);
        }
        printf("\n");
    }
    return 0;
}
```
