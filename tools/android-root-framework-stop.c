#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define STATUS "/data/local/tmp/p90-android-framework-stop.txt"

static int stop_service(const char *service)
{
    pid_t child = fork();
    int status = -1;
    char *argv[] = {"setprop", "ctl.stop", (char *)service, NULL};
    if (child == 0) {
        execv("/system/bin/setprop", argv);
        _exit(127);
    }
    if (child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

int main(void)
{
    char line[96];
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int zygote, surfaceflinger, length;
    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    zygote = stop_service("zygote");
    surfaceflinger = stop_service("surfaceflinger");
    length = snprintf(line, sizeof(line),
                      "zygote-stop=%d\nsurfaceflinger-stop=%d\n",
                      zygote, surfaceflinger);
    write(out, line, length);
    fsync(out);
    close(out);
    return zygote == 0 && surfaceflinger == 0 ? 0 : 3;
}
