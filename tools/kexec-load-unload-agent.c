#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define KEXEC "/data/local/tmp/p90-kexec"
#define KERNEL "/data/local/tmp/p90-kexec-test-bzImage"
#define STATUS "/data/local/tmp/p90-kexec-proof.txt"

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
    int load_result;
    int unload_result;
    char *load_argv[] = {
        KEXEC,
        "--load",
        KERNEL,
        "--type=bzImage",
        "--command-line=console=ttyS0,115200n8 earlyprintk=serial p90.kexec_proof=1",
        0
    };
    char *unload_argv[] = {KEXEC, "--unload", 0};

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
    unload_result = run_and_record(status, "unload", unload_argv);
    dprintf(status, "complete=1 load_result=%d unload_result=%d\n",
            load_result, unload_result);
    fsync(status);
    fchmod(status, 0644);
    close(status);
    return load_result == 0 && unload_result == 0 ? 0 : 4;
}
