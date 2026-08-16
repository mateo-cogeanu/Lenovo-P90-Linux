#ifndef P90_KEXEC_ANDROID_COMPAT_H
#define P90_KEXEC_ANDROID_COMPAT_H

#include <net/if.h>
#include <stddef.h>
#include <sys/types.h>

struct if_nameindex *if_nameindex(void);
int memfd_create(const char *name, unsigned int flags);
ssize_t getrandom(void *buffer, size_t length, unsigned int flags);

#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 0x0002U
#endif

#endif
