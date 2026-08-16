#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define FRAMEBUFFER "/dev/graphics/fb0"
#define STATUS "/data/local/tmp/p90-fbdev-scanout-activate.txt"

static void record(int out, const char *name, int result, int saved_errno)
{
    char line[256];
    int length = snprintf(line, sizeof(line), "%s=%d errno=%d (%s)\n",
                          name, result, saved_errno, strerror(saved_errno));
    write(out, line, length);
    fsync(out);
}

int main(void)
{
    struct fb_var_screeninfo var;
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int fb = open(FRAMEBUFFER, O_RDWR);
    int result;
    int saved_errno;

    if (out < 0 || fb < 0)
        return 2;
    memset(&var, 0, sizeof(var));
    result = ioctl(fb, FBIOGET_VSCREENINFO, &var);
    saved_errno = errno;
    record(out, "get-var", result, saved_errno);
    if (result != 0)
        return 3;

    result = ioctl(fb, FBIOBLANK, FB_BLANK_POWERDOWN);
    saved_errno = errno;
    record(out, "blank-powerdown", result, saved_errno);
    sleep(1);

    var.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
    result = ioctl(fb, FBIOPUT_VSCREENINFO, &var);
    saved_errno = errno;
    record(out, "put-var-force", result, saved_errno);

    var.activate = FB_ACTIVATE_NOW;
    result = ioctl(fb, FBIOPAN_DISPLAY, &var);
    saved_errno = errno;
    record(out, "pan-display", result, saved_errno);

    result = ioctl(fb, FBIOBLANK, FB_BLANK_UNBLANK);
    saved_errno = errno;
    record(out, "unblank", result, saved_errno);
    close(fb);
    close(out);
    return result == 0 ? 0 : 4;
}
