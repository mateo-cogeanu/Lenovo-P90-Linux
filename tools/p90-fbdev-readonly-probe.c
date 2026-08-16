#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static uint32_t fnv1a32(const unsigned char *data, size_t length)
{
    uint32_t hash = UINT32_C(2166136261);

    for (size_t i = 0; i < length; ++i) {
        hash ^= data[i];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/dev/graphics/fb0";
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    long page_size;
    size_t sample_size;
    void *mapping;
    int fd;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open(%s): %s\n", path, strerror(errno));
        return 1;
    }
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        fprintf(stderr, "FBIOGET_FSCREENINFO: %s\n", strerror(errno));
        close(fd);
        return 2;
    }
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
        fprintf(stderr, "FBIOGET_VSCREENINFO: %s\n", strerror(errno));
        close(fd);
        return 3;
    }

    printf("device=%s\n", path);
    printf("id=%.16s\n", fix.id);
    printf("memory_bytes=%u line_length=%u visual=%u type=%u\n",
           fix.smem_len, fix.line_length, fix.visual, fix.type);
    printf("visible=%ux%u virtual=%ux%u offset=%u,%u bpp=%u rotate=%u\n",
           var.xres, var.yres, var.xres_virtual, var.yres_virtual,
           var.xoffset, var.yoffset, var.bits_per_pixel, var.rotate);
    printf("red=%u/%u green=%u/%u blue=%u/%u alpha=%u/%u\n",
           var.red.offset, var.red.length, var.green.offset, var.green.length,
           var.blue.offset, var.blue.length, var.transp.offset,
           var.transp.length);

    page_size = sysconf(_SC_PAGESIZE);
    sample_size = page_size > 0 ? (size_t)page_size : 4096;
    if (fix.smem_len && sample_size > fix.smem_len)
        sample_size = fix.smem_len;
    mapping = mmap(NULL, sample_size, PROT_READ, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        fprintf(stderr, "readonly mmap: %s\n", strerror(errno));
        close(fd);
        return 4;
    }
    printf("readonly_sample_bytes=%zu fnv1a32=%08" PRIx32 "\n",
           sample_size, fnv1a32(mapping, sample_size));
    munmap(mapping, sample_size);
    close(fd);
    return 0;
}
