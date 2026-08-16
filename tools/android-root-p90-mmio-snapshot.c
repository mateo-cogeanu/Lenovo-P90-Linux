#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define OUTPUT "/data/local/tmp/p90-mmio-snapshot.txt"

int main(void)
{
    const unsigned long page = 0xc0070000UL;
    volatile unsigned int *mmio;
    int mem, out;
    char line[256];
    int length;

    out = open(OUTPUT, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    mem = open("/dev/mem", O_RDONLY | O_SYNC);
    if (mem < 0)
        return 3;
    mmio = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, mem, page);
    if (mmio == MAP_FAILED)
        return 4;
    length = snprintf(line, sizeof(line),
                      "cntr=%08x linoff=%08x stride=%08x pos=%08x "
                      "size=%08x surf=%08x\n",
                      mmio[(0x70180 - 0x70000) / 4],
                      mmio[(0x70184 - 0x70000) / 4],
                      mmio[(0x70188 - 0x70000) / 4],
                      mmio[(0x7018c - 0x70000) / 4],
                      mmio[(0x70190 - 0x70000) / 4],
                      mmio[(0x7019c - 0x70000) / 4]);
    write(out, line, length);
    fsync(out);
    munmap((void *)mmio, 0x1000);
    close(mem);
    close(out);
    return 0;
}
