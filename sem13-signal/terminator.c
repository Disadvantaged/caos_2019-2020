// %%cpp terminator.c

#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <sys/types.h>

sig_atomic_t last_signal = 0;

static void handler(int signum) {
    last_signal = signum;  
}

int main() {
    int signals[] = {SIGUSR1, SIGUSR2, 0};
    for (int* signal = signals; *signal; ++signal) {
        sigaction(*signal,
                  &(struct sigaction)
                  {.sa_handler=handler, .sa_flags=SA_RESTART},
                  NULL);
    }
    sigset_t mask;
    sigfillset(&mask);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    sigemptyset(&mask);
    
    int parent_pid = getpid();
    
    int child_pid = fork();
    assert(child_pid >= 0);
    if (child_pid == 0) {
        while (1) {
            sigsuspend(&mask);
            if (last_signal) {
                if (last_signal == SIGUSR1) {
                    printf("Child process: Pong\n"); fflush(stdout);
                    kill(parent_pid, SIGUSR1);
                } else {
                    printf("Child process finish\n"); fflush(stdout);
                    return 0;
                }
                last_signal = 0;
            }
        }
    } else {
        for (int i = 0; i < 10; ++i) {
            printf("Child process: Ping\n"); fflush(stdout);
            kill(child_pid)
            while (1) {
                sigsuspend(&mask);
                if (last_signal) { last_signal = 0; break; }
            }
        }
    }
    
    return res;
}

