#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define STATUS "/data/local/tmp/p90-module-probe-status.txt"
#define MODULE "/data/local/tmp/p90-kexec-handoff.ko"

static int run(int fd, const char *label, char *const argv[])
{
    pid_t child = fork();
    int status = 0;
    if (child == 0) {
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        execv(argv[0], argv);
        dprintf(fd, "%s exec failed: %s\n", label, strerror(errno));
        _exit(127);
    }
    if (child < 0 || waitpid(child, &status, 0) != child)
        return -1;
    if (!WIFEXITED(status))
        return -1;
    dprintf(fd, "%s exit=%d\n", label, WEXITSTATUS(status));
    return WEXITSTATUS(status);
}

int main(void)
{
    int fd = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int load_result;
    int unload_result = -1;
    char *insmod[] = {"/system/bin/insmod", MODULE, 0};
    char *rmmod[] = {"/system/bin/rmmod", "p90_kexec_handoff_module", 0};

    if (fd < 0)
        return 2;
    fchmod(fd, 0644);
    dprintf(fd, "uid=%d gid=%d euid=%d egid=%d\n",
            getuid(), getgid(), geteuid(), getegid());
    if (geteuid() != 0) {
        close(fd);
        return 3;
    }
    load_result = run(fd, "insmod", insmod);
    if (load_result == 0) {
        dprintf(fd, "go_parameter=%s\n",
                access("/sys/module/p90_kexec_handoff_module/parameters/go",
                       F_OK) == 0 ? "present" : "missing");
        unload_result = run(fd, "rmmod", rmmod);
    }
    dprintf(fd, "complete=1 load=%d unload=%d\n",
            load_result, unload_result);
    fsync(fd);
    close(fd);
    return load_result == 0 && unload_result == 0 ? 0 : 4;
}
