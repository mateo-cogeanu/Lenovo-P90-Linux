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
#define STATUS "/data/local/tmp/p90-debian-xorg-phosh.txt"
#define XSOCKET ROOTFS "/tmp/.X11-unix/X1"
#define EXCLUSIVE_APPROVAL "/data/local/tmp/p90-freeze-surfaceflinger-approved"

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

static int bind_if_needed(int out, const char *source, const char *target,
                          const char *sentinel)
{
    int result;
    if (access(sentinel, F_OK) == 0)
        return 0;
    result = mount(source, target, NULL, MS_BIND | MS_REC, NULL);
    dprintf(out, "bind source=%s target=%s result=%d errno=%d (%s)\n",
            source, target, result, errno, strerror(errno));
    return result;
}

static int write_text_file(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ssize_t length = strlen(text);
    int result = -1;
    if (fd >= 0 && write(fd, text, length) == length) {
        fsync(fd);
        result = 0;
    }
    if (fd >= 0)
        close(fd);
    return result;
}

static int run_android(const char *program, char *const argv[])
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

static pid_t launch(int out, int phosh)
{
    pid_t child = fork();
    char *xorg_argv[] = {
        "/usr/lib/xorg/Xorg", ":1", "-config", "/root/p90-xorg-phosh.conf",
        "-nolisten", "tcp", "-noreset", "-novtswitch", "-sharevts",
        "-logfile", "/root/Xorg.p90-phosh.log", "-verbose", "6", NULL
    };
    char *phosh_argv[] = {
        "/bin/sh", "-c",
        "mkdir -p /run/user/0; chmod 700 /run/user/0; "
        "export HOME=/root PATH=/usr/sbin:/usr/bin:/sbin:/bin DISPLAY=:1; "
        "export XDG_RUNTIME_DIR=/run/user/0 WLR_BACKENDS=x11 WLR_X11_OUTPUTS=1; "
        "export WLR_RENDERER=pixman WLR_RENDERER_ALLOW_SOFTWARE=1; "
        "export WLR_LIBINPUT_NO_DEVICES=1 G_MESSAGES_DEBUG=all; "
        "exec /usr/bin/dbus-run-session -- /usr/bin/phoc "
        "-C /usr/share/phosh/phoc.ini -E /usr/libexec/phosh",
        NULL
    };

    if (child == 0) {
        setpgid(0, 0);
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        setenv("HOME", "/root", 1);
        setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
        if (chroot(ROOTFS) != 0 || chdir("/") != 0)
            _exit(126);
        execv(phosh ? phosh_argv[0] : xorg_argv[0],
              phosh ? phosh_argv : xorg_argv);
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

int main(void)
{
    int out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int config, socket_ready = 0, fb0_link_created = 0, status = 0;
    int shm_directory_created = 0;
    pid_t xorg = -1, phosh = -1;
    int exclusive = access(EXCLUSIVE_APPROVAL, F_OK) == 0;

    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    dprintf(out, "launcher uid=%d gid=%d euid=%d egid=%d\n",
            getuid(), getgid(), geteuid(), getegid());
    if (exclusive) {
        int unmount_result = umount2("/system/bin/dumpstate", MNT_DETACH);
        dprintf(out, "launcher-self-unmount-result=%d errno=%d (%s)\n",
                unmount_result, errno, strerror(errno));
        fsync(out);
        if (unmount_result != 0) {
            unlink(EXCLUSIVE_APPROVAL);
            close(out);
            return 4;
        }
    }
    bind_if_needed(out, "/dev", ROOTFS "/dev", ROOTFS "/dev/graphics/fb0");
    bind_if_needed(out, "/proc", ROOTFS "/proc", ROOTFS "/proc/version");
    bind_if_needed(out, "/sys", ROOTFS "/sys", ROOTFS "/sys/class/graphics/fb0/name");
    if (mkdir(ROOTFS "/dev/shm", 01777) == 0)
        shm_directory_created = 1;
    chmod(ROOTFS "/dev/shm", 01777);
    dprintf(out, "posix-shm-ready=%d errno=%d (%s)\n",
            access(ROOTFS "/dev/shm", W_OK) == 0, errno, strerror(errno));
    mkdir(ROOTFS "/run/udev", 0755);
    mkdir(ROOTFS "/run/udev/data", 0755);
    dprintf(out, "touchscreen-udev-db=%d\n",
            write_text_file(ROOTFS "/run/udev/data/c13:66",
                "I:1\nE:ID_INPUT=1\nE:ID_INPUT_TOUCHSCREEN=1\n"
                "E:ID_PATH=p90-synaptics-touchscreen\nV:1\n"));
    mkdir(ROOTFS "/tmp/.X11-unix", 0777);
    chmod(ROOTFS "/tmp/.X11-unix", 01777);
    if (access(ROOTFS "/dev/fb0", F_OK) != 0 &&
        symlink("/dev/graphics/fb0", ROOTFS "/dev/fb0") == 0) {
        fb0_link_created = 1;
        dprintf(out, "temporary-fb0-link=created\n");
    }
    config = open(ROOTFS "/root/p90-xorg-phosh.conf",
                  O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (config < 0 || write(config, xorg_config, sizeof(xorg_config) - 1) !=
            (ssize_t)(sizeof(xorg_config) - 1))
        return 3;
    fsync(config);
    close(config);

    dprintf(out, "surfaceflinger-kept-running=1\n");
    xorg = launch(out, 0);
    dprintf(out, "xorg-pid=%d\n", xorg);
    fsync(out);
    for (int i = 0; i < 10; ++i) {
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
        phosh = launch(out, 1);
        dprintf(out, "phoc-phosh-pid=%d\n", phosh);
        fsync(out);
        if (exclusive) {
            char *stop[] = {"setprop", "ctl.stop", "surfaceflinger", NULL};
            char *start[] = {"setprop", "ctl.start", "surfaceflinger", NULL};
            char *wake[] = {"input", "keyevent", "224", NULL};
            sleep(3);
            dprintf(out, "surfaceflinger-stop-result=%d\n",
                    run_android("/system/bin/setprop", stop));
            fsync(out);
            sleep(8);
            dprintf(out, "surfaceflinger-start-result=%d\n",
                    run_android("/system/bin/setprop", start));
            sleep(2);
            dprintf(out, "android-wake-result=%d\n",
                    run_android("/system/bin/input", wake));
            unlink(EXCLUSIVE_APPROVAL);
            fsync(out);
            sleep(9);
        } else {
            sleep(20);
        }
        if (waitpid(phosh, &status, WNOHANG) == phosh)
            dprintf(out, "phoc-phosh-exited=1 status=%d\n", status);
        else
            dprintf(out, "phoc-phosh-survived-20s=1\n");
    }
    terminate_group(phosh);
    terminate_group(xorg);
    if (fb0_link_created)
        dprintf(out, "temporary-fb0-link-remove=%d\n", unlink(ROOTFS "/dev/fb0"));
    if (shm_directory_created)
        dprintf(out, "temporary-dev-shm-remove=%d\n", rmdir(ROOTFS "/dev/shm"));
    dprintf(out, "probe-complete=1\n");
    fsync(out);
    close(out);
    return 0;
}
