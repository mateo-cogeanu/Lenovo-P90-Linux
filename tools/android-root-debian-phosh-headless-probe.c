#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ROOTFS "/data/local/p90-debian"
#define STATUS "/data/local/tmp/p90-debian-phosh-headless.txt"

int main(void)
{
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    pid_t child;
    int wait_status = 0;
    char *argv[] = {
        "/bin/sh", "-c",
        "export HOME=/root PATH=/usr/sbin:/usr/bin:/sbin:/bin; "
        "mkdir -p /run/user/0; chmod 700 /run/user/0; "
        "export XDG_RUNTIME_DIR=/run/user/0; "
        "export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1; "
        "export WLR_RENDERER=pixman WLR_RENDERER_ALLOW_SOFTWARE=1; "
        "export WLR_LIBINPUT_NO_DEVICES=1; "
        "/usr/bin/timeout 10s /usr/bin/dbus-run-session -- "
        "/usr/bin/phoc -C /usr/share/phosh/phoc.ini -E /usr/libexec/phosh",
        NULL
    };

    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    dprintf(out, "launcher uid=%d gid=%d euid=%d egid=%d\n",
            getuid(), getgid(), geteuid(), getegid());
    child = fork();
    if (child == 0) {
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        if (chroot(ROOTFS) != 0 || chdir("/") != 0) {
            dprintf(out, "chroot failed errno=%d (%s)\n", errno,
                    strerror(errno));
            _exit(126);
        }
        execv(argv[0], argv);
        dprintf(out, "exec failed errno=%d (%s)\n", errno, strerror(errno));
        _exit(127);
    }
    if (child < 0 || waitpid(child, &wait_status, 0) != child ||
        !WIFEXITED(wait_status))
        dprintf(out, "complete=1 probe=-1\n");
    else
        dprintf(out, "complete=1 probe=%d\n", WEXITSTATUS(wait_status));
    fsync(out);
    close(out);
    return 0;
}
