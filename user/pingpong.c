#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char **argv) {
    int pid;
    char buf[] = "X";
    int p2c[2];
    int c2p[2];
    pipe(p2c);
    pipe(c2p);

    if(fork() == 0) {
        //child
        close(p2c[0]);
        close(c2p[1]);
        pid = getpid();
        read(p2c[1], buf, 1);
        printf("%d: received ping\n", pid);
        write(c2p[0], buf, 1);
        close(p2c[1]);
        close(c2p[0]);
    } else {
        //parent
        close(p2c[1]);
        close(c2p[0]);
        pid = getpid();
        write(p2c[0], buf, 1);
        read(c2p[1], buf, 1);
        sleep(5);
        printf("%d: received pong\n", pid);
        close(p2c[0]);
        close(c2p[1]);
    }
    exit(0);
}
