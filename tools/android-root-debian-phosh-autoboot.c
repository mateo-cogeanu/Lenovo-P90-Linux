#include <errno.h>
#include <fcntl.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ROOTFS "/data/local/p90-debian"
#define STATUS "/data/local/tmp/p90-debian-autoboot.txt"
#define XSOCKET ROOTFS "/tmp/.X11-unix/X1"
#define DISABLE "/data/local/tmp/p90-debian-autoboot-disabled"
#define GETRANDOM_SHIM "/data/local/tmp/p90-getrandom-compat.so"
#define DRM_PSB_DPU_DSR_ON 0x21
#define DRM_PSB_DSR_DISABLE 0xffffffffU
#define DRM_IOCTL_PSB_DPU_DSR_CONTROL \
    DRM_IOW(DRM_COMMAND_BASE + DRM_PSB_DPU_DSR_ON, unsigned int)

static int out = -1;
static pid_t xorg = -1;
static pid_t phosh = -1;

static const char xorg_config[] =
    "Section \"ServerFlags\"\n"
    "  Option \"AutoAddDevices\" \"false\"\n"
    "  Option \"AutoEnableDevices\" \"false\"\n"
    "  Option \"AutoAddGPU\" \"false\"\n"
    "  Option \"DontVTSwitch\" \"true\"\n"
    "  Option \"AllowMouseOpenFail\" \"true\"\n"
    "EndSection\n"
    "Section \"Device\"\n"
    "  Identifier \"P90 psbfb\"\n"
    "  Driver \"fbdev\"\n"
    "  Option \"fbdev\" \"/dev/graphics/fb0\"\n"
    "  Option \"ShadowFB\" \"false\"\n"
    "EndSection\n"
    "Section \"Monitor\"\n"
    "  Identifier \"P90 DSI panel\"\n"
    "EndSection\n"
    "Section \"Screen\"\n"
    "  Identifier \"P90 screen\"\n"
    "  Device \"P90 psbfb\"\n"
    "  Monitor \"P90 DSI panel\"\n"
    "  DefaultDepth 24\n"
    "  SubSection \"Display\"\n"
    "    Depth 24\n"
    "    Modes \"1080x1920\"\n"
    "  EndSubSection\n"
    "EndSection\n"
    "Section \"InputDevice\"\n"
    "  Identifier \"P90 touchscreen\"\n"
    "  Driver \"libinput\"\n"
    "  Option \"Device\" \"/dev/input/event2\"\n"
    "  Option \"Touchscreen\" \"on\"\n"
    "EndSection\n"
    "Section \"ServerLayout\"\n"
    "  Identifier \"P90 layout\"\n"
    "  Screen \"P90 screen\"\n"
    "  InputDevice \"P90 touchscreen\" \"CorePointer\"\n"
    "EndSection\n";

static const char phoc_config[] =
    "[output:X11-1]\n"
    "mode = 1080x1920\n"
    "scale = 2\n";

static const char phosh_session[] =
    "#!/bin/sh\n"
    "export GDK_BACKEND=wayland GSK_RENDERER=cairo GTK_A11Y=none\n"
    "export GIO_USE_VFS=local LANG=C.UTF-8\n"
    "export LD_PRELOAD=/run/p90-getrandom-compat.so\n"
    "umask 022\n"
    "gsettings set org.gnome.desktop.interface enable-animations false 2>/dev/null || true\n"
    "gsettings set org.gnome.desktop.background picture-uri 'file:///run/p90-background.ppm' 2>/dev/null || true\n"
    "gsettings set org.gnome.desktop.background picture-uri-dark 'file:///run/p90-background.ppm' 2>/dev/null || true\n"
    "gsettings set org.gnome.desktop.background picture-options zoom 2>/dev/null || true\n"
    "gsettings set org.gnome.desktop.a11y.applications screen-keyboard-enabled true 2>/dev/null || true\n"
    "/usr/libexec/phosh &\n"
    "phosh_pid=$!\n"
    "sleep 3\n"
    "/usr/bin/squeekboard >>/var/log/squeekboard.p90.log 2>&1 &\n"
    "wait $phosh_pid\n";

/* PPM is supported by baseline gdk-pixbuf; no optional SVG/WebP loader needed. */
static const char background_ppm[] =
    "P3\n4 6\n255\n"
    "24 38 74  27 40 78  31 41 82  34 42 85\n"
    "28 37 76  34 39 83  42 40 89  50 40 93\n"
    "35 34 78  45 36 88  55 37 96  64 38 100\n"
    "42 31 75  54 33 87  66 35 96  75 36 100\n"
    "34 29 64  44 31 75  55 33 84  64 34 89\n"
    "21 24 44  27 26 52  34 28 59  40 29 63\n";

