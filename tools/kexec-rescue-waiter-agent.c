#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define KEXEC  "/data/local/tmp/p90-kexec"
#define KERNEL "/data/local/tmp/p90-rescue-bzImage"
#define INITRD  "/data/local/tmp/p90-rescue-ramdisk.img"
#define STATUS  "/data/local/tmp/p90-kexec-rescue-status.txt"
#define READY   "/data/local/tmp/p90-kexec-rescue-ready"
#define GO      "/data/local/tmp/p90-kexec-rescue-go"

/* Lenovo's supplied boot_cmdline with this handset's SPID and serial number. */
#define CMDLINE \
    "init=/init pci=noearly console=logk0 console=ttyS0 " \
    "no_console_suspend=1 earlyprintk=nologger panic_on_bad_page=1 " \
    "panic_on_list_corruption=1 loglevel=8 vmalloc=256M " \
    "androidboot.hardware=mofd_v1 " \
    "androidboot.spid=0000:0000:0000:0008:0000:0000 " \
    "androidboot.serialno=MedfieldE0F225F9 " \
    "snd_pcm.maximum_substreams=8 ptrace.ptrace_can_access=1 " \
    "allow_factory=1 ip=50.0.0.2:50.0.0.1::255.255.255.0::usb0:on " \
    "debug_locks=0 p90.kexec_rescue=1"

static int run_and_record(int status, const char *label, char *const argv[])
{
    pid_t child = fork();
    int wait_status = 0;

    if (child == 0) {
        dup2(status, STDOUT_FILENO);
        dup2(status, STDERR_FILENO);
        execv(KEXEC, argv);
        dprintf(status, "%s exec errno=%d (%s)\n", label, errno,
                strerror(errno));
        _exit(127);
    }
    if (child < 0) {
        dprintf(status, "%s fork errno=%d (%s)\n", label, errno,
                strerror(errno));
        return -1;
    }
    if (waitpid(child, &wait_status, 0) != child) {
        dprintf(status, "%s wait errno=%d (%s)\n", label, errno,
                strerror(errno));
        return -1;
    }
    if (WIFEXITED(wait_status)) {
        dprintf(status, "%s exit=%d\n", label, WEXITSTATUS(wait_status));
        return WEXITSTATUS(wait_status);
    }
    dprintf(status, "%s abnormal_status=%d\n", label, wait_status);
    return -1;
}

static void publish_marker(const char *path, const char *contents)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, contents, strlen(contents));
        fsync(fd);
        fchmod(fd, 0644);
        close(fd);
    }
}

int main(void)
{
    int status;
    int load_result;
    int i;
    char *load_argv[] = {
        KEXEC, "--load", KERNEL, "--type=bzImage",
        "--initrd=" INITRD,
        "--command-line=" CMDLINE,
        0
    };
    char *exec_argv[] = {KEXEC, "--exec", 0};
    char *unload_argv[] = {KEXEC, "--unload", 0};

    unlink(READY);
    unlink(GO);
    status = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (status < 0)
        return 2;
    dprintf(status, "uid=%d gid=%d euid=%d egid=%d\n",
            getuid(), getgid(), geteuid(), getegid());
    if (geteuid() != 0) {
        dprintf(status, "refusing: agent is not root\n");
        close(status);
        return 3;
    }

    load_result = run_and_record(status, "load", load_argv);
    dprintf(status, "load_result=%d\n", load_result);
    fsync(status);
    if (load_result != 0) {
        fchmod(status, 0644);
        close(status);
        return 4;
    }

    publish_marker(READY, "loaded; waiting for verified-restore trigger\n");
    dprintf(status, "ready=1 timeout_seconds=300\n");
    fsync(status);

    /* Five minutes gives the host time to restore and hash-check dumpstate. */
    for (i = 0; i < 1200; ++i) {
        if (access(GO, F_OK) == 0) {
            unlink(GO);
            dprintf(status, "go=1 syncing and executing kexec\n");
            fsync(status);
            sync();
            run_and_record(status, "exec", exec_argv);
            dprintf(status, "ERROR: kexec execution returned\n");
            fsync(status);
            run_and_record(status, "unload_after_exec_failure", unload_argv);
            fchmod(status, 0644);
            close(status);
            return 5;
        }
        usleep(250000);
    }

    dprintf(status, "timeout=1 unloading\n");
    run_and_record(status, "unload_after_timeout", unload_argv);
    unlink(READY);
    fsync(status);
    fchmod(status, 0644);
    close(status);
    return 6;
}
