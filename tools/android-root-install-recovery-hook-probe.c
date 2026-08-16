#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define SOURCE "/data/local/tmp/p90-install-recovery-hook-probe.sh"
#define TARGET "/system/etc/install-recovery.sh"
#define STATUS "/data/local/tmp/p90-install-recovery-bind-probe.txt"

static int start_service(void)
{
    pid_t child = fork();
    int status = -1;
    char *argv[] = {"setprop", "ctl.start", "flash_recovery", NULL};

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
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int mounted = 0;
    char line[256];
    int length;

    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    length = snprintf(line, sizeof(line), "uid=%d gid=%d\n", geteuid(),
                      getegid());
    write(out, line, length);

    if (mount(SOURCE, TARGET, NULL, MS_BIND, NULL) == 0) {
        mounted = 1;
        write(out, "bind-result=0\n", sizeof("bind-result=0\n") - 1);
    } else {
        length = snprintf(line, sizeof(line),
                          "bind-result=-1 errno=%d (%s)\n", errno,
                          strerror(errno));
        write(out, line, length);
    }
    fsync(out);

    if (mounted) {
        length = snprintf(line, sizeof(line), "start-result=%d\n",
                          start_service());
        write(out, line, length);
        fsync(out);
        sleep(3);
        {
            int result = umount2(TARGET, MNT_DETACH);
            int saved_errno = errno;
            length = snprintf(line, sizeof(line),
                              "unmount-result=%d errno=%d (%s)\n", result,
                              saved_errno, strerror(saved_errno));
            write(out, line, length);
        }
    }
    write(out, "complete=1\n", sizeof("complete=1\n") - 1);
    fsync(out);
    close(out);
    return mounted ? 0 : 3;
}
