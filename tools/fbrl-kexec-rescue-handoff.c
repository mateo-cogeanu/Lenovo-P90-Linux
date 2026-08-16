#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define KEXEC  "/tmp/p90-kexec"
#define KERNEL "/tmp/p90-rescue-bzImage"
#define INITRD  "/tmp/p90-rescue-ramdisk.img"
#define STATUS  "/tmp/p90-kexec-handoff.txt"
#define FALLBACK_ADBD "/tmp/p90-original-adbd"

#define KEXEC_PHYSICAL_ARCH_FILE_OFFSET 0xa1c0

static int force_kexec_physical_arch_x86_64(int status)
{
    static const unsigned char expected[] = {0x55, 0x57, 0x56, 0x53};
    static const unsigned char replacement[] = {
        0xb8, 0x00, 0x00, 0x3e, 0x00, 0xc3
    };
    unsigned char actual[sizeof(expected)];
    int fd = open(KEXEC, O_RDWR);

    if (fd < 0) {
        dprintf(status, "patch open errno=%d (%s)\n", errno, strerror(errno));
        return -1;
    }
    if (pread(fd, actual, sizeof(actual), KEXEC_PHYSICAL_ARCH_FILE_OFFSET) !=
            (ssize_t)sizeof(actual) ||
        memcmp(actual, expected, sizeof(expected)) != 0) {
        dprintf(status, "patch refused: physical_arch prefix mismatch\n");
        close(fd);
        return -1;
    }
    if (pwrite(fd, replacement, sizeof(replacement),
               KEXEC_PHYSICAL_ARCH_FILE_OFFSET) !=
            (ssize_t)sizeof(replacement)) {
        dprintf(status, "patch write errno=%d (%s)\n", errno, strerror(errno));
        close(fd);
        return -1;
    }
    fsync(fd);
    close(fd);
    dprintf(status, "patched physical_arch=KEXEC_ARCH_X86_64\n");
    return 0;
}

static int run_load(int status)
{
    char *argv[] = {
        KEXEC, "--load", KERNEL, "--type=bzImage",
        "--initrd=" INITRD, "--reuse-cmdline", NULL
    };
    pid_t child = fork();
    int wait_status = 0;

    if (child == 0) {
        dup2(status, STDOUT_FILENO);
        dup2(status, STDERR_FILENO);
        execv(KEXEC, argv);
        dprintf(status, "load exec errno=%d (%s)\n", errno, strerror(errno));
        _exit(127);
    }
    if (child < 0 || waitpid(child, &wait_status, 0) != child)
        return -1;
    if (!WIFEXITED(wait_status))
        return -1;
    return WEXITSTATUS(wait_status);
}

static void fallback_to_adbd(int status)
{
    char *argv[] = {FALLBACK_ADBD, NULL};

    fsync(status);
    close(status);
    chmod(FALLBACK_ADBD, 0755);
    execv(FALLBACK_ADBD, argv);
    _exit(127);
}

int main(void)
{
    int status = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int watchdog;
    int load_result;
    char *exec_argv[] = {KEXEC, "--exec", NULL};

    if (status < 0)
        return 2;
    fchmod(status, 0644);
    dprintf(status, "uid=%d gid=%d euid=%d egid=%d\n",
            getuid(), getgid(), geteuid(), getegid());
    if (geteuid() != 0) {
        dprintf(status, "refusing: agent is not root\n");
        fallback_to_adbd(status);
    }
    if (chmod(KEXEC, 0755) != 0 ||
        force_kexec_physical_arch_x86_64(status) != 0) {
        dprintf(status, "loader preparation failed errno=%d (%s)\n",
                errno, strerror(errno));
        fallback_to_adbd(status);
    }

    load_result = run_load(status);
    dprintf(status, "load exit=%d\n", load_result);
    if (load_result != 0)
        fallback_to_adbd(status);

    watchdog = open("/sys/class/misc/watchdog/disable", O_WRONLY);
    if (watchdog >= 0) {
        ssize_t written = write(watchdog, "1\n", 2);
        dprintf(status, "watchdog disable write=%ld errno=%d (%s)\n",
                (long)written, errno, strerror(errno));
        close(watchdog);
    } else {
        dprintf(status, "watchdog disable open errno=%d (%s)\n",
                errno, strerror(errno));
    }
    dprintf(status, "executing kexec\n");
    fsync(status);
    close(status);
    sync();
    execv(KEXEC, exec_argv);

    status = open(STATUS, O_WRONLY | O_APPEND);
    if (status >= 0) {
        dprintf(status, "kexec --exec returned errno=%d (%s)\n",
                errno, strerror(errno));
        fallback_to_adbd(status);
    }
    return 5;
}
