#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ROOTFS "/data/local/p90-debian"
#define SOURCE "/data/local/tmp/p90-repair-debs"
#define TARGET ROOTFS "/tmp/p90-repair-debs"
#define STATUS "/data/local/tmp/p90-debian-package-repair.txt"

int main(void)
{
    int out, status = -1, length;
    char line[64];
    pid_t child;
    char *argv[] = {
        "/bin/sh", "-c",
        "export DEBIAN_FRONTEND=noninteractive "
        "PATH=/usr/sbin:/usr/bin:/sbin:/bin; "
        "/usr/bin/dpkg -i /tmp/p90-repair-debs/*.deb && "
        "/usr/bin/dpkg --configure -a; result=$?; "
        "/usr/bin/update-desktop-database /usr/share/applications "
        "2>/dev/null || true; echo repair-result=$result; exit $result",
        NULL
    };

    out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    mount("/dev", ROOTFS "/dev", NULL, MS_BIND | MS_REC, NULL);
    mount("/proc", ROOTFS "/proc", NULL, MS_BIND | MS_REC, NULL);
    mount("/sys", ROOTFS "/sys", NULL, MS_BIND | MS_REC, NULL);
    mkdir(TARGET, 0755);
    if (mount(SOURCE, TARGET, NULL, MS_BIND | MS_REC, NULL) != 0)
        return 3;
    child = fork();
    if (child == 0) {
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        if (chroot(ROOTFS) != 0 || chdir("/") != 0)
            _exit(126);
        execv(argv[0], argv);
        _exit(127);
    }
    if (child > 0)
        waitpid(child, &status, 0);
    length = snprintf(line, sizeof(line), "repair-wait-status=%d\n", status);
    write(out, line, length);
    fsync(out);
    umount2(TARGET, MNT_DETACH);
    umount2(ROOTFS "/sys", MNT_DETACH);
    umount2(ROOTFS "/proc", MNT_DETACH);
    umount2(ROOTFS "/dev", MNT_DETACH);
    close(out);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 4;
}
