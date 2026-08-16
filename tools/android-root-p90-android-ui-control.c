#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define STATUS "/data/local/tmp/p90-android-ui-control.txt"

static int service_action(const char *action, const char *service)
{
    pid_t child = fork();
    int status = -1;
    char property[32];
    char *arguments[] = { "setprop", property, (char *)service, NULL };
    snprintf(property, sizeof(property), "ctl.%s", action);
    if (child == 0) {
        execv("/system/bin/setprop", arguments);
        _exit(127);
    }
    if (child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

int main(int argc, char **argv)
{
    int delayed = argc > 1 && !strcmp(argv[1], "--restore-after-delay");
    int restore = delayed || (argc > 1 && !strcmp(argv[1], "restore"));
    int out;
    int zygote, media;
    char line[160];
    pid_t watchdog;

    if (delayed) sleep(90);
    out = open(STATUS, O_WRONLY | O_CREAT |
              (delayed ? O_APPEND : O_TRUNC), 0644);
    if (out < 0 || geteuid() != 0) return 2;
    fchmod(out, 0644);
    zygote = service_action(restore ? "start" : "stop", "zygote");
    media = service_action(restore ? "start" : "stop", "media");
    snprintf(line, sizeof(line),
             "mode=%s\nzygote-result=%d\nmedia-result=%d\n"
             "surfaceflinger-intentionally-unchanged=1\n",
             restore ? "restore" : "linux-only-ui", zygote, media);
    write(out, line, strlen(line));
    if (!restore) {
        watchdog = fork();
        if (watchdog == 0) {
            execl("/data/local/tmp/p90-android-ui-control",
                  "p90-android-ui-control", "--restore-after-delay",
                  (char *)NULL);
            _exit(127);
        }
        snprintf(line, sizeof(line), "restore-watchdog-pid=%d delay=90\n",
                 watchdog);
        write(out, line, strlen(line));
    }
    fsync(out);
    close(out);
    return zygote == 0 && media == 0 ? 0 : 3;
}
