#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX 100

static int clamp(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}
int main(void) {
    int M, N, K, L;
    if (scanf("%d %d %d %d", &M, &N, &K, &L) != 4) {
        fprintf(stderr, "Need M N K L\n");
        return 1;
    }
    if (M <= 0 || N <= 0 || K <= 0 || L <= 0 ||
        M > MAX || N > MAX) {
        fprintf(stderr, "Bad sizes\n");
        return 1;
    }
    double (*A)[MAX] = mmap(NULL, sizeof(double) * MAX * MAX,
                            PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    double (*B)[MAX] = mmap(NULL, sizeof(double) * MAX * MAX,
                            PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (A == MAP_FAILED || B == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    int i, j;
    for (i = 0; i < M; i += 1) {
        for (j = 0; j < N; j += 1) {
            if (scanf("%lf", &A[i][j]) != 1) {
                fprintf(stderr, "Not enough data\n");
                return 1;
            }
        }
    }
    for (i = 0; i < M; i += 1) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }
        if (pid == 0) {
            fprintf(stdout, "[PID %d] Processing row %d...\n",
                    getpid(), i);
            int kh = K / 2;
            int kw = L / 2;
            for (j = 0; j < N; j += 1) {
                int r0 = clamp(i - kh, 0, M - 1);
                int r1 = clamp(i + (K - 1 - kh), 0, M - 1);
                int c0 = clamp(j - kw, 0, N - 1);
                int c1 = clamp(j + (L - 1 - kw), 0, N - 1);
                double mn = A[r0][c0];
                double mx = A[r0][c0];
                int r, c;
                for (r = r0; r <= r1; r += 1) {
                    for (c = c0; c <= c1; c += 1) {
                        if (A[r][c] < mn) {
                            mn = A[r][c];
                        }
                        if (A[r][c] > mx) {
                            mx = A[r][c];
                        }
                    }
                }
                B[i][j] = mx - mn;
            }
            fprintf(stdout, "[PID %d] Finished row %d\n", getpid(), i);
            fflush(stdout);
            _exit(0);
        }
    }
    for (i = 0; i < M; i += 1) {
        int st = 0;
        (void)wait(&st);
    }
    for (i = 0; i < M; i += 1) {
        for (j = 0; j < N; j += 1) {
            if (j > 0) {
                printf(" ");
            }
            printf("%.6f", B[i][j]);
        }
        printf("\n");
    }
    return 0;
}
