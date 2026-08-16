#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define RESOURCE "/sys/bus/pci/devices/0000:00:02.0/resource0"
#define STATUS "/data/local/tmp/p90-psb-fbdev-plane-switch.txt"
#define MAP_OFFSET 0x70000
#define MAP_SIZE 0x1000
#define DSPASURF 0x7019c

int main(void)
{
    char line[160];
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int fd;
    int length;
    volatile unsigned int *base;
    volatile unsigned int *surface;
    unsigned int before, after;

    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    fd = open(RESOURCE, O_RDWR | O_SYNC);
    if (fd < 0) {
        length = snprintf(line, sizeof(line), "open-error=%d (%s)\n",
                          errno, strerror(errno));
        write(out, line, length);
        close(out);
        return 3;
    }
    base = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                MAP_OFFSET);
    if (base == MAP_FAILED) {
        length = snprintf(line, sizeof(line), "mmap-error=%d (%s)\n",
                          errno, strerror(errno));
        write(out, line, length);
        close(fd);
        close(out);
        return 4;
    }
    surface = &base[(DSPASURF - MAP_OFFSET) / 4];
    before = *surface;
    *surface = 0;
    __sync_synchronize();
    after = *surface;
    length = snprintf(line, sizeof(line),
                      "DSPASURF-before=0x%08x\nDSPASURF-after=0x%08x\n",
                      before, after);
    write(out, line, length);
    fsync(out);
    munmap((void *)base, MAP_SIZE);
    close(fd);
    close(out);
    return after == 0 ? 0 : 5;
}
