#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define STATUS "/data/local/tmp/p90-psb-dsi-context-switch.txt"

static int record(int out, const char *name, unsigned long long value)
{
    char line[128];
    int length = snprintf(line, sizeof(line), "%s=0x%llx\n", name, value);
    return write(out, line, length) == length ? 0 : -1;
}

static unsigned long long find_symbol(const char *wanted)
{
    FILE *symbols = fopen("/proc/kallsyms", "r");
    char line[512], type, name[256];
    unsigned long long address;
    if (!symbols)
        return 0;
    while (fgets(line, sizeof(line), symbols)) {
        if (sscanf(line, "%llx %c %255s", &address, &type, name) == 3 &&
            strcmp(name, wanted) == 0) {
            fclose(symbols);
            return address;
        }
    }
    fclose(symbols);
    return 0;
}

static int read_phys(int fd, unsigned long long address, void *value, size_t size)
{
    long page_size = sysconf(_SC_PAGESIZE);
    unsigned long long page = address & ~((unsigned long long)page_size - 1);
    size_t offset = (size_t)(address - page);
    void *mapping;
    if (offset + size > (size_t)page_size) {
        errno = EINVAL;
        return -1;
    }
    mapping = mmap(NULL, page_size, PROT_READ, MAP_SHARED, fd, (off_t)page);
    if (mapping == MAP_FAILED)
        return -1;
    memcpy(value, (char *)mapping + offset, size);
    munmap(mapping, page_size);
    return 0;
}

static int write_phys(int fd, unsigned long long address,
                      const void *value, size_t size)
{
    long page_size = sysconf(_SC_PAGESIZE);
    unsigned long long page = address & ~((unsigned long long)page_size - 1);
    size_t offset = (size_t)(address - page);
    void *mapping;
    if (offset + size > (size_t)page_size) {
        errno = EINVAL;
        return -1;
    }
    mapping = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   fd, (off_t)page);
    if (mapping == MAP_FAILED)
        return -1;
    memcpy((char *)mapping + offset, value, size);
    msync(mapping, page_size, MS_SYNC);
    munmap(mapping, page_size);
    return 0;
}

static int virtual_to_physical(int mem, unsigned long long pgd,
                               unsigned long long virt,
                               unsigned long long *physical)
{
    const unsigned long long address_mask = 0x000ffffffffff000ULL;
    unsigned int shifts[] = {39, 30, 21, 12};
    unsigned long long table = pgd;
    unsigned long long entry;
    int level;

    for (level = 0; level < 4; ++level) {
        unsigned long long slot = (virt >> shifts[level]) & 0x1ffULL;
        if (read_phys(mem, table + slot * 8, &entry, sizeof(entry)) != 0 ||
            !(entry & 1ULL))
            return -1;
        if (level == 1 && (entry & (1ULL << 7))) {
            *physical = (entry & 0x000fffffc0000000ULL) |
                        (virt & 0x3fffffffULL);
            return 0;
        }
        if (level == 2 && (entry & (1ULL << 7))) {
            *physical = (entry & 0x000fffffffe00000ULL) |
                        (virt & 0x1fffffULL);
            return 0;
        }
        table = entry & address_mask;
    }
    *physical = (entry & address_mask) | (virt & 0xfffULL);
    return 0;
}

static int set_kptr(const char *value)
{
    int fd = open("/proc/sys/kernel/kptr_restrict", O_WRONLY);
    size_t length = strlen(value);
    int result = -1;
    if (fd >= 0 && write(fd, value, length) == (ssize_t)length)
        result = 0;
    if (fd >= 0)
        close(fd);
    return result;
}

int main(void)
{
    unsigned long long global_dev, kernel_text, level4, pgd_phys;
    unsigned long long dev_private, dsi_config, physical;
    uint32_t dspcntr, dspsurf, stride, zero = 0, verify;
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int mem;

    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    if (set_kptr("0\n") != 0) {
        record(out, "kptr-disable-errno", errno);
        return 3;
    }
    global_dev = find_symbol("globle_dev");
    kernel_text = find_symbol("_text");
    level4 = find_symbol("init_level4_pgt");
    set_kptr("2\n");
    record(out, "globle_dev", global_dev);
    record(out, "kernel-text", kernel_text);
    record(out, "init-level4-pgt", level4);
    if (!global_dev || !kernel_text || !level4)
        return 3;
    /* /proc/iomem reports this exact S149 kernel image at 0x02000000. */
    pgd_phys = level4 - kernel_text + 0x02000000ULL;
    record(out, "pgd-physical", pgd_phys);
    mem = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem < 0) {
        record(out, "mem-open-errno", errno);
        return 4;
    }
    if (virtual_to_physical(mem, pgd_phys, global_dev + 0x5e0,
                            &physical) != 0 ||
        read_phys(mem, physical, &dev_private, sizeof(dev_private)) != 0 ||
        virtual_to_physical(mem, pgd_phys, dev_private + 0xf8,
                            &physical) != 0 ||
        read_phys(mem, physical, &dsi_config, sizeof(dsi_config)) != 0 ||
        virtual_to_physical(mem, pgd_phys, dsi_config + 0x1f4,
                            &physical) != 0 ||
        read_phys(mem, physical, &dspcntr, sizeof(dspcntr)) != 0 ||
        virtual_to_physical(mem, pgd_phys, dsi_config + 0x1fc,
                            &physical) != 0 ||
        read_phys(mem, physical, &dspsurf, sizeof(dspsurf)) != 0 ||
        virtual_to_physical(mem, pgd_phys, dsi_config + 0x204,
                            &physical) != 0 ||
        read_phys(mem, physical, &stride, sizeof(stride)) != 0) {
        record(out, "read-errno", errno);
        close(mem);
        return 5;
    }
    record(out, "dev_private", dev_private);
    record(out, "dsi_config", dsi_config);
    record(out, "cached-dspcntr", dspcntr);
    record(out, "cached-dspsurf-before", dspsurf);
    record(out, "cached-stride", stride);
    if (stride != 4352 || dspcntr != 0x98000000U ||
        (dspsurf != 0x038a0000U && dspsurf != 0x048a0000U)) {
        record(out, "validation-failed", 1);
        close(mem);
        return 6;
    }
    if (virtual_to_physical(mem, pgd_phys, dsi_config + 0x1fc,
                            &physical) != 0 ||
        write_phys(mem, physical, &zero, sizeof(zero)) != 0 ||
        read_phys(mem, physical, &verify, sizeof(verify)) != 0) {
        record(out, "write-errno", errno);
        close(mem);
        return 7;
    }
    record(out, "cached-dspsurf-after", verify);
    fsync(out);
    close(mem);
    close(out);
    return verify == 0 ? 0 : 8;
}
