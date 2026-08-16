#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUSYBOX "/system/bin/busybox"
#define ARCHIVE "/data/local/tmp/p90-debian-rootfs-complete.tar.gz"
#define ROOTFS "/data/local/p90-debian"
#define STATUS "/data/local/tmp/p90-debian-android-install.txt"

static int run(int out, const char *label, char *const argv[])
{
    pid_t child = fork();
    int wait_status = 0;

    if (child == 0) {
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        execv(argv[0], argv);
        dprintf(out, "exec %s failed: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    if (child < 0 || waitpid(child, &wait_status, 0) != child ||
        !WIFEXITED(wait_status))
        return -1;
    dprintf(out, "%s exit=%d\n", label, WEXITSTATUS(wait_status));
    fsync(out);
    return WEXITSTATUS(wait_status);
}

static int chroot_probe(int out)
{
    pid_t child = fork();
    int wait_status = 0;
    char *argv[] = {
        "/bin/sh", "-c",
        "echo debian-version=$(cat /etc/debian_version); "
        "echo architecture=$(dpkg --print-architecture); "
        "echo kernel=$(uname -srmo)", NULL
    };

    if (child == 0) {
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        if (chroot(ROOTFS) != 0 || chdir("/") != 0) {
            dprintf(out, "chroot failed: %s\n", strerror(errno));
            _exit(126);
        }
        execv(argv[0], argv);
        dprintf(out, "chroot exec failed: %s\n", strerror(errno));
        _exit(127);
    }
    if (child < 0 || waitpid(child, &wait_status, 0) != child ||
        !WIFEXITED(wait_status))
        return -1;
    dprintf(out, "native chroot probe exit=%d\n", WEXITSTATUS(wait_status));
    return WEXITSTATUS(wait_status);
}

int main(void)
{
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int extract_result = -1;
    int probe_result = -1;
    char *mkdir_argv[] = {BUSYBOX, "mkdir", "-p", ROOTFS, NULL};
    char *tar_argv[] = {BUSYBOX, "tar", "-xzf", ARCHIVE, "-C", ROOTFS, NULL};

    if (out < 0)
        return 2;
    fchmod(out, 0644);
    dprintf(out, "uid=%d gid=%d euid=%d egid=%d started=1\n",
            getuid(), getgid(), geteuid(), getegid());
    fsync(out);
    if (geteuid() != 0 || access(ARCHIVE, R_OK) != 0) {
        dprintf(out, "refusing errno=%d (%s)\n", errno, strerror(errno));
        close(out);
        return 3;
    }

    /* Give the host time to restore the factory dumpstate executable. */
    sleep(15);
    if (run(out, "mkdir", mkdir_argv) == 0) {
        extract_result = run(out, "extract", tar_argv);
        if (extract_result == 0)
            probe_result = chroot_probe(out);
    }
    dprintf(out, "complete=1 extract=%d probe=%d\n",
            extract_result, probe_result);
    fsync(out);
    close(out);
    sync();
    return extract_result || probe_result ? 4 : 0;
}
