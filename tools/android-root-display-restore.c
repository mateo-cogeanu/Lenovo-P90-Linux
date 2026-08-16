#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define STATUS "/data/local/tmp/p90-display-restore.txt"

static int run(const char *program, char *const argv[])
{
    pid_t child = fork();
    int status = -1;

    if (child == 0) {
        execv(program, argv);
        _exit(127);
    }
    if (child > 0 && waitpid(child, &status, 0) == child &&
        WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

int main(void)
{
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    char *start[] = {"setprop", "ctl.start", "surfaceflinger", NULL};
    char *wake[] = {"input", "keyevent", "224", NULL};

    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    dprintf(out, "uid=%d start-result=%d\n", geteuid(),
            run("/system/bin/setprop", start));
    fsync(out);
    sleep(5);
    dprintf(out, "wake-result=%d\n", run("/system/bin/input", wake));
    dprintf(out, "complete=1\n");
    fsync(out);
    close(out);
    return 0;
}
