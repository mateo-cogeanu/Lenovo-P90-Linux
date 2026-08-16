#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define RESOURCE "/sys/bus/pci/devices/0000:00:02.0/resource0"
#define STATUS "/data/local/tmp/p90-psb-plane-probe.txt"
#define MAP_OFFSET 0x70000
#define MAP_SIZE 0x1000

static unsigned int reg(volatile unsigned int *base, unsigned int address)
{
    return base[(address - MAP_OFFSET) / 4];
}

static void record(int out, const char *name, unsigned int value)
{
    char line[96];
    int length = snprintf(line, sizeof(line), "%s=0x%08x\n", name, value);
    write(out, line, length);
}

int main(void)
{
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int fd;
    volatile unsigned int *base;

    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    fd = open(RESOURCE, O_RDONLY | O_SYNC);
    if (fd < 0) {
        char line[128];
        int length = snprintf(line, sizeof(line), "open-error=%d (%s)\n",
                              errno, strerror(errno));
        write(out, line, length);
        close(out);
        return 3;
    }
    base = mmap(NULL, MAP_SIZE, PROT_READ, MAP_SHARED, fd, MAP_OFFSET);
    if (base == MAP_FAILED) {
        char line[128];
        int length = snprintf(line, sizeof(line), "mmap-error=%d (%s)\n",
                              errno, strerror(errno));
        write(out, line, length);
        close(fd);
        close(out);
        return 4;
    }
    record(out, "DSPACNTR", reg(base, 0x70180));
    record(out, "DSPALINOFF", reg(base, 0x70184));
    record(out, "DSPASTRIDE", reg(base, 0x70188));
    record(out, "DSPAPOS", reg(base, 0x7018c));
    record(out, "DSPASIZE", reg(base, 0x70190));
    record(out, "DSPASURF", reg(base, 0x7019c));
    fsync(out);
    munmap((void *)base, MAP_SIZE);
    close(fd);
    close(out);
    return 0;
}
