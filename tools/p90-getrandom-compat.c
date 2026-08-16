/* Compatibility for Linux 3.10, which predates getrandom(2) and memfd(2). */
#include <stdarg.h>

typedef unsigned long size_t;
typedef long ssize_t;

extern int open(const char *path, int flags, ...);
extern ssize_t read(int fd, void *buffer, size_t count);
extern int getpid(void);
extern int snprintf(char *buffer, size_t size, const char *format, ...);
extern int shm_open(const char *name, int oflag, unsigned int mode);
extern int shm_unlink(const char *name);
extern long syscall(long number, ...);

#define O_RDWR 2
#define O_CREAT 0100
#define O_EXCL 0200
#define F_SETFD 2
#define FD_CLOEXEC 1
#define MFD_CLOEXEC 1
#define F_ADD_SEALS 1033
#define F_GET_SEALS 1034
#define SYS_CLOSE 3
#define SYS_FCNTL 72

/* WebKit requires sealing.  POSIX shm has the same storage semantics but the
 * old kernel cannot enforce seals, so remember them for compatibility. */
static unsigned int emulated_seals[65536];
static unsigned char emulated_memfd[65536];

int fcntl(int fd, int command, ...)
{
    unsigned long argument = 0;
    va_list arguments;

    if (fd >= 0 && fd < (int)sizeof(emulated_memfd) && emulated_memfd[fd]) {
        if (command == F_ADD_SEALS) {
            va_start(arguments, command);
            argument = va_arg(arguments, unsigned long);
            va_end(arguments);
            emulated_seals[fd] |= (unsigned int)argument;
            return 0;
        }
        if (command == F_GET_SEALS)
            return (int)emulated_seals[fd];
    }

    if (command != 1 && command != 3 && command != 9 && command != 11 &&
        command != 1025 && command != 1032 && command != F_GET_SEALS) {
        va_start(arguments, command);
        argument = va_arg(arguments, unsigned long);
        va_end(arguments);
    }
    return (int)syscall(SYS_FCNTL, fd, command, argument);
}

int close(int fd)
{
    if (fd >= 0 && fd < (int)sizeof(emulated_memfd)) {
        emulated_memfd[fd] = 0;
        emulated_seals[fd] = 0;
    }
    return (int)syscall(SYS_CLOSE, fd);
}

/* wlroots uses memfd_create for anonymous wl_shm backing storage. */
int memfd_create(const char *label, unsigned int flags)
{
    static unsigned int sequence;
    char name[96];
    int fd;
    unsigned int attempt;
    (void)label;

    for (attempt = 0; attempt < 32; ++attempt) {
        unsigned int value = __sync_add_and_fetch(&sequence, 1);
        snprintf(name, sizeof(name), "/p90-memfd-%d-%u", getpid(), value);
        fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            if (fd < (int)sizeof(emulated_memfd))
                emulated_memfd[fd] = 1;
            if (flags & MFD_CLOEXEC)
                fcntl(fd, F_SETFD, FD_CLOEXEC);
            return fd;
        }
    }
    return -1;
}

ssize_t getrandom(void *buffer, size_t length, unsigned int flags)
{
    unsigned char *cursor = (unsigned char *)buffer;
    size_t remaining = length;
    int fd;
    (void)flags;

    fd = open("/dev/urandom", 0);
    if (fd < 0)
        return -1;
    while (remaining != 0) {
        ssize_t count = read(fd, cursor, remaining);
        if (count <= 0) {
            close(fd);
            return -1;
        }
        cursor += count;
        remaining -= (size_t)count;
    }
    close(fd);
    return (ssize_t)length;
}
