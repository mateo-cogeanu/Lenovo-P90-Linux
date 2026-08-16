#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ROOTFS "/data/local/p90-debian"
#define STATUS "/data/local/tmp/p90-debian-file-state.txt"
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

static void copy_text(int out, const char *path)
{
    char buffer[512];
    ssize_t count;
    int in = open(path, O_RDONLY);
    if (in < 0) {
        dprintf(out, "read %s failed: %s\n", path, strerror(errno));
        return;
    }
    dprintf(out, "text %s: ", path);
    while ((count = read(in, buffer, sizeof(buffer))) > 0)
        write(out, buffer, count);
    close(in);
    dprintf(out, "\n");
}

int main(void)
{
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int watchdog;
    char *argv[] = {FALLBACK_ADBD, NULL};

    if (out < 0)
        return 2;
    fchmod(out, 0644);
    dprintf(out, "uid=%d gid=%d euid=%d egid=%d\n",
            getuid(), getgid(), geteuid(), getegid());
    watchdog = open("/sys/class/misc/watchdog/disable", O_WRONLY);
    if (watchdog >= 0) {
        dprintf(out, "watchdog re-enable write=%ld\n",
                (long)write(watchdog, "0\n", 2));
        close(watchdog);
    }
    report_file(out, ROOTFS);
    report_file(out, ROOTFS "/bin/sh");
    report_file(out, ROOTFS "/lib64/ld-linux-x86-64.so.2");
    report_file(out, ROOTFS "/usr/bin/dpkg");
    report_file(out, ROOTFS "/usr/bin/systemd");
    report_file(out, ROOTFS "/usr/bin/phoc");
    copy_text(out, ROOTFS "/etc/debian_version");
    fsync(out);
    close(out);
    chmod(FALLBACK_ADBD, 0755);
    execv(FALLBACK_ADBD, argv);
    return 127;
}
