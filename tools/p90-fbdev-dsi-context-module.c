/* Minimal Lenovo P90 module: switch and cache DSI primary surface as psbfb. */
extern unsigned long kallsyms_lookup_name(const char *name);
extern int printk(const char *format, ...);

typedef void (*p90_dsr_forbid_fn)(void *dev, int pipe);
typedef void (*p90_flip_fn)(void *dev, unsigned long address,
                            unsigned long format, unsigned long stride,
                            unsigned int pipe);

int init_module(void)
{
    void *dev = (void *)kallsyms_lookup_name("globle_dev");
    p90_dsr_forbid_fn forbid = (p90_dsr_forbid_fn)
        kallsyms_lookup_name("DCCBDsrForbid");
    p90_flip_fn flip = (p90_flip_fn)
        kallsyms_lookup_name("DCCBFlipToSurface");

    if (!dev || !flip) {
        printk("p90_fbdev_context: symbols missing dev=%p flip=%p\n",
               dev, flip);
        return -2;
    }
    if (forbid)
        forbid(dev, 0);
    /* 0x18000000 is Moorefield's 32-bpp no-alpha plane format. */
    flip(dev, 0, 0x18000000UL, 4352UL, 0);
    printk("p90_fbdev_context: DSI context switched to psbfb offset zero\n");
    return 0;
}

void cleanup_module(void)
{
    printk("p90_fbdev_context: module unloaded\n");
}
