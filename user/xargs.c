#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define NULL (void *)0

int main(int argc, char **argv)
{
    if(argc < 2)
        fprintf(2, "usage: xargs [cmd] args... < args...\n");

    char buf[512];
    char *args[MAXARG];
    int i, idx = 0, tmp;
    uint line_start = 0, line_end, word_start, word_end;
    int over = 0;

    memset(buf, 0, 512);
    for(i = 1; i != argc; ++i)
        args[i - 1] = argv[i];
    --i;

    while((tmp = read(0, buf + idx, 512)))
        idx += tmp;

    //分别处理每行，将一行内容作为参数添加到参数表后，然后执行。
    while(1) {
        int j = i;
        word_start = line_start;
        if(strchr(buf + line_start, '\n') != NULL) {
            line_end = strchr(buf + line_start, '\n') - buf;
            buf[line_end] = '\0';
        } else
            break;

        while(strchr(buf + word_start, ' ') != NULL) {
            word_end = strchr(buf + word_start, ' ') - buf;
            buf[word_end] = '\0';
            args[j++] = buf + word_start;
            word_start = word_end + 1;
        }
        args[j] = buf + word_start;
        line_start = line_end + 1;

        if(fork() == 0)
            exec(args[0], args);
        else
            wait(NULL);

        if(over)
            break;
    }

    exit(0);
}
