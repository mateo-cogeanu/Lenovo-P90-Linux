#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ROOTFS "/data/local/p90-debian"
#define STATUS "/data/local/tmp/p90-debian-xorg-fbdev.txt"
#define XSOCKET ROOTFS "/tmp/.X11-unix/X1"

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
    "  Option \"ShadowFB\" \"true\"\n"
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
    "EndSection\n";

static int run_android(const char *program, char *const argv[])
{
    pid_t child = fork();
    int status = -1;

    if (child == 0) {
        execv(program, argv);
        _exit(127);
    }
    if (child > 0 && waitpid(child, &status, 0) == child &&
        WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

static void restore_android_display(void)
{
    char *start[] = {"setprop", "ctl.start", "surfaceflinger", NULL};
    char *wake[] = {"input", "keyevent", "224", NULL};

    run_android("/system/bin/setprop", start);
    sleep(2);
    run_android("/system/bin/input", wake);
}

static int ensure_bind(int out, const char *source, const char *target,
                       const char *sentinel)
{
    int result;

    if (access(sentinel, F_OK) == 0) {
        dprintf(out, "mount-ready target=%s\n", target);
        return 0;
    }
    result = mount(source, target, NULL, MS_BIND | MS_REC, NULL);
    dprintf(out, "bind source=%s target=%s result=%d errno=%d (%s)\n",
            source, target, result, errno, strerror(errno));
    return result;
}

static pid_t launch_xorg(int out)
{
    pid_t child = fork();
    char *argv[] = {
        "/usr/lib/xorg/Xorg", ":1", "-config", "/root/p90-xorg-fbdev.conf",
        "-nolisten", "tcp", "-noreset", "-novtswitch", "-sharevts",
        "-logfile", "/root/Xorg.p90.log", "-verbose", "6", NULL
    };

    if (child == 0) {
        setpgid(0, 0);
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        setenv("HOME", "/root", 1);
        setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
        if (chroot(ROOTFS) != 0 || chdir("/") != 0)
            _exit(126);
        execv(argv[0], argv);
        _exit(127);
    }
    if (child > 0)
        setpgid(child, child);
    return child;
}

static int paint_root(int out)
{
    pid_t child = fork();
    int status = -1;
    char *argv[] = {
        "/usr/bin/xsetroot", "-display", ":1", "-solid", "#3465a4", NULL
    };

    if (child == 0) {
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        if (chroot(ROOTFS) != 0 || chdir("/") != 0)
            _exit(126);
        execv(argv[0], argv);
        _exit(127);
    }
    if (child > 0 && waitpid(child, &status, 0) == child &&
        WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

int main(void)
{
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int config;
    int status = 0;
    int socket_ready = 0;
    int fb0_link_created = 0;
    pid_t xorg;

    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    dprintf(out, "launcher uid=%d gid=%d euid=%d egid=%d\n",
            getuid(), getgid(), geteuid(), getegid());

    ensure_bind(out, "/dev", ROOTFS "/dev", ROOTFS "/dev/graphics/fb0");
    ensure_bind(out, "/proc", ROOTFS "/proc", ROOTFS "/proc/version");
    ensure_bind(out, "/sys", ROOTFS "/sys",
                ROOTFS "/sys/class/graphics/fb0/name");
    mkdir(ROOTFS "/tmp/.X11-unix", 0777);
    chmod(ROOTFS "/tmp/.X11-unix", 01777);
    if (access(ROOTFS "/dev/fb0", F_OK) != 0 &&
        symlink("/dev/graphics/fb0", ROOTFS "/dev/fb0") == 0) {
        fb0_link_created = 1;
        dprintf(out, "temporary-fb0-link=created\n");
    } else {
        dprintf(out, "temporary-fb0-link=existing-or-failed errno=%d (%s)\n",
                errno, strerror(errno));
    }
    config = open(ROOTFS "/root/p90-xorg-fbdev.conf",
                  O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (config < 0 || write(config, xorg_config, sizeof(xorg_config) - 1) !=
            (ssize_t)(sizeof(xorg_config) - 1)) {
        dprintf(out, "config-write-failed errno=%d (%s)\n", errno,
                strerror(errno));
        if (config >= 0)
            close(config);
        close(out);
        return 3;
    }
    fsync(config);
    close(config);

    /* Keep SurfaceFlinger alive: psbfb allows a non-exclusive diagnostic open. */
    dprintf(out, "surfaceflinger-kept-running=1\n");
    fsync(out);
    xorg = launch_xorg(out);
    dprintf(out, "xorg-pid=%d\n", xorg);
    fsync(out);

    for (int i = 0; i < 8; ++i) {
        if (access(XSOCKET, F_OK) == 0) {
            socket_ready = 1;
            break;
        }
        if (waitpid(xorg, &status, WNOHANG) == xorg)
            break;
        sleep(1);
    }
    dprintf(out, "x11-socket-ready=%d\n", socket_ready);
    if (socket_ready) {
        dprintf(out, "xsetroot-result=%d\n", paint_root(out));
        fsync(out);
        sleep(10);
    }

    if (xorg > 0) {
        kill(-xorg, SIGTERM);
        sleep(2);
        kill(-xorg, SIGKILL);
        waitpid(xorg, &status, WNOHANG);
    }
    if (fb0_link_created) {
        dprintf(out, "temporary-fb0-link-remove=%d\n",
                unlink(ROOTFS "/dev/fb0"));
    }
    restore_android_display();
    dprintf(out, "android-display-restored=1\n");
    fsync(out);

    fsync(out);
    close(out);
    return 0;
}
