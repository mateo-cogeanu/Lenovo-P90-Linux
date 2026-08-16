#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define STATUS "/data/local/tmp/p90-autoboot-pause.txt"
#define EXPECTED "/system/bin/p90-debian-autoboot-v7"

int main(void)
{
    char path[64], cmdline[128] = {0};
    int pid, fd, out, result = -1, saved_errno = 0;

    out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    for (pid = 2; pid < 32768; ++pid) {
        snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        fd = open(path, O_RDONLY);
        if (fd < 0)
            continue;
        memset(cmdline, 0, sizeof(cmdline));
        read(fd, cmdline, sizeof(cmdline) - 1);
        close(fd);
        if (strcmp(cmdline, EXPECTED) == 0) {
            result = kill(pid, SIGSTOP);
            saved_errno = errno;
            dprintf(out, "validated-pid=%d\nsigstop=%d\nerrno=%d\n",
                    pid, result, saved_errno);
            close(out);
            return result == 0 ? 0 : 3;
        }
    }
    dprintf(out, "validated-pid=0\nsigstop=-1\nerrno=%d\n", ESRCH);
    close(out);
    return 4;
}
