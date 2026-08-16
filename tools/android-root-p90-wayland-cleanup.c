#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define LOG "/data/local/tmp/p90-wayland-cleanup.txt"

static int matches(const char *text)
{
    return strstr(text, "p90-wayland-sf-compositor") ||
           strstr(text, "/usr/bin/phoc") ||
           strstr(text, "/usr/libexec/phosh") ||
           strstr(text, "/usr/bin/squeekboard") ||
           strstr(text, "/usr/bin/dbus-run-session");
}

static int signal_matching(int signal, int out)
{
    DIR *directory = opendir("/proc");
    struct dirent *entry;
    int count = 0;
    if (!directory) return -1;
    while ((entry = readdir(directory))) {
        char path[64], command[512], line[640];
        int fd, length, i;
        pid_t pid = (pid_t)atoi(entry->d_name);
        if (pid <= 1 || pid == getpid()) continue;
        snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        length = read(fd, command, sizeof(command) - 1);
        close(fd);
        if (length <= 0) continue;
        command[length] = '\0';
        for (i = 0; i < length - 1; ++i)
            if (command[i] == '\0') command[i] = ' ';
        if (!matches(command)) continue;
        length = snprintf(line, sizeof(line), "signal=%d pid=%d cmd=%s\n",
                          signal, pid, command);
        write(out, line, length);
        kill(pid, signal);
        ++count;
    }
    closedir(directory);
    return count;
}

int main(void)
{
    int out = open(LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    char line[80];
    int first, second;
    if (out < 0 || geteuid() != 0) return 2;
    fchmod(out, 0644);
    first = signal_matching(SIGTERM, out);
    sleep(2);
    second = signal_matching(SIGKILL, out);
    unlink("/data/local/tmp/p90-wayland-run/wayland-0");
    unlink("/data/local/tmp/p90-wayland-run/wayland-0.lock");
    unlink("/data/local/p90-debian/run/user/0/wayland-0");
    unlink("/data/local/p90-debian/run/user/0/wayland-0.lock");
    snprintf(line, sizeof(line), "term-count=%d kill-count=%d\n", first, second);
    write(out, line, strlen(line));
    fsync(out);
    close(out);
    return 0;
}
