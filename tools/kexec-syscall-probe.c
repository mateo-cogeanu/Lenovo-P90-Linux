#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef __NR_kexec_load
#define __NR_kexec_load 283
#endif

#ifndef __NR_reboot
#define __NR_reboot 88
#endif

#define KEXEC_ARCH_X86_64 (62UL << 16)

static void report(const char *name, long result)
{
    printf("%s result=%ld errno=%d (%s)\n",
           name, result, errno, strerror(errno));
}

int main(void)
{
    long result;

    errno = 0;
    result = syscall(__NR_kexec_load, 0UL, 0UL, 0UL, 0UL);
    printf("kexec_load syscall=%d ", __NR_kexec_load);
    report("zero-segment", result);

    /* Bit 14 is not a legal kexec flag. This call must fail before it can
     * allocate or install an image, even for a privileged process. */
    errno = 0;
    result = syscall(__NR_kexec_load, 0UL, 0UL, 0UL, 1UL << 14);
    report("kexec-invalid-flags", result);

    /* A 32-bit process on this 64-bit kernel must explicitly name the native
     * architecture before the compat wrapper reaches the capability check. */
    errno = 0;
    result = syscall(__NR_kexec_load, 0UL, 0UL, 0UL,
                     KEXEC_ARCH_X86_64 | (1UL << 14));
    report("kexec-x86_64-invalid-flags", result);

    /* The reboot magic values are deliberately invalid. A capable process
     * gets EINVAL; an incapable process gets EPERM. It cannot reboot. */
    errno = 0;
    result = syscall(__NR_reboot, 0UL, 0UL, 0UL, 0UL);
    report("reboot-invalid-magic", result);

    return 0;
}
