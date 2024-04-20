#include "kernel/types.h"
#include "user/user.h"

#define NULL (void *)0
#define SCALE 34

int main(int argc, char **argv)
{
    int *nums;
    int p[2];

    pipe(p);
    if(fork() == 0) {
        //child
child:  
        nums = malloc(SCALE * sizeof(int));
        memset((void *)nums, 0, SCALE * sizeof(int));
        close(p[1]);
        int n = read(p[0], (void *)nums, SCALE * sizeof(int));
        close(p[0]);
        n = n / sizeof(int); //numbers received

        printf("prime %d\n", nums[0]);
        if(n == 1) {
            free(nums);
            exit(0);
        }
        else {
            //pass on
            int prime = nums[0];
            int j = 0; //numbers to pass
            for(int i = 1; i != n; ++i) {
                if(nums[i] % prime)
                    nums[j++] = nums[i];
            }
            if(j == 0) {
                free(nums);
                exit(0);
            }

            pipe(p);
            if(fork()) {
                // parent
                close(p[0]);
                write(p[1], (void *)nums, j * sizeof(int));
                close(p[1]);
            } else
                goto child;
        }
    } else {
        //main process
        nums = malloc(SCALE * sizeof(int));
        memset((void *)nums, 0, SCALE * sizeof(int));
        close(p[0]);
        for(int i = 0; i != SCALE; ++i)
            nums[i] = i + 2;
        write(p[1], (void *)nums, SCALE * sizeof(int));
        close(p[1]);
        wait(NULL);
        free(nums);
        sleep(2);
        exit(0);
    }
}
