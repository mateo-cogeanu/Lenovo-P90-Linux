#include <errno.h>
#include <fcntl.h>
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

static int run_load(int status)
{
    char *argv[] = {
        KEXEC, "--load", KERNEL, "--type=bzImage", "--initrd=" INITRD,
        "--reuse-cmdline", 0
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

int main(void)
{
    int status;
    int result;
    char *shell_argv[] = {
        "/system/bin/sh", "-c",
        "rm -f /data/local/tmp/p90-kexec-rescue-go; "
        "echo 'loaded; root shell waiting for verified restore' > "
            "/data/local/tmp/p90-kexec-rescue-ready; "
        "chmod 644 /data/local/tmp/p90-kexec-rescue-ready; "
        "i=0; while [ ! -f /data/local/tmp/p90-kexec-rescue-go ] && "
            "[ $i -lt 300 ]; do sleep 1; i=$(($i+1)); done; "
        "if [ -f /data/local/tmp/p90-kexec-rescue-go ]; then "
            "echo 1 > /sys/class/misc/watchdog/disable; "
            "echo watchdog_disable_rc=$? >> " STATUS "; "
            "cat /sys/class/misc/watchdog/disable >> " STATUS "; "
            "echo 'go=1 syncing and executing' >> " STATUS "; "
            "chmod 644 " STATUS "; sync; exec " KEXEC " --exec; "
        "else "
            "echo 'timeout=1 unloading' >> " STATUS "; "
            KEXEC " --unload >> " STATUS " 2>&1; "
            "rm -f /data/local/tmp/p90-kexec-rescue-ready; "
        "fi",
        0
    };

    unlink("/data/local/tmp/p90-kexec-rescue-ready");
    unlink("/data/local/tmp/p90-kexec-rescue-go");
    status = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (status < 0)
        return 2;
    fchmod(status, 0644);
    dprintf(status, "uid=%d gid=%d euid=%d egid=%d\n",
            getuid(), getgid(), geteuid(), getegid());
    if (geteuid() != 0) {
        dprintf(status, "refusing: agent is not root\n");
        close(status);
        return 3;
    }

    result = run_load(status);
    dprintf(status, "load exit=%d\n", result);
    fsync(status);
    close(status);
    if (result != 0)
        return 4;

    execv(shell_argv[0], shell_argv);
    return 5;
}
