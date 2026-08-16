#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define STATUS "/data/local/tmp/p90-kmem-probe-status.txt"

int main(void)
{
    int status = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    FILE *symbols;
    int kmem;
    char line[512];
    int matches = 0;

    if (status < 0)
        return 2;
    fchmod(status, 0644);
    dprintf(status, "uid=%d gid=%d euid=%d egid=%d\n",
            getuid(), getgid(), geteuid(), getegid());
    if (geteuid() != 0) {
        close(status);
        return 3;
    }

    symbols = fopen("/proc/kallsyms", "r");
    if (!symbols) {
        dprintf(status, "kallsyms open errno=%d (%s)\n", errno,
                strerror(errno));
    } else {
        while (fgets(line, sizeof(line), symbols)) {
            if (strstr(line, " sig_enforce\n")) {
                dprintf(status, "kallsyms %s", line);
                ++matches;
            }
        }
        fclose(symbols);
        dprintf(status, "sig_enforce_matches=%d\n", matches);
    }

    kmem = open("/dev/kmem", O_RDWR);
    if (kmem < 0)
        dprintf(status, "dev_kmem_rw_open errno=%d (%s)\n", errno,
                strerror(errno));
    else {
        dprintf(status, "dev_kmem_rw_open=1\n");
        close(kmem);
    }

    dprintf(status, "complete=1\n");
    fsync(status);
    close(status);
    return 0;
}
