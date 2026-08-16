/*
 * Lenovo P90 / Intel Moorefield kexec handoff shim.
 *
 * Lenovo's Intel SCU watchdog reboot notifier re-arms the watchdog during
 * kernel_kexec(), after userspace has stopped it.  This module deliberately
 * performs the normal device and x86 machine shutdown steps, but omits the
 * reboot-notifier chain before entering the already-loaded kexec image.
 */
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/kexec.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

static struct kimage **p90_kexec_image;
static void (*p90_device_shutdown)(void);
static void (*p90_machine_shutdown)(void);
static void (*p90_machine_kexec)(struct kimage *image);
static int go;

static int p90_set_go(const char *value, struct kernel_param *kp)
{
	int requested;
	int ret = kstrtoint(value, 0, &requested);

	if (ret)
		return ret;
	if (requested != 1)
		return -EINVAL;
	if (!p90_kexec_image || !*p90_kexec_image || !p90_device_shutdown ||
	    !p90_machine_shutdown || !p90_machine_kexec)
		return -ENODEV;

	go = 1;
	pr_emerg("p90_kexec_handoff: bypassing reboot notifiers\n");
	p90_device_shutdown();
	p90_machine_shutdown();
	p90_machine_kexec(*p90_kexec_image);

	pr_emerg("p90_kexec_handoff: ERROR machine_kexec returned\n");
	return -EIO;
}

module_param_call(go, p90_set_go, param_get_int, &go, 0200);
MODULE_PARM_DESC(go, "Write 1 to enter the already-loaded kexec image");

static int __init p90_handoff_init(void)
{
	p90_kexec_image = (struct kimage **)kallsyms_lookup_name("kexec_image");
	p90_device_shutdown = (void *)kallsyms_lookup_name("device_shutdown");
	p90_machine_shutdown = (void *)kallsyms_lookup_name("machine_shutdown");
	p90_machine_kexec = (void *)kallsyms_lookup_name("machine_kexec");

	if (!p90_kexec_image || !p90_device_shutdown || !p90_machine_shutdown ||
	    !p90_machine_kexec) {
		pr_err("p90_kexec_handoff: required symbol missing\n");
		return -ENOENT;
	}

	pr_info("p90_kexec_handoff: ready image=%px device=%px machine=%px enter=%px\n",
		p90_kexec_image, p90_device_shutdown, p90_machine_shutdown,
		p90_machine_kexec);
	return 0;
}

static void __exit p90_handoff_exit(void)
{
	pr_info("p90_kexec_handoff: unloaded without handoff\n");
}

module_init(p90_handoff_init);
module_exit(p90_handoff_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("P90 native Linux port");
MODULE_DESCRIPTION("Lenovo P90 kexec handoff without SCU watchdog notifier");
