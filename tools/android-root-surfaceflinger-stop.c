#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define STATUS "/data/local/tmp/p90-surfaceflinger-stop.txt"

int main(void)
{
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int status = -1;
    int result = -1;
    pid_t child;
    char line[64];
    int length;
    char *argv[] = {"setprop", "ctl.stop", "surfaceflinger", NULL};

    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    child = fork();
    if (child == 0) {
        execv("/system/bin/setprop", argv);
        _exit(127);
    }
    if (child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status))
        result = WEXITSTATUS(status);
    length = snprintf(line, sizeof(line), "uid=%d stop-result=%d\n",
                      geteuid(), result);
    write(out, line, length);
    fsync(out);
    close(out);
    return result == 0 ? 0 : 3;
}
