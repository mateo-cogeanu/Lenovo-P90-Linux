#include <errno.h>
#include <mntent.h>
#include <net/if.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef __NR_getrandom
#define __NR_getrandom 355
#endif

#ifndef __NR_memfd_create
#define __NR_memfd_create 319
#endif

/* Android 4.4 bionic omits these glibc interfaces. Kexec does not need to
 * enumerate interfaces during the load-only proof, so return an empty list. */
struct if_nameindex *if_nameindex(void)
{
    static struct if_nameindex empty[] = {{0, 0}};
    return empty;
}

#ifndef P90_STATIC_BIONIC_HAS_MNTENT
FILE *setmntent(const char *path, const char *mode)
{
    return fopen(path, mode);
}

int endmntent(FILE *stream)
{
    return fclose(stream) == 0 ? 1 : 0;
}
#endif

ssize_t getrandom(void *buffer, size_t length, unsigned int flags)
{
    return syscall(__NR_getrandom, buffer, length, flags);
}

int memfd_create(const char *name, unsigned int flags)
{
    return syscall(__NR_memfd_create, name, flags);
}