static const char hidden_desktop[] =
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=Unavailable prototype app\n"
    "NoDisplay=true\n"
    "Hidden=true\n";

static void log_line(const char *text)
{
    if (out >= 0) {
        write(out, text, strlen(text));
        fsync(out);
    }
}

static void log_number(const char *name, long value)
{
    char line[128];
    int length = snprintf(line, sizeof(line), "%s=%ld\n", name, value);
    if (out >= 0 && length > 0)
        write(out, line, length);
    if (out >= 0)
        fsync(out);
}

static int write_file(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    size_t length = strlen(text);
    int result = -1;
    if (fd >= 0 && write(fd, text, length) == (ssize_t)length) {
        fsync(fd);
        result = 0;
    }
    if (fd >= 0)
        close(fd);
    return result;
}

static int android_command(const char *program, char *const argv[])
{
    pid_t child = fork();
    int status = -1;
    if (child == 0) {
        execv(program, argv);
        _exit(127);
    }
    if (child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

static int property_equals(const char *name, const char *expected)
{
    int pipefd[2];
    pid_t child;
    int status = -1;
    char value[8];
    ssize_t count;
    char *argv[] = {"getprop", (char *)name, NULL};

    if (pipe(pipefd) != 0)
        return 0;
    child = fork();
    if (child == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execv("/system/bin/getprop", argv);
        _exit(127);
    }
    close(pipefd[1]);
    count = read(pipefd[0], value, sizeof(value) - 1);
    close(pipefd[0]);
    if (child > 0)
        waitpid(child, &status, 0);
    if (count <= 0)
        return 0;
    value[count] = '\0';
    value[strcspn(value, "\r\n")] = '\0';
    return strcmp(value, expected) == 0;
}

static int ensure_backlight(void)
{
    const char *path = "/sys/class/backlight/psb-bl/brightness";
    char value[32];
    int fd = open(path, O_RDONLY);
    int current = 0;
    int length;

    if (fd >= 0) {
        length = read(fd, value, sizeof(value) - 1);
        close(fd);
        if (length > 0) {
            value[length] = '\0';
            current = atoi(value);
        }
    }
    if (current > 0)
        return 0;
    fd = open(path, O_WRONLY);
    if (fd < 0)
        return -1;
    length = write(fd, "128\n", 4);
    close(fd);
    return length == 4 ? 1 : -1;
}

static int bind_if_missing(const char *source, const char *target,
                           const char *sentinel)
{
    if (access(sentinel, F_OK) == 0)
        return 0;
    return mount(source, target, NULL, MS_BIND | MS_REC, NULL);
}

static int set_numeric_pin(void)
{
    const char password[] = "root:2468\n";
    char *argv[] = {"chpasswd", NULL};
    int input[2], status = -1;
    pid_t child;

    if (pipe(input) != 0)
        return -1;
    child = fork();
    if (child == 0) {
        close(input[1]);
        dup2(input[0], STDIN_FILENO);
        close(input[0]);
        if (chroot(ROOTFS) != 0 || chdir("/") != 0)
            _exit(126);
        execv("/usr/sbin/chpasswd", argv);
        _exit(127);
    }
    close(input[0]);
    write(input[1], password, sizeof(password) - 1);
    close(input[1]);
    if (child > 0)
        waitpid(child, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void set_cpu_governor(const char *governor)
{
    char path[96];
    int cpu, fd;
    for (cpu = 0; cpu < 4; ++cpu) {
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor",
                 cpu);
        fd = open(path, O_WRONLY);
        if (fd >= 0) {
            write(fd, governor, strlen(governor));
            close(fd);
        }
    }
}

static pid_t launch_debian(int compositor)
{
    pid_t child = fork();
    char *xorg_argv[] = {
        "/usr/lib/xorg/Xorg", ":1", "-config", "/run/p90-xorg.conf",
        "-nolisten", "tcp", "-noreset", "-novtswitch", "-sharevts",
        "-logfile", "/var/log/Xorg.p90-autoboot.log", "-verbose", "0", NULL
    };
    char *phosh_argv[] = {
        "/bin/sh", "-c",
        "export HOME=/root PATH=/usr/sbin:/usr/bin:/sbin:/bin DISPLAY=:1; "
        "export XDG_RUNTIME_DIR=/run/user/0 WLR_BACKENDS=x11 WLR_X11_OUTPUTS=1; "
        "export WLR_RENDERER=pixman WLR_RENDERER_ALLOW_SOFTWARE=1; "
        "export WLR_LIBINPUT_NO_DEVICES=1; "
        "/usr/bin/unclutter --timeout 0 --hide-on-touch --start-hidden "
        "--fork 2>/dev/null || true; "
        "exec /usr/bin/dbus-run-session -- /usr/bin/phoc "
        "-C /run/p90-phoc.ini -E /run/p90-phosh-session",
        NULL
    };

    if (child == 0) {
        setpgid(0, 0);
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        if (chroot(ROOTFS) != 0 || chdir("/") != 0)
            _exit(126);
        execv(compositor ? phosh_argv[0] : xorg_argv[0],
              compositor ? phosh_argv : xorg_argv);
        _exit(127);
    }
    if (child > 0)
        setpgid(child, child);
    return child;
}

static void terminate_group(pid_t child)
{
    if (child <= 0)
        return;
    kill(-child, SIGTERM);
    sleep(2);
    kill(-child, SIGKILL);
    waitpid(child, NULL, WNOHANG);
}

static void restore_android(void)
{
    char *start[] = {"setprop", "ctl.start", "surfaceflinger", NULL};
    char *wake[] = {"input", "keyevent", "224", NULL};
    android_command("/system/bin/setprop", start);
    sleep(3);
    android_command("/system/bin/input", wake);
}

static int activate_fbdev_scanout(void)
{
    struct fb_var_screeninfo var;
    int fb = open("/dev/graphics/fb0", O_RDWR);
    int backlight = 128;
    int brightness;
    int result;
    char value[32];
    int length;

    brightness = open("/sys/class/backlight/psb-bl/brightness", O_RDONLY);
    if (brightness >= 0) {
        length = read(brightness, value, sizeof(value) - 1);
        if (length > 0) {
            value[length] = '\0';
            backlight = atoi(value);
            if (backlight <= 0)
                backlight = 128;
        }
        close(brightness);
    }
    log_number("scanout-saved-brightness", backlight);

    if (fb < 0) {
        log_number("scanout-open", -1);
        return -1;
    }
    memset(&var, 0, sizeof(var));
    result = ioctl(fb, FBIOGET_VSCREENINFO, &var);
    log_number("scanout-get-var", result);
    if (result != 0) {
        close(fb);
        return -1;
    }
    log_number("scanout-blank",
               ioctl(fb, FBIOBLANK, FB_BLANK_POWERDOWN));
    sleep(1);
    var.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
    log_number("scanout-put-var", ioctl(fb, FBIOPUT_VSCREENINFO, &var));
    var.activate = FB_ACTIVATE_NOW;
    log_number("scanout-pan", ioctl(fb, FBIOPAN_DISPLAY, &var));
    result = ioctl(fb, FBIOBLANK, FB_BLANK_UNBLANK);
    log_number("scanout-unblank", result);
    close(fb);
    brightness = open("/sys/class/backlight/psb-bl/brightness", O_WRONLY);
    if (brightness >= 0) {
        length = snprintf(value, sizeof(value), "%d\n", backlight);
        log_number("scanout-backlight-write",
                   write(brightness, value, length) == length ? 0 : -1);
        close(brightness);
    } else {
        log_number("scanout-backlight-write", -1);
    }
    return result;
}

static int activate_drm_fbdev_plane(void)
{
    struct drm_mode_crtc saved;
    struct drm_mode_crtc disable;
    unsigned int connector = 8;
    int card = open("/dev/dri/card0", O_RDWR);
    int result;

    if (card < 0)
        return -1;
    memset(&saved, 0, sizeof(saved));
    saved.crtc_id = 3;
    if (ioctl(card, DRM_IOCTL_MODE_GETCRTC, &saved) != 0 ||
        saved.fb_id == 0 || !saved.mode_valid) {
        close(card);
        return -2;
    }
    memset(&disable, 0, sizeof(disable));
    disable.crtc_id = saved.crtc_id;
    result = ioctl(card, DRM_IOCTL_MODE_SETCRTC, &disable);
    if (result != 0) {
        close(card);
        return -3;
    }
    usleep(200000);
    saved.set_connectors_ptr =
        (unsigned long long)(unsigned long)&connector;
    saved.count_connectors = 1;
    result = ioctl(card, DRM_IOCTL_MODE_SETCRTC, &saved);
    close(card);
    return result;
}

static int disable_panel_self_refresh(void)
{
    unsigned int control = DRM_PSB_DSR_DISABLE;
    int card = open("/dev/dri/card0", O_RDWR);
    int result;

    if (card < 0)
        return -1;
    result = ioctl(card, DRM_IOCTL_PSB_DPU_DSR_CONTROL, &control);
    close(card);
    return result;
}

static int read_psb_plane_surface(unsigned int *value)
{
    const unsigned int map_offset = 0x70000;
    const unsigned int map_size = 0x1000;
    const unsigned int dspasurf = 0x7019c;
    volatile unsigned int *base;
    int fd = open("/sys/bus/pci/devices/0000:00:02.0/resource0",
                  O_RDONLY | O_SYNC);

    if (fd < 0)
        return -1;
    base = mmap(NULL, map_size, PROT_READ, MAP_SHARED, fd, map_offset);
    if (base == MAP_FAILED) {
        close(fd);
        return -2;
    }
    *value = base[(dspasurf - map_offset) / 4];
    munmap((void *)base, map_size);
    close(fd);
    return 0;
}

static int activate_psb_fbdev_surface_mmio(void)
{
    const unsigned long mmio_page = 0xc0070000UL;
    const unsigned int map_size = 0x1000;
    volatile unsigned int *mmio;
    unsigned int cntr, linoff, stride, pos, size, before, after;
    int mem = open("/dev/mem", O_RDWR | O_SYNC);

    if (mem < 0)
        return -1;
    mmio = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                mem, mmio_page);
    if (mmio == MAP_FAILED) {
        close(mem);
        return -2;
    }
    cntr = mmio[(0x70180 - 0x70000) / 4];
    linoff = mmio[(0x70184 - 0x70000) / 4];
    stride = mmio[(0x70188 - 0x70000) / 4];
    pos = mmio[(0x7018c - 0x70000) / 4];
    size = mmio[(0x70190 - 0x70000) / 4];
    before = mmio[(0x7019c - 0x70000) / 4];
    log_number("mmio-dspasurf-before", before);
    if ((cntr != 0x98000000U && cntr != 0x9c000000U) ||
        linoff != 0 || stride != 0x1100U ||
        pos != 0 || size != 0x077f0437U ||
        (before != 0 && ((before & 0xfffU) != 0 || before >= 0x20000000U))) {
        munmap((void *)mmio, map_size);
        close(mem);
        return -3;
    }
    if (before != 0) {
        mmio[(0x7019c - 0x70000) / 4] = 0;
        __sync_synchronize();
        usleep(100000);
    }
    after = mmio[(0x7019c - 0x70000) / 4];
    log_number("mmio-dspasurf-after", after);
    munmap((void *)mmio, map_size);
    close(mem);
    return after == 0 ? 0 : -4;
}

int main(void)
{
    int ready = 0;
    int status = 0;
    int i;
    char *stop[] = {"setprop", "ctl.stop", "surfaceflinger", NULL};

    out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    log_number("uid", geteuid());
    if (access(DISABLE, F_OK) == 0) {
        log_line("disabled=1\n");
        return 0;
    }
    for (i = 0; i < 60; ++i) {
        if (access(ROOTFS "/usr/lib/xorg/Xorg", X_OK) == 0 &&
            access(ROOTFS "/usr/bin/phoc", X_OK) == 0 &&
            access("/dev/graphics/fb0", F_OK) == 0)
            break;
        sleep(1);
    }
    if (i == 60) {
        log_line("hardware-or-rootfs-timeout=1\n");
        return 3;
    }
    log_number("prerequisites-ready-after", i);
    /*
     * The hook belongs to class main, while Android starts SurfaceFlinger
     * again later in the boot.  Do not hand the display to Linux until that
     * late Android transition has happened, otherwise Android can reclaim
     * the primary plane and set the backlight to zero behind us.
     */
    for (i = 0; i < 120 &&
                !property_equals("sys.boot_completed", "1"); ++i)
        sleep(1);
    log_number("android-boot-complete-after", i);

    bind_if_missing("/dev", ROOTFS "/dev", ROOTFS "/dev/graphics/fb0");
    bind_if_missing("/proc", ROOTFS "/proc", ROOTFS "/proc/version");
    bind_if_missing("/sys", ROOTFS "/sys",
                    ROOTFS "/sys/class/graphics/fb0/name");
    mkdir(ROOTFS "/dev/shm", 01777);
    chmod(ROOTFS "/dev/shm", 01777);
    mkdir(ROOTFS "/run/user", 0755);
    mkdir(ROOTFS "/run/user/0", 0700);
    chmod(ROOTFS "/run/user/0", 0700);
    mkdir(ROOTFS "/run/udev", 0755);
    mkdir(ROOTFS "/run/udev/data", 0755);
    mkdir(ROOTFS "/tmp/.X11-unix", 0777);
    chmod(ROOTFS "/tmp/.X11-unix", 01777);
    mkdir(ROOTFS "/root/.local", 0755);
    mkdir(ROOTFS "/root/.local/share", 0755);
    mkdir(ROOTFS "/root/.local/share/applications", 0755);
    unlink(ROOTFS "/dev/fb0");
    symlink("/dev/graphics/fb0", ROOTFS "/dev/fb0");
    unlink(ROOTFS "/var/log/squeekboard.p90.log");
    write_file(ROOTFS "/run/p90-getrandom-compat.so", "");
    if (mount(GETRANDOM_SHIM, ROOTFS "/run/p90-getrandom-compat.so",
              NULL, MS_BIND, NULL) != 0) {
        log_line("getrandom-shim-bind-failed=1\n");
        return 4;
    }

    if (write_file(ROOTFS "/run/udev/data/c13:66",
                   "I:1\nE:ID_INPUT=1\nE:ID_INPUT_TOUCHSCREEN=1\n"
                   "E:ID_PATH=p90-synaptics-touchscreen\nV:1\n") != 0 ||
        write_file(ROOTFS "/run/p90-xorg.conf", xorg_config) != 0 ||
        write_file(ROOTFS "/run/p90-phoc.ini", phoc_config) != 0 ||
        write_file(ROOTFS "/run/p90-background.ppm", background_ppm) != 0 ||
        write_file(ROOTFS "/root/.local/share/applications/org.gnome.Nautilus.desktop",
                   hidden_desktop) != 0 ||
        write_file(ROOTFS "/root/.local/share/applications/org.gnome.TextEditor.desktop",
                   hidden_desktop) != 0 ||
        write_file(ROOTFS "/run/p90-phosh-session", phosh_session) != 0 ||
        chmod(ROOTFS "/run/p90-phosh-session", 0755) != 0) {
        log_line("runtime-file-setup-failed=1\n");
        return 4;
    }

    log_number("surfaceflinger-stop-result",
               android_command("/system/bin/setprop", stop));
    sleep(2);
    log_number("dsr-disable-result", disable_panel_self_refresh());
    log_number("numeric-pin-result", set_numeric_pin());
    set_cpu_governor("performance");
    log_line("cpu-governor=performance\n");

    unlink(XSOCKET);
    unlink(ROOTFS "/tmp/.X1-lock");
    xorg = launch_debian(0);
    log_number("xorg-pid", xorg);
    for (i = 0; i < 15; ++i) {
        if (access(XSOCKET, F_OK) == 0) {
            ready = 1;
            break;
        }
        if (waitpid(xorg, &status, WNOHANG) == xorg)
            break;
        sleep(1);
    }
    log_number("x11-ready", ready);
    if (!ready) {
        terminate_group(xorg);
        restore_android();
        return 5;
    }

    phosh = launch_debian(1);
    log_number("phoc-phosh-pid", phosh);
    sleep(8);
    if (waitpid(phosh, &status, WNOHANG) == phosh) {
        log_number("phoc-phosh-early-exit", status);
        terminate_group(xorg);
        restore_android();
        return 6;
    }

    log_number("fbdev-scanout-result", activate_fbdev_scanout());
    log_number("drm-fbdev-plane-result", activate_drm_fbdev_plane());
    log_number("mmio-fbdev-plane-result",
               activate_psb_fbdev_surface_mmio());
    log_line("linux-screen-active=1\n");

    /*
     * Android 4.4 components can explicitly restart SurfaceFlinger or blank
     * the backlight long after sys.boot_completed.  Keep the Linux display
     * authoritative for as long as Phosh is alive.
     */
    for (;;) {
        pid_t result = waitpid(phosh, &status, WNOHANG);
        int backlight_result;
        unsigned int plane_surface = 0;
        if (result == phosh)
            break;
        if (result < 0 && errno != EINTR)
            break;
        if (property_equals("init.svc.surfaceflinger", "running")) {
            log_number("supervisor-surfaceflinger-stop",
                       android_command("/system/bin/setprop", stop));
        }
        if (read_psb_plane_surface(&plane_surface) == 0 &&
            plane_surface != 0) {
            log_number("supervisor-stale-dspasurf", plane_surface);
            log_number("supervisor-mmio-plane-result",
                       activate_psb_fbdev_surface_mmio());
        }
        backlight_result = ensure_backlight();
        if (backlight_result != 0)
            log_number("supervisor-backlight-result", backlight_result);
        sleep(2);
    }
    log_number("phoc-phosh-exit-status", status);
    terminate_group(phosh);
    terminate_group(xorg);
    set_cpu_governor("interactive");
    restore_android();
    log_line("android-display-restored=1\n");
    close(out);
    return 0;
}
