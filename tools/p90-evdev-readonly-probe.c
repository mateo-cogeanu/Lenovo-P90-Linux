#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void print_abs(int fd, unsigned int code, const char *label)
{
    struct input_absinfo info;

    if (ioctl(fd, EVIOCGABS(code), &info) == 0) {
        printf("%s value=%d min=%d max=%d fuzz=%d flat=%d resolution=%d\n",
               label, info.value, info.minimum, info.maximum, info.fuzz,
               info.flat, info.resolution);
    }
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/dev/input/event2";
    char name[256] = {0};
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);

    if (fd < 0) {
        fprintf(stderr, "open(%s): %s\n", path, strerror(errno));
        return 1;
    }
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
        fprintf(stderr, "EVIOCGNAME: %s\n", strerror(errno));
        close(fd);
        return 2;
    }
    printf("device=%s name=%s\n", path, name);
    print_abs(fd, ABS_X, "ABS_X");
    print_abs(fd, ABS_Y, "ABS_Y");
    print_abs(fd, ABS_MT_POSITION_X, "ABS_MT_POSITION_X");
    print_abs(fd, ABS_MT_POSITION_Y, "ABS_MT_POSITION_Y");
    print_abs(fd, ABS_MT_SLOT, "ABS_MT_SLOT");
    close(fd);
    return 0;
}
