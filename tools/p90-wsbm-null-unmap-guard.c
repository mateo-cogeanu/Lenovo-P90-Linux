#include <fcntl.h>
#include <unistd.h>

/*
 * Diagnostic interposer for the Lenovo P90 KitKat WSBM stack.  The vendor HWC
 * can pass a BO whose method table has no unmap callback when it is initialized
 * outside SurfaceFlinger.  For one bounded diagnostic run, turn unmap into a
 * no-op to establish whether allocation can otherwise complete.  This leaks
 * the temporary object and must never be used as a production workaround.
 */
void wsbmBOUnmap(void *buffer)
{
    int fd;

    (void)buffer;
    /* Diagnostic only: deliberately leak this temporary mapping/object. */
    fd = open("/data/local/tmp/p90-wsbm-guard-hit.txt",
              O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        write(fd, "guarded-null-unmap\n", 19);
        close(fd);
    }
}
