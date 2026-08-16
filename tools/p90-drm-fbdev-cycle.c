#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CARD "/dev/dri/card0"
#define STATUS "/data/local/tmp/p90-drm-fbdev-cycle.txt"

static void record(int out, const char *name, int result)
{
    char line[160];
    int saved_errno = errno;
    int length = snprintf(line, sizeof(line), "%s=%d errno=%d (%s)\n",
                          name, result, saved_errno, strerror(saved_errno));
    write(out, line, length);
    fsync(out);
}

int main(void)
{
    struct drm_mode_crtc saved, disable;
    unsigned int connector = 8;
    int card = open(CARD, O_RDWR);
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int result;

    if (card < 0 || out < 0)
        return 2;
    memset(&saved, 0, sizeof(saved));
    saved.crtc_id = 3;
    result = ioctl(card, DRM_IOCTL_MODE_GETCRTC, &saved);
    record(out, "getcrtc", result);
    if (result != 0 || saved.fb_id == 0 || !saved.mode_valid)
        return 3;
    memset(&disable, 0, sizeof(disable));
    disable.crtc_id = saved.crtc_id;
    result = ioctl(card, DRM_IOCTL_MODE_SETCRTC, &disable);
    record(out, "disable-crtc", result);
    if (result != 0)
        return 4;
    sleep(1);
    saved.set_connectors_ptr =
        (unsigned long long)(unsigned long)&connector;
    saved.count_connectors = 1;
    result = ioctl(card, DRM_IOCTL_MODE_SETCRTC, &saved);
    record(out, "enable-fbdev-crtc", result);
    close(card);
    close(out);
    return result == 0 ? 0 : 5;
}
