#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ROOTFS "/data/local/p90-debian"
#define STAGED "/data/local/tmp/p90-chroot-command"
#define COMMAND_FILE "/data/local/tmp/p90-chroot-command.txt"
#define LOG_FILE "/data/local/tmp/p90-chroot-command.log"
#define DONE_FILE "/data/local/tmp/p90-chroot-command.done"
#define PACKAGE_SOURCE "/data/local/tmp/p90-debs-full-v1"
#define PACKAGE_TARGET ROOTFS "/tmp/p90-debs-full-v1"

static int bind_if_missing(const char *source, const char *target,
                           const char *sentinel)
{
    if (access(sentinel, F_OK) == 0) return 0;
    return mount(source, target, NULL, MS_BIND | MS_REC, NULL);
}

static int write_text(const char *path, const char *text, mode_t mode)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    size_t length = strlen(text);
    int result = -1;
    if (fd >= 0 && write(fd, text, length) == (ssize_t)length) {
        fchmod(fd, mode);
        fsync(fd);
        result = 0;
    }
    if (fd >= 0) close(fd);
    return result;
}

int main(int argc, char **argv)
{
    char command[8192];
    char done[128];
    char *shell_argv[] = { "/bin/sh", "-c", command, NULL };
    char *host_argv[] = { "/system/bin/sh", "-c", command + 5, NULL };
    int input, output, status = -1;
    ssize_t count;
    pid_t child;

    if (argc < 2 || strcmp(argv[1], "--staged") != 0) {
        execl(STAGED, "p90-chroot-command", "--staged", (char *)NULL);
        return 125;
    }
    if (geteuid() != 0) return 2;
    unlink(DONE_FILE);
    output = open(LOG_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output < 0) return 3;
    fchmod(output, 0644);

    input = open(COMMAND_FILE, O_RDONLY);
    count = input >= 0 ? read(input, command, sizeof(command) - 1) : -1;
    if (input >= 0) close(input);
    if (count <= 0) {
        dprintf(output, "command-read-failed=%d\n", errno);
        close(output);
        return 4;
    }
    command[count] = '\0';
    dprintf(output, "uid=%d\ncommand=%s\n", geteuid(), command);
    fsync(output);

    if (strncmp(command, "HOST:", 5) != 0) {
        mkdir(ROOTFS "/run/systemd", 0755);
        mkdir(ROOTFS "/run/systemd/resolve", 0755);
        chmod(ROOTFS "/run/systemd", 0755);
        chmod(ROOTFS "/run/systemd/resolve", 0755);
        mkdir(PACKAGE_TARGET, 0755);
        write_text(ROOTFS "/run/systemd/resolve/stub-resolv.conf",
                   "nameserver 1.1.1.1\nnameserver 8.8.8.8\n", 0644);
        if (bind_if_missing("/dev", ROOTFS "/dev", ROOTFS "/dev/graphics/fb0") ||
            bind_if_missing("/proc", ROOTFS "/proc", ROOTFS "/proc/version") ||
            bind_if_missing("/sys", ROOTFS "/sys",
                            ROOTFS "/sys/class/graphics/fb0/name")) {
            dprintf(output, "bind-failed=%d\n", errno);
            close(output);
            return 5;
        }
        if (access(PACKAGE_SOURCE, F_OK) == 0 &&
            mount(PACKAGE_SOURCE, PACKAGE_TARGET, NULL, MS_BIND, NULL) != 0 &&
            errno != EBUSY) {
            dprintf(output, "package-bind-failed=%d\n", errno);
            close(output);
            return 5;
        }
    }

    child = fork();
    if (child == 0) {
        dup2(output, STDOUT_FILENO);
        dup2(output, STDERR_FILENO);
        if (strncmp(command, "HOST:", 5) == 0) {
            execv(host_argv[0], host_argv);
        } else {
            if (chroot(ROOTFS) != 0 || chdir("/") != 0) _exit(126);
            setenv("HOME", "/root", 1);
            setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
            setenv("DEBIAN_FRONTEND", "noninteractive", 1);
            execv(shell_argv[0], shell_argv);
        }
        _exit(127);
    }
    if (child > 0) waitpid(child, &status, 0);
    dprintf(output, "wait-status=%d\n", status);
    fsync(output);
    close(output);
    snprintf(done, sizeof(done), "wait-status=%d\n", status);
    write_text(DONE_FILE, done, 0644);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 6;
}
