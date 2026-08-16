#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define STATUS "/data/local/tmp/p90-debian-display-restart.txt"
#define ROOTFS "/data/local/p90-debian"

static int control(const char *action, const char *service)
{
    pid_t child = fork();
    int status = -1;
    char property[32];
    char *argv[] = {"setprop", property, (char *)service, NULL};

    snprintf(property, sizeof(property), "ctl.%s", action);
    if (child == 0) {
        execv("/system/bin/setprop", argv);
        _exit(127);
    }
    if (child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

static void record(int out, const char *name, int value)
{
    char line[96];
    int length = snprintf(line, sizeof(line), "%s=%d\n", name, value);
    write(out, line, length);
    fsync(out);
}

static int display_process(const char *command)
{
    return strstr(command, "/usr/lib/xorg/Xorg") != NULL ||
           strstr(command, "/usr/bin/phoc") != NULL ||
           strstr(command, "/usr/libexec/phosh") != NULL ||
           strstr(command, "/usr/bin/dbus-run-session") != NULL ||
           strstr(command, "dbus-daemon") != NULL;
}

static int kill_display_processes(int signal_number)
{
    DIR *proc = opendir("/proc");
    struct dirent *entry;
    int killed = 0;
    char path[64];
    char command[512];

    if (proc == NULL)
        return -1;
    while ((entry = readdir(proc)) != NULL) {
        int fd;
        ssize_t count;
        pid_t pid;
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
            continue;
        pid = (pid_t)atoi(entry->d_name);
        if (pid <= 1 || pid == getpid())
            continue;
        snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        fd = open(path, O_RDONLY);
        if (fd < 0)
            continue;
        count = read(fd, command, sizeof(command) - 1);
        close(fd);
        if (count <= 0)
            continue;
        command[count] = '\0';
        if (display_process(command) && kill(pid, signal_number) == 0)
            ++killed;
    }
    closedir(proc);
    return killed;
}

int main(void)
{
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    record(out, "stop-debian", control("stop", "flash_recovery"));
    sleep(2);
    record(out, "term-display-processes", kill_display_processes(SIGTERM));
    sleep(3);
    record(out, "kill-display-processes", kill_display_processes(SIGKILL));
    unlink(ROOTFS "/tmp/.X1-lock");
    unlink(ROOTFS "/tmp/.X11-unix/X1");
    record(out, "stop-surfaceflinger", control("stop", "surfaceflinger"));
    sleep(2);
    record(out, "start-debian", control("start", "flash_recovery"));
    record(out, "complete", 1);
    close(out);
    return 0;
}
