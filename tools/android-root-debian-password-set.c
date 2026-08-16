#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ROOTFS "/data/local/p90-debian"
#define STATUS "/data/local/tmp/p90-debian-password-set.txt"

int main(void)
{
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int input[2];
    int status = -1;
    pid_t child;
    const char *password = getenv("P90_ROOT_PASSWORD");
    char password_line[128];
    int password_length;
    char *argv[] = {"chpasswd", NULL};

    if (out < 0 || geteuid() != 0)
        return 2;
    if (password == NULL || password[0] == '\0' ||
        strchr(password, '\n') != NULL || strchr(password, ':') != NULL)
        return 5;
    password_length = snprintf(password_line, sizeof(password_line),
                               "root:%s\n", password);
    if (password_length <= 6 || password_length >= (int)sizeof(password_line))
        return 5;
    fchmod(out, 0644);
    if (pipe(input) != 0)
        return 3;
    child = fork();
    if (child == 0) {
        close(input[1]);
        dup2(input[0], STDIN_FILENO);
        close(input[0]);
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        if (chroot(ROOTFS) != 0 || chdir("/") != 0)
            _exit(126);
        execv("/usr/sbin/chpasswd", argv);
        _exit(127);
    }
    close(input[0]);
    write(input[1], password_line, password_length);
    close(input[1]);
    if (child > 0)
        waitpid(child, &status, 0);
    {
        char line[64];
        int length = snprintf(line, sizeof(line), "chpasswd-status=%d\n",
                              status);
        write(out, line, length);
    }
    fsync(out);
    close(out);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 4;
}
