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
#define STATUS  "/tmp/p90-kexec-preserve-probe.txt"
#define FALLBACK_ADBD "/tmp/p90-original-adbd"
#define KEXEC_PHYSICAL_ARCH_FILE_OFFSET 0xa1c0

static int patch_arch(int out)
{
    static const unsigned char expected[] = {0x55, 0x57, 0x56, 0x53};
    static const unsigned char replacement[] = {
        0xb8, 0x00, 0x00, 0x3e, 0x00, 0xc3
    };
    unsigned char actual[sizeof(expected)];
    int fd = open(KEXEC, O_RDWR);

    if (fd < 0 ||
        pread(fd, actual, sizeof(actual), KEXEC_PHYSICAL_ARCH_FILE_OFFSET) !=
            (ssize_t)sizeof(actual) ||
        memcmp(actual, expected, sizeof(expected)) != 0 ||
        pwrite(fd, replacement, sizeof(replacement),
               KEXEC_PHYSICAL_ARCH_FILE_OFFSET) !=
            (ssize_t)sizeof(replacement)) {
        dprintf(out, "patch refused errno=%d (%s)\n", errno, strerror(errno));
        if (fd >= 0)
            close(fd);
        return -1;
    }
    fsync(fd);
    close(fd);
    dprintf(out, "patched physical_arch=KEXEC_ARCH_X86_64\n");
    return 0;
}

static int run(int out, const char *label, char *const argv[])
{
    pid_t child = fork();
    int wait_status = 0;

    if (child == 0) {
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        execv(argv[0], argv);
        _exit(127);
    }
    if (child < 0 || waitpid(child, &wait_status, 0) != child ||
        !WIFEXITED(wait_status))
        return -1;
    dprintf(out, "%s exit=%d\n", label, WEXITSTATUS(wait_status));
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
    int load_result = -1;
    int unload_result = -1;
    char *load[] = {
        KEXEC, "--kexec-syscall", "--load-preserve-context", KERNEL,
        "--type=bzImage",
        "--initrd=" INITRD, "--reuse-cmdline",
        "--mem-max=0x3fffffff", NULL
    };
    char *unload[] = {KEXEC, "--unload", NULL};

    if (out < 0)
        return 2;
    fchmod(out, 0644);
    dprintf(out, "uid=%d gid=%d euid=%d egid=%d\n",
            getuid(), getgid(), geteuid(), getegid());
    if (geteuid() == 0 && chmod(KEXEC, 0755) == 0 && patch_arch(out) == 0) {
        load_result = run(out, "preserve-context load", load);
        if (load_result == 0)
            unload_result = run(out, "unload", unload);
    }
    dprintf(out, "complete=1 load=%d unload=%d\n",
            load_result, unload_result);
    chain_adbd(out);
    return 5;
}
