#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define STATUS "/data/local/tmp/p90-psb-fbdev-plane-switch-devmem.txt"
#define MMIO_PAGE 0xc0070000UL
#define MMIO_SIZE 0x1000
#define REG(offset) ((offset) - 0x70000)

int main(void)
{
    volatile uint32_t *mmio;
    uint32_t cntr, linoff, stride, pos, size, before, after;
    char log[768];
    int length;
    int mem, out;

    out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    mem = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem < 0) {
        length = snprintf(log, sizeof(log), "open-error=%d (%s)\n",
                          errno, strerror(errno));
        write(out, log, length);
        close(out);
        return 3;
    }
    mmio = mmap(NULL, MMIO_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                mem, MMIO_PAGE);
    if (mmio == MAP_FAILED) {
        length = snprintf(log, sizeof(log), "mmap-error=%d (%s)\n",
                          errno, strerror(errno));
        write(out, log, length);
        close(mem);
        close(out);
        return 4;
    }
    cntr = mmio[REG(0x70180) / 4];
    linoff = mmio[REG(0x70184) / 4];
    stride = mmio[REG(0x70188) / 4];
    pos = mmio[REG(0x7018c) / 4];
    size = mmio[REG(0x70190) / 4];
    before = mmio[REG(0x7019c) / 4];
    length = snprintf(log, sizeof(log),
                      "DSPACNTR=0x%08x\nDSPALINOFF=0x%08x\n"
                      "DSPASTRIDE=0x%08x\nDSPAPOS=0x%08x\n"
                      "DSPASIZE=0x%08x\nDSPASURF-before=0x%08x\n",
                      cntr, linoff, stride, pos, size, before);
    write(out, log, length);
    if (cntr != 0x98000000U || linoff != 0 || stride != 0x1100U ||
        pos != 0 || size != 0x077f0437U ||
        (before != 0x038a0000U && before != 0x048a0000U)) {
        write(out, "validation-failed=1\n",
              sizeof("validation-failed=1\n") - 1);
        munmap((void *)mmio, MMIO_SIZE);
        close(mem);
        close(out);
        return 5;
    }
    mmio[REG(0x7019c) / 4] = 0;
    __sync_synchronize();
    usleep(100000);
    after = mmio[REG(0x7019c) / 4];
    length = snprintf(log, sizeof(log), "DSPASURF-after=0x%08x\n", after);
    write(out, log, length);
    fsync(out);
    munmap((void *)mmio, MMIO_SIZE);
    close(mem);
    close(out);
    return after == 0 ? 0 : 6;
}
