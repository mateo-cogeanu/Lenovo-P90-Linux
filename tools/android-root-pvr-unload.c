#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define STATUS "/data/local/tmp/p90-pvr-unload.txt"

int main(void)
{
    char line[64];
    char *argv[] = {"rmmod", "dfrgx", NULL};
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int status = -1;
    int length;
    pid_t child;

    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    child = fork();
    if (child == 0) {
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        execv("/system/bin/rmmod", argv);
        _exit(127);
    }
    if (child > 0)
        waitpid(child, &status, 0);
    length = snprintf(line, sizeof(line), "rmmod-status=%d\n", status);
    write(out, line, length);
    fsync(out);
    close(out);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 3;
}
