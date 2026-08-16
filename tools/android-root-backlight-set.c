#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BRIGHTNESS "/sys/class/backlight/psb-bl/brightness"
#define STATUS "/data/local/tmp/p90-backlight-set.txt"

int main(void)
{
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int brightness;
    int result = -1;
    const char value[] = "128\n";
    char line[64];
    int length;

    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    brightness = open(BRIGHTNESS, O_WRONLY);
    if (brightness >= 0 && write(brightness, value, sizeof(value) - 1) ==
            (ssize_t)(sizeof(value) - 1))
        result = 0;
    if (brightness >= 0)
        close(brightness);
    length = snprintf(line, sizeof(line), "uid=%d brightness-result=%d\n",
                      geteuid(), result);
    write(out, line, length);
    fsync(out);
    close(out);
    return result == 0 ? 0 : 3;
}
