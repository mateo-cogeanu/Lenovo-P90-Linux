#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ROOTFS "/data/local/p90-debian"
#define STATUS "/data/local/tmp/p90-debian-sshd.txt"

static const char authorized_key[] =
    "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIEZDxUsVx1VwF0c/mjvgL9Bx6YFeEy1FK1N0Sd7924j9 p90-native-debian\n";

static int bind_mount(int out, const char *source, const char *target)
{
    int result = mount(source, target, NULL, MS_BIND | MS_REC, NULL);
    dprintf(out, "mount %s -> %s result=%d errno=%d (%s)\n", source,
            target, result, errno, strerror(errno));
    return result;
}

int main(void)
{
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int key;
    char *argv[] = {
        "/usr/sbin/sshd", "-D", "-e", "-ddd", "-p", "2222",
        "-o", "ListenAddress=127.0.0.1",
        "-o", "PasswordAuthentication=no",
        "-o", "KbdInteractiveAuthentication=no",
        "-o", "PubkeyAuthentication=yes",
        "-o", "PermitRootLogin=yes",
        "-o", "UsePAM=no",
        "-o", "PidFile=/run/sshd-p90.pid", NULL
    };
    char pid_text[32];
    int old_pid_file;
    ssize_t pid_length;

    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    dprintf(out, "uid=%d gid=%d euid=%d egid=%d\n",
            getuid(), getgid(), geteuid(), getegid());

    mkdir(ROOTFS "/run", 0755);
    mkdir(ROOTFS "/run/sshd", 0755);
    mkdir(ROOTFS "/root/.ssh", 0700);
    chmod(ROOTFS "/root/.ssh", 0700);
    key = open(ROOTFS "/root/.ssh/authorized_keys",
               O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (key < 0 || write(key, authorized_key, sizeof(authorized_key) - 1) !=
            (ssize_t)(sizeof(authorized_key) - 1)) {
        dprintf(out, "authorized_keys failed errno=%d (%s)\n", errno,
                strerror(errno));
        if (key >= 0)
            close(key);
        close(out);
        return 3;
    }
    fsync(key);
    close(key);

    bind_mount(out, "/dev", ROOTFS "/dev");
    bind_mount(out, "/proc", ROOTFS "/proc");
    bind_mount(out, "/sys", ROOTFS "/sys");

    old_pid_file = open(ROOTFS "/run/sshd-p90.pid", O_RDONLY);
    if (old_pid_file >= 0) {
        pid_length = read(old_pid_file, pid_text, sizeof(pid_text) - 1);
        close(old_pid_file);
        if (pid_length > 0) {
            pid_text[pid_length] = '\0';
            dprintf(out, "stopping old sshd pid=%d result=%d\n",
                    atoi(pid_text), kill(atoi(pid_text), SIGTERM));
            sleep(1);
        }
    }
    dprintf(out, "launching=1 port=2222 key_fingerprint=SHA256:4C/rKtVG8DI8SnkhpHjo/akGZPVFrWaslCxfoyodzl0\n");
    fsync(out);
    dup2(out, STDOUT_FILENO);
    dup2(out, STDERR_FILENO);
    if (out != STDOUT_FILENO && out != STDERR_FILENO)
        close(out);

    if (chroot(ROOTFS) != 0 || chdir("/") != 0)
        return 4;
    execv(argv[0], argv);
    return 127;
}
