#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define STATUS "/data/local/tmp/p90-fbdev-open-probe.txt"

int main(void)
{
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int read_only;
    int read_write;
    int read_only_errno;
    int read_write_errno;
    int unlink_result;
    int unlink_errno;

    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    errno = 0;
    read_only = open("/dev/graphics/fb0", O_RDONLY);
    read_only_errno = errno;
    errno = 0;
    read_write = open("/dev/graphics/fb0", O_RDWR);
    read_write_errno = errno;
    if (read_only >= 0)
        close(read_only);
    if (read_write >= 0)
        close(read_write);
    errno = 0;
    unlink_result = unlink("/dev/fb0");
    unlink_errno = errno;
    dprintf(out, "uid=%d readonly_fd=%d errno=%d (%s)\n", geteuid(),
            read_only, read_only_errno, strerror(read_only_errno));
    dprintf(out, "readwrite_fd=%d errno=%d (%s)\n", read_write,
            read_write_errno, strerror(read_write_errno));
    dprintf(out, "temporary-link-unlink=%d errno=%d (%s)\n", unlink_result,
            unlink_errno, strerror(unlink_errno));
    fsync(out);
    close(out);
    return 0;
}
