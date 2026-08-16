#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define LOG "/data/local/tmp/p90-stale-bootanim-stop.txt"

static int run(const char *program, char *const argv[])
{
    pid_t child = fork();
    int status = -1;
    if (child == 0) {
        execv(program, argv);
        _exit(127);
    }
    if (child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

int main(void)
{
    int out = open(LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    char *stop_bootanim[] = { "setprop", "ctl.stop", "bootanim", 0 };
    char *start_sf[] = { "setprop", "ctl.start", "surfaceflinger", 0 };
    char *wake[] = { "input", "keyevent", "224", 0 };
    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    dprintf(out, "uid=%d stop-bootanim=%d start-sf=%d wake=%d\n", geteuid(),
            run("/system/bin/setprop", stop_bootanim),
            run("/system/bin/setprop", start_sf),
            run("/system/bin/input", wake));
    close(out);
    return 0;
}
