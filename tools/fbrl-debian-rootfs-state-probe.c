#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ROOTFS "/data/local/p90-debian"
#define INSTALL_STATUS "/data/local/tmp/p90-debian-install.txt"
#define STATUS "/data/local/tmp/p90-debian-state.txt"
#define WATCHDOG "/sys/class/misc/watchdog/disable"
#define FALLBACK_ADBD "/tmp/p90-original-adbd"

static void report_file(int out, const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0)
        dprintf(out, "file %s size=%lld mode=%o uid=%d gid=%d\n", path,
                (long long)st.st_size, st.st_mode & 07777, st.st_uid, st.st_gid);
    else
        dprintf(out, "missing %s errno=%d (%s)\n", path, errno,
                strerror(errno));
}

static void copy_status(int out)
{
    char buffer[4096];
    ssize_t count;
    int in = open(INSTALL_STATUS, O_RDONLY);

    dprintf(out, "--- installer log ---\n");
    if (in < 0) {
        dprintf(out, "unavailable errno=%d (%s)\n", errno, strerror(errno));
        return;
    }
    while ((count = read(in, buffer, sizeof(buffer))) > 0)
        write(out, buffer, count);
    close(in);
    dprintf(out, "--- end installer log ---\n");
}

static int run_chroot_probe(int out)
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
        if (chroot(ROOTFS) != 0 || chdir("/") != 0)
            _exit(126);
        execv(argv[0], argv);
        _exit(127);
    }
    if (child < 0 || waitpid(child, &wait_status, 0) != child ||
        !WIFEXITED(wait_status))
        return -1;
    return WEXITSTATUS(wait_status);
}

static void chain_adbd(int out)
{
    char *argv[] = {FALLBACK_ADBD, NULL};
    fsync(out);
    close(out);
    chmod(FALLBACK_ADBD, 0755);
    execv(FALLBACK_ADBD, argv);
    _exit(127);
}

int main(void)
{
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int watchdog;
    int probe;

    if (out < 0)
        return 2;
    fchmod(out, 0644);
    dprintf(out, "uid=%d gid=%d euid=%d egid=%d\n",
            getuid(), getgid(), geteuid(), getegid());
    watchdog = open(WATCHDOG, O_WRONLY);
    if (watchdog >= 0) {
        dprintf(out, "watchdog re-enable write=%ld\n",
                (long)write(watchdog, "0\n", 2));
        close(watchdog);
    }
    copy_status(out);
    report_file(out, ROOTFS "/bin/sh");
    report_file(out, ROOTFS "/usr/bin/dpkg");
    report_file(out, ROOTFS "/usr/bin/systemd");
    report_file(out, ROOTFS "/usr/bin/phoc");
    probe = run_chroot_probe(out);
    dprintf(out, "native chroot probe exit=%d\n", probe);
    chain_adbd(out);
    return 5;
}
