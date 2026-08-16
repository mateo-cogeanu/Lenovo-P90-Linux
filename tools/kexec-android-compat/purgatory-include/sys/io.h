#ifndef P90_PURGATORY_SYS_IO_H
#define P90_PURGATORY_SYS_IO_H

static __inline unsigned char inb(unsigned short port)
{
    unsigned char value;
    __asm__ volatile("inb %w1,%0" : "=a" (value) : "Nd" (port));
    return value;
}

static __inline void outb(unsigned char value, unsigned short port)
{
    __asm__ volatile("outb %b0,%w1" : : "a" (value), "Nd" (port));
}

static __inline void outw(unsigned short value, unsigned short port)
{
    __asm__ volatile("outw %w0,%w1" : : "a" (value), "Nd" (port));
}

#endif
