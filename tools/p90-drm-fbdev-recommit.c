#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CARD "/dev/dri/card0"
#define STATUS "/data/local/tmp/p90-drm-fbdev-recommit.txt"

int main(void)
{
    struct drm_mode_crtc crtc;
    unsigned int connector = 8;
    char line[512];
    int card = open(CARD, O_RDWR);
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int result, saved_errno, length;

    if (card < 0 || out < 0)
        return 2;
    memset(&crtc, 0, sizeof(crtc));
    crtc.crtc_id = 3;
    result = ioctl(card, DRM_IOCTL_MODE_GETCRTC, &crtc);
    saved_errno = errno;
    length = snprintf(line, sizeof(line),
                      "getcrtc=%d errno=%d (%s) crtc=%u fb=%u "
                      "mode_valid=%u mode=%s %ux%u\n",
                      result, saved_errno, strerror(saved_errno),
                      crtc.crtc_id, crtc.fb_id, crtc.mode_valid,
                      crtc.mode.name, crtc.mode.hdisplay, crtc.mode.vdisplay);
    write(out, line, length);
    if (result != 0 || crtc.fb_id == 0 || !crtc.mode_valid) {
        close(card);
        close(out);
        return 3;
    }
    crtc.set_connectors_ptr =
        (unsigned long long)(unsigned long)&connector;
    crtc.count_connectors = 1;
    result = ioctl(card, DRM_IOCTL_MODE_SETCRTC, &crtc);
    saved_errno = errno;
    length = snprintf(line, sizeof(line),
                      "setcrtc=%d errno=%d (%s) connector=%u\n",
                      result, saved_errno, strerror(saved_errno), connector);
    write(out, line, length);
    fsync(out);
    close(card);
    close(out);
    return result == 0 ? 0 : 4;
}
