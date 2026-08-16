#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ROOTFS "/data/local/p90-debian"
#define STATUS "/data/local/tmp/p90-debian-loopback-shell.txt"

int main(void)
{
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int server;
    int client;
    int one = 1;
    struct sockaddr_in address;
    char *argv[] = {"/bin/sh", "-i", NULL};

    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    dprintf(out, "uid=%d gid=%d euid=%d egid=%d\n",
            getuid(), getgid(), geteuid(), getegid());

    server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0)
        return 3;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(22333);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(server, 1) != 0) {
        dprintf(out, "listen failed errno=%d (%s)\n", errno, strerror(errno));
        close(out);
        return 4;
    }
    dprintf(out, "listening=1 address=127.0.0.1 port=22333\n");
    fsync(out);
    client = accept(server, NULL, NULL);
    close(server);
    if (client < 0)
        return 5;
    dprintf(out, "accepted=1\n");
    fsync(out);

    if (chroot(ROOTFS) != 0 || chdir("/") != 0) {
        dprintf(out, "chroot failed errno=%d (%s)\n", errno, strerror(errno));
        close(out);
        return 6;
    }
    dprintf(out, "chroot=1\n");
    fsync(out);
    close(out);
    dup2(client, STDIN_FILENO);
    dup2(client, STDOUT_FILENO);
    dup2(client, STDERR_FILENO);
    if (client > STDERR_FILENO)
        close(client);
    setenv("HOME", "/root", 1);
    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
    setenv("TERM", "xterm-256color", 1);
    execv(argv[0], argv);
    dprintf(STDERR_FILENO, "exec failed errno=%d (%s)\n", errno,
            strerror(errno));
    return 127;
}
