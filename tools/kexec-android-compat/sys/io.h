#ifndef P90_ANDROID_COMPAT_SYS_IO_H
#define P90_ANDROID_COMPAT_SYS_IO_H

/* Minimal freestanding x86 port-I/O helpers for kexec-tools purgatory.
 * Android bionic does not ship glibc's <sys/io.h>. */
static __inline__ unsigned char inb(unsigned short port)
{
    unsigned char value;
    __asm__ volatile ("inb %w1,%0" : "=a" (value) : "Nd" (port));
    return value;
}

static __inline__ unsigned short inw(unsigned short port)
{
    unsigned short value;
    __asm__ volatile ("inw %w1,%0" : "=a" (value) : "Nd" (port));
    return value;
}

static __inline__ unsigned int inl(unsigned short port)
{
    unsigned int value;
    __asm__ volatile ("inl %w1,%0" : "=a" (value) : "Nd" (port));
    return value;
}

static __inline__ void outb(unsigned char value, unsigned short port)
{
    __asm__ volatile ("outb %0,%w1" : : "a" (value), "Nd" (port));
}

static __inline__ void outw(unsigned short value, unsigned short port)
{
    __asm__ volatile ("outw %0,%w1" : : "a" (value), "Nd" (port));
}

static __inline__ void outl(unsigned int value, unsigned short port)
{
    __asm__ volatile ("outl %0,%w1" : : "a" (value), "Nd" (port));
}

#endif
