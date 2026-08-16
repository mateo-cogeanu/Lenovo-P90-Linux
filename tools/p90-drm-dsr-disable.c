#include <drm/drm.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CARD "/dev/dri/card0"
#define STATUS "/data/local/tmp/p90-drm-dsr-disable.txt"
#define DRM_PSB_DPU_DSR_ON 0x21
#define DRM_PSB_DSR_DISABLE 0xffffffffU
#define DRM_IOCTL_PSB_DPU_DSR_CONTROL \
    DRM_IOW(DRM_COMMAND_BASE + DRM_PSB_DPU_DSR_ON, unsigned int)

int main(void)
{
    unsigned int control = DRM_PSB_DSR_DISABLE;
    char line[160];
    int card = open(CARD, O_RDWR);
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int result, saved_errno, length;

    if (card < 0 || out < 0)
        return 2;
    result = ioctl(card, DRM_IOCTL_PSB_DPU_DSR_CONTROL, &control);
    saved_errno = errno;
    length = snprintf(line, sizeof(line),
                      "dsr-disable=%d errno=%d (%s) request=0x%08x\n",
                      result, saved_errno, strerror(saved_errno), control);
    write(out, line, length);
    fsync(out);
    close(card);
    close(out);
    return result == 0 ? 0 : 3;
}
