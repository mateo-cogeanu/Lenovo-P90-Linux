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
#define HYBRIS "/data/local/tmp/p90-hybris"
#define LOG "/data/local/tmp/p90-wayland-phosh-probe.txt"
#define AUTOBOOT_LOG "/data/local/tmp/p90-wayland-phosh-autoboot.txt"
#define SOCKET ROOTFS "/run/user/0/wayland-0"
#define GETRANDOM_SHIM "/data/local/tmp/p90-getrandom-compat.so"
#define DURATION_FILE "/data/local/tmp/p90-wayland-session-seconds"
#define CAMERA_CAPTURE "/data/local/p90-camera-hal-surface-jpeg-capture"
#define CAMERA_FIFO ROOTFS "/run/p90-camera-request"

static int out = -1;
static volatile sig_atomic_t stop_requested = 0;

static const char phoc_config[] =
    "[output:WL-1]\n"
    "mode = 540x960\n"
    "scale = 1\n";

/* The GTK preload collision that originally wedged KGX is fixed.  Keep a
   direct, non-D-Bus launcher so Console inherits only the getrandom shim. */
static const char terminal_wrapper[] =
    "#!/bin/sh\n"
    "export LD_PRELOAD=/run/p90-getrandom-compat.so\n"
    "exec /usr/bin/kgx \"$@\"\n";

static const char terminal_desktop[] =
    "[Desktop Entry]\n"
    "Name=Console\n"
    "Comment=Use the command line\n"
    "Exec=/usr/local/bin/p90-terminal\n"
    "Icon=org.gnome.Console\n"
    "Type=Application\n"
    "Terminal=false\n"
    "DBusActivatable=false\n"
    "Categories=GNOME;GTK;System;TerminalEmulator;\n"
    "Keywords=shell;prompt;command;commandline;cmd;\n"
    "StartupNotify=true\n";

/* The P90 camera app talks to a narrow root-owned FIFO.  Its host worker uses
   the verified factory camera HAL + hidden SurfaceFlinger preview route; it
   never starts Zygote, system_server, mediaserver, or an Android camera app. */
static const char camera_wrapper[] =
    "#!/bin/sh\n"
    "export LD_PRELOAD=/run/p90-getrandom-compat.so\n"
    "exec /usr/bin/python3 /usr/local/libexec/p90-camera-app \"$@\"\n";

static const char camera_desktop[] =
    "[Desktop Entry]\n"
    "Name=Camera\n"
    "Comment=Take photos\n"
    "Exec=/usr/local/bin/p90-camera\n"
    "Icon=org.postmarketos.Megapixels\n"
    "Type=Application\n"
    "Terminal=false\n"
    "DBusActivatable=false\n"
    "Categories=GTK;Photography;Graphics;\n"
    "Keywords=camera;photo;\n"
    "X-Purism-FormFactor=Workstation;Mobile;\n"
    "X-Phosh-UsesFeedback=true\n"
    "StartupNotify=false\n";

static const char pipewire_speaker_config[] =
    "context.properties = {\n"
    "    default.clock.rate = 48000\n"
    "    default.clock.allowed-rates = [ 48000 ]\n"
    "    default.clock.quantum = 1024\n"
    "    default.clock.min-quantum = 256\n"
    "}\n"
    "context.objects = [\n"
    "  { factory = adapter args = {\n"
    "      factory.name = api.alsa.pcm.sink\n"
    "      node.name = \"alsa_output.p90_speaker\"\n"
    "      node.description = \"Lenovo P90 Speaker\"\n"
    "      media.class = \"Audio/Sink\"\n"
    "      api.alsa.path = \"hw:1,0\"\n"
    "      api.alsa.period-size = 1024\n"
    "      api.alsa.period-num = 4\n"
    "      api.alsa.headroom = 0\n"
    "      api.alsa.disable-mmap = false\n"
    "      api.alsa.disable-batch = false\n"
    "      audio.format = \"S16LE\"\n"
    "      audio.rate = 48000\n"
    "      audio.channels = 2\n"
    "      audio.position = \"FL,FR\"\n"
    "      node.suspend-on-idle = false\n"
    "      resample.disable = true\n"
    "  } },\n"
    "  { factory = adapter args = {\n"
    "      factory.name = api.alsa.pcm.source\n"
    "      node.name = \"alsa_input.p90_microphone\"\n"
    "      node.description = \"Lenovo P90 Microphone\"\n"
    "      media.class = \"Audio/Source\"\n"
    "      api.alsa.path = \"hw:1,0\"\n"
    "      api.alsa.period-size = 1024\n"
    "      api.alsa.period-num = 4\n"
    "      api.alsa.headroom = 0\n"
    "      api.alsa.disable-mmap = false\n"
    "      api.alsa.disable-batch = false\n"
    "      audio.format = \"S16LE\"\n"
    "      audio.rate = 48000\n"
    "      audio.channels = 2\n"
    "      audio.position = \"FL,FR\"\n"
    "      node.suspend-on-idle = false\n"
    "      resample.disable = true\n"
    "  } }\n"
    "]\n";

static const char phosh_session[] =
    "#!/bin/sh\n"
    "export GDK_BACKEND=wayland GSK_RENDERER=cairo GTK_A11Y=none\n"
    "export WEBKIT_DISABLE_COMPOSITING_MODE=1\n"
    "export WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1\n"
    "export GIO_USE_VFS=local LANG=C.UTF-8 XDG_CURRENT_DESKTOP=Phosh:GNOME\n"
    "export LD_PRELOAD=/run/p90-getrandom-compat.so\n"
    "mkdir -p /root/.local/share/squeekboard/keyboards\n"
    "if [ -f /usr/local/share/p90/us_wide-portrait.yaml ]; then "
    "cp /usr/local/share/p90/us_wide-portrait.yaml "
    "/root/.local/share/squeekboard/keyboards/us_wide.yaml; fi\n"
    "gsettings set org.gnome.desktop.interface enable-animations true 2>/dev/null || true\n"
    "gsettings set org.gnome.desktop.a11y.applications screen-keyboard-enabled true 2>/dev/null || true\n"
    "gsettings set org.gnome.desktop.input-sources sources \"[('xkb', 'us')]\" 2>/dev/null || true\n"
    "/system/bin/tinymix -D 1 'PM4 DMIC1L ENA' 1\n"
    "/system/bin/tinymix -D 1 'PM4 DMIC1R ENA' 1\n"
    "/system/bin/tinymix -D 1 'AIF1ADC1L Mixer ADC/DMIC Switch' 1\n"
    "/system/bin/tinymix -D 1 'AIF1ADC1R Mixer ADC/DMIC Switch' 1\n"
    "/system/bin/tinymix -D 1 'codec_in0 gain 0 mute' 0\n"
    "/system/bin/tinymix -D 1 'pcm0_out mix 0 codec_in0' 1\n"
    "/system/bin/tinymix -D 1 'pcm1_out mix 0 codec_in0' 1\n"
    "/system/bin/tinymix -D 1 'pcm1_out gain 0 mute' 0\n"
    "/system/bin/tinymix -D 1 'pcm0_in gain 0 mute' 0\n"
    "/system/bin/tinymix -D 1 'pcm1_in gain 0 mute' 0\n"
    "/system/bin/tinymix -D 1 'codec_out0 mix 0 pcm0_in' 1\n"
    "/system/bin/tinymix -D 1 'codec_out0 mix 0 pcm1_in' 1\n"
    "/system/bin/tinymix -D 1 'codec_out0 gain 0 mute' 0\n"
    "/system/bin/tinymix -D 1 'DAC1L Mixer AIF1.1 Switch' 1\n"
    "/system/bin/tinymix -D 1 'DAC1R Mixer AIF1.1 Switch' 1\n"
    "/system/bin/tinymix -D 1 'DAC1 Switch' 1 1\n"
    "/system/bin/tinymix -D 1 'Left Output Mixer DAC Switch' 1\n"
    "/system/bin/tinymix -D 1 'SPKL DAC1 Switch' 1\n"
    "/system/bin/tinymix -D 1 'SPKL Boost SPKL Switch' 1\n"
    "/system/bin/tinymix -D 1 'Speaker Mixer Volume' 3 3\n"
    "/system/bin/tinymix -D 1 'Speaker Boost Volume' 7 2\n"
    "/system/bin/tinymix -D 1 'Speaker Volume' 45 45\n"
    "/system/bin/tinymix -D 1 'Speaker Switch' 1 1\n"
    "/usr/bin/pipewire >>/var/log/pipewire.p90.log 2>&1 &\n"
    "/usr/bin/pipewire-pulse >>/var/log/pipewire-pulse.p90.log 2>&1 &\n"
    "sleep 2\n"
    "/usr/bin/wireplumber -c main.conf >>/var/log/wireplumber-main.p90.log 2>&1 &\n"
    "/usr/bin/wireplumber -c policy.conf >>/var/log/wireplumber-policy.p90.log 2>&1 &\n"
    "/usr/libexec/feedbackd >>/var/log/feedbackd.p90.log 2>&1 &\n"
    "/usr/libexec/gsd-power >>/var/log/gsd-power.p90.log 2>&1 &\n"
    "/usr/libexec/gsd-rfkill >>/var/log/gsd-rfkill.p90.log 2>&1 &\n"
    "/usr/libexec/gsd-media-keys >>/var/log/gsd-media-keys.p90.log 2>&1 &\n"
    "/usr/libexec/gsd-sound >>/var/log/gsd-sound.p90.log 2>&1 &\n"
    "/usr/libexec/gsd-wwan >>/var/log/gsd-wwan.p90.log 2>&1 &\n"
    "sleep 2\n"
    "LD_PRELOAD=/usr/local/lib/p90-phosh-wallpaper-live.so:/run/p90-getrandom-compat.so "
    "/usr/libexec/phosh >>/var/log/phosh.p90-wayland.log 2>&1 &\n"
    "phosh_pid=$!\n"
    "sleep 3\n"
    "/usr/bin/squeekboard >>/var/log/squeekboard.p90-wayland.log 2>&1 &\n"
    "wait $phosh_pid\n";

static const char system_services[] =
    "#!/bin/sh\n"
    "export HOME=/root PATH=/usr/sbin:/usr/bin:/sbin:/bin\n"
    "export LD_PRELOAD=/run/p90-getrandom-compat.so\n"
    "mkdir -p /run/dbus /run/NetworkManager /run/ModemManager /run/wpa_supplicant /run/udev/data\n"
    "dbus-uuidgen --ensure\n"
    "dbus-daemon --system --fork\n"
    "cat >/run/udev/data/n3 <<'EOF'\n"
    "I:3\nE:ID_NET_DRIVER=wl\nE:ID_NET_NAME=wlan0\nE:INTERFACE=wlan0\n"
    "E:DEVTYPE=wlan\nE:NM_UNMANAGED=0\nV:1\nEOF\n"
    "chmod 0644 /run/udev/data/n3\n"
    "wpa_supplicant -u -s -Dnl80211 -O /run/wpa_supplicant >>/var/log/wpa_supplicant.p90.log 2>&1 &\n"
    /* The 2014 Broadcom firmware can scan before its association path is
       ready.  Starting NetworkManager after only two seconds leaves wlan0 in
       an unrecoverable prepare/config failure until NetworkManager restarts. */
    "sleep 8\n"
    "NetworkManager --no-daemon >>/var/log/NetworkManager.p90.log 2>&1 &\n"
    /* The first daemon instance makes the legacy wl/nl80211 supplicant
       interface available but cannot prepare wlan0.  A clean second instance
       plus an explicit activation is the verified association sequence. */
    "sleep 8\n"
    "pkill NetworkManager 2>/dev/null || true\n"
    "sleep 2\n"
    "NetworkManager --no-daemon >>/var/log/NetworkManager.p90.log 2>&1 &\n"
    "( sleep 10; if nmcli -t -f NAME connection show | grep -Fqx XGS; then "
    "nmcli --wait 30 connection up XGS >>/var/log/NetworkManager-connect.p90.log 2>&1 || true; fi ) &\n"
    "/usr/libexec/upowerd >>/var/log/upowerd.p90.log 2>&1 &\n"
    /* Lenovo's rild already owns the XMM7260 protocol and publishes the
       Android RIL socket.  oFono's rilmodem driver turns that socket into
       native Linux telephony D-Bus interfaces without Android's UI stack. */
    "OFONO_RIL_DEVICE=ril OFONO_RIL_RAT_LTE=1 "
    "ofonod -n >>/var/log/ofono-ril.p90.log 2>&1 &\n";

static void log_text(const char *text)
{
    write(out, text, strlen(text));
    fsync(out);
}

static void log_number(const char *name, long value)
{
    char line[160];
    int n = snprintf(line, sizeof(line), "%s=%ld\n", name, value);
    if (n > 0) write(out, line, n);
    fsync(out);
}

static int write_file(const char *path, const char *text, mode_t mode)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    size_t size = strlen(text);
    int result = -1;
    if (fd >= 0 && write(fd, text, size) == (ssize_t)size) {
        fchmod(fd, mode);
        fsync(fd);
        result = 0;
    }
    if (fd >= 0) close(fd);
    return result;
}

static int bind_if_missing(const char *source, const char *target,
                           const char *sentinel)
{
    if (access(sentinel, F_OK) == 0) return 0;
    return mount(source, target, NULL, MS_BIND | MS_REC, NULL);
}

static int property_equals(const char *name, const char *expected)
{
    int pipefd[2], status = -1;
    pid_t child;
    char value[32];
    ssize_t count;
    char *arguments[] = { "getprop", (char *)name, NULL };
    if (pipe(pipefd) != 0) return 0;
    child = fork();
    if (child == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execv("/system/bin/getprop", arguments);
        _exit(127);
    }
    close(pipefd[1]);
    count = read(pipefd[0], value, sizeof(value) - 1);
    close(pipefd[0]);
    if (child > 0) waitpid(child, &status, 0);
    if (count <= 0) return 0;
    value[count] = '\0';
    value[strcspn(value, "\r\n")] = '\0';
    return strcmp(value, expected) == 0;
}

static int service_action(const char *action, const char *service)
{
    pid_t child;
    int status = -1;
    char property[32];
    char *arguments[] = { "setprop", property, (char *)service, NULL };
    snprintf(property, sizeof(property), "ctl.%s", action);
    child = fork();
    if (child == 0) {
        execv("/system/bin/setprop", arguments);
        _exit(127);
    }
    if (child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

static void request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static void set_cpu_governor(const char *governor)
{
    char path[96];
    int cpu, fd;
    for (cpu = 0; cpu < 4; ++cpu) {
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", cpu);
        fd = open(path, O_WRONLY);
        if (fd >= 0) {
            write(fd, governor, strlen(governor));
            close(fd);
        }
    }
}

static pid_t launch_outer(void)
{
    pid_t child = fork();
    if (child == 0) {
        const char *library_path = HYBRIS "/lib:" HYBRIS "/runtime";
        setpgid(0, 0);
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        setenv("XDG_RUNTIME_DIR", ROOTFS "/run/user/0", 1);
        setenv("HYBRIS_EGLPLATFORM", "surfaceflinger", 1);
        setenv("HYBRIS_LINKER_DIR", HYBRIS "/lib/libhybris/linker", 1);
        setenv("HYBRIS_LD_LIBRARY_PATH",
               HYBRIS "/android-patched:/system/vendor/lib:/system/lib", 1);
        setenv("PATH", "/system/bin:/system/xbin", 1);
        execl(HYBRIS "/runtime/ld-linux.so.2", "ld-linux.so.2",
              "--library-path", library_path,
              HYBRIS "/bin/p90-wayland-sf-compositor",
              "wayland-0", "none", (char *)NULL);
        _exit(127);
    }
    if (child > 0) setpgid(child, child);
    return child;
}

static pid_t launch_phoc(void)
{
    static char command[] =
        "export HOME=/root PATH=/usr/sbin:/usr/bin:/sbin:/bin; "
        "export XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-0; "
        "export LD_PRELOAD=/run/p90-getrandom-compat.so; "
        "export WLR_BACKENDS=wayland,libinput WLR_RENDERER=pixman; "
        "export WLR_RENDERER_ALLOW_SOFTWARE=1 WLR_SESSION=direct; "
        "/run/p90-system-services; "
        "exec /usr/bin/dbus-run-session -- /usr/bin/phoc "
        "-C /run/p90-phoc-wayland.ini -E /run/p90-phosh-wayland-session";
    char *argv[] = { "/bin/sh", "-c", command, NULL };
    pid_t child = fork();
    if (child == 0) {
        setpgid(0, 0);
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        if (chroot(ROOTFS) || chdir("/")) _exit(126);
        execv(argv[0], argv);
        _exit(127);
    }
    if (child > 0) setpgid(child, child);
    return child;
}

static int valid_camera_target(const char *path)
{
    static const char prefix[] = ROOTFS "/root/Pictures/P90-Camera/";
    const unsigned char *name;
    size_t length;

    if (strncmp(path, prefix, sizeof(prefix) - 1) != 0) return 0;
    name = (const unsigned char *)path + sizeof(prefix) - 1;
    if (*name == '\0' || strstr((const char *)name, "..") != NULL) return 0;
    for (; *name; ++name) {
        if (!((*name >= 'a' && *name <= 'z') ||
              (*name >= 'A' && *name <= 'Z') ||
              (*name >= '0' && *name <= '9') ||
              *name == '-' || *name == '_' || *name == '.'))
            return 0;
    }
    length = strlen(path);
    return length > 4 && strcmp(path + length - 4, ".jpg") == 0;
}

static int wait_program(const char *program, const char *argument,
                        const char *log_path)
{
    pid_t child = fork();
    int status = -1, fd;
    if (child == 0) {
        if (log_path != NULL) {
            fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) {
                fchmod(fd, 0644);
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                close(fd);
            }
        }
        if (argument != NULL)
            execl(program, program, argument, (char *)NULL);
        else
            execl(program, program, (char *)NULL);
        _exit(127);
    }
    if (child > 0) waitpid(child, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 126;
}

static pid_t launch_camera_service(void)
{
    pid_t service = fork();
    if (service == 0) {
        char target[512], result_path[544], result_text[64];
        int fifo, count, status, n;
        pid_t sensor;

        setpgid(0, 0);
        unlink(CAMERA_FIFO);
        if (mkfifo(CAMERA_FIFO, 0666) != 0) _exit(120);
        chmod(CAMERA_FIFO, 0666);
        for (;;) {
            fifo = open(CAMERA_FIFO, O_RDONLY);
            if (fifo < 0) { sleep(1); continue; }
            count = read(fifo, target, sizeof(target) - 1);
            close(fifo);
            if (count <= 0) continue;
            target[count] = '\0';
            target[strcspn(target, "\r\n")] = '\0';
            if (!valid_camera_target(target)) continue;
            snprintf(result_path, sizeof(result_path), "%s.result", target);
            unlink(result_path);
            unlink(target);

            sensor = fork();
            if (sensor == 0) {
                execl("/system/bin/sensorservice", "sensorservice",
                      (char *)NULL);
                _exit(127);
            }
            sleep(2);
            setenv("LD_LIBRARY_PATH", "/system/lib", 1);
            status = wait_program(CAMERA_CAPTURE, target,
                                  ROOTFS "/var/log/p90-camera-capture.log");
            if (sensor > 0) {
                kill(sensor, SIGTERM);
                waitpid(sensor, NULL, 0);
            }
            if (status == 0) chmod(target, 0644);
            n = snprintf(result_text, sizeof(result_text), "%d\n", status);
            if (n > 0) write_file(result_path, result_text, 0644);
        }
    }
    if (service > 0) setpgid(service, service);
    return service;
}

static void terminate_group(pid_t child)
{
    if (child <= 0) return;
    kill(-child, SIGTERM);
    sleep(1);
    kill(-child, SIGKILL);
    waitpid(child, NULL, WNOHANG);
}

static int session_seconds(void)
{
    char value[24];
    int fd = open(DURATION_FILE, O_RDONLY);
    int count, seconds = 60;
    if (fd >= 0) {
        count = read(fd, value, sizeof(value) - 1);
        close(fd);
        if (count > 0) {
            value[count] = '\0';
            seconds = atoi(value);
        }
    }
    if (seconds < 30) seconds = 30;
    if (seconds > 1800) seconds = 1800;
    return seconds;
}

int main(int argc, char **argv)
{
    pid_t outer = -1, phoc = -1, camera_service = -1;
    int status = 0, i, ready = 0, duration, android_ui_stopped = 0;
    int persistent = strstr(argv[0], "p90-debian-autoboot-v") != NULL;
    const char *log_path = persistent ? AUTOBOOT_LOG : LOG;

    /* Escape the temporary Dirty-COW-backed executable mapping first. */
    if (!persistent && (argc < 2 || strcmp(argv[1], "--supervisor") != 0)) {
        execl("/data/local/tmp/p90-wayland-phosh-probe",
              "p90-wayland-phosh-probe", "--supervisor", (char *)NULL);
        return 125;
    }

    out = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0 || geteuid() != 0) return 2;
    fchmod(out, 0644);
    log_text("uid=0\nprobe=p90-wayland-phosh\n");
    duration = session_seconds();
    log_number("session-seconds", duration);
    log_number("persistent", persistent);

    signal(SIGTERM, request_stop);
    signal(SIGINT, request_stop);

    if (persistent) {
        for (i = 0; i < 180; ++i) {
            if (property_equals("sys.boot_completed", "1") &&
                access(ROOTFS "/usr/bin/phoc", X_OK) == 0 &&
                access(HYBRIS "/bin/p90-wayland-sf-compositor", X_OK) == 0)
                break;
            sleep(1);
        }
        log_number("boot-prerequisites-after", i);
        if (i == 180) return 7;
    }
    set_cpu_governor("performance");

    mkdir(ROOTFS "/system", 0755);
    bind_if_missing("/system", ROOTFS "/system", ROOTFS "/system/bin/sh");
    bind_if_missing("/dev", ROOTFS "/dev", ROOTFS "/dev/graphics/fb0");
    bind_if_missing("/proc", ROOTFS "/proc", ROOTFS "/proc/version");
    bind_if_missing("/sys", ROOTFS "/sys", ROOTFS "/sys/class/graphics/fb0/name");
    mkdir(ROOTFS "/dev/shm", 01777);
    chmod(ROOTFS "/dev/shm", 01777);
    mkdir(ROOTFS "/run/user", 0755);
    mkdir(ROOTFS "/run/user/0", 0700);
    chmod(ROOTFS "/run/user/0", 0700);
    mkdir(ROOTFS "/run/udev", 0755);
    mkdir(ROOTFS "/run/udev/data", 0755);
    mkdir(ROOTFS "/run/dbus", 0755);
    mkdir(ROOTFS "/etc/pipewire/pipewire.conf.d", 0755);
    mkdir(ROOTFS "/usr/local", 0755);
    mkdir(ROOTFS "/usr/local/bin", 0755);
    mkdir(ROOTFS "/usr/local/share", 0755);
    mkdir(ROOTFS "/usr/local/share/applications", 0755);
    unlink(ROOTFS "/run/dbus/system_bus_socket");
    unlink(ROOTFS "/run/dbus/pid");
    if (access(ROOTFS "/run/p90-getrandom-compat.so", F_OK) != 0 &&
        write_file(ROOTFS "/run/p90-getrandom-compat.so", "", 0644)) {
        log_number("compat-shim-placeholder", -errno);
        return 3;
    }
    /* Bind mounts do not survive reboot, so recreate this unconditionally. */
    if (mount(GETRANDOM_SHIM, ROOTFS "/run/p90-getrandom-compat.so",
              NULL, MS_BIND, NULL) != 0) {
        log_number("compat-shim-bind", -errno);
        return 3;
    }
    if (write_file(ROOTFS "/run/p90-phoc-wayland.ini", phoc_config, 0644) ||
        write_file(ROOTFS "/etc/pipewire/pipewire.conf.d/90-p90-speaker.conf",
                   pipewire_speaker_config, 0644) ||
        write_file(ROOTFS "/run/udev/data/c13:66",
                   "I:1\nE:ID_INPUT=1\nE:ID_INPUT_TOUCHSCREEN=1\n"
                   "E:ID_PATH=p90-synaptics-touchscreen\nV:1\n", 0644) ||
        write_file(ROOTFS "/usr/local/bin/p90-terminal",
                   terminal_wrapper, 0755) ||
        write_file(ROOTFS "/usr/local/share/applications/org.gnome.Console.desktop",
                   terminal_desktop, 0644) ||
        write_file(ROOTFS "/usr/local/bin/p90-camera",
                   camera_wrapper, 0755) ||
        write_file(ROOTFS "/usr/local/share/applications/org.postmarketos.Megapixels.desktop",
                   camera_desktop, 0644) ||
        write_file(ROOTFS "/run/p90-phosh-wayland-session", phosh_session, 0755) ||
        write_file(ROOTFS "/run/p90-system-services", system_services, 0755)) {
        log_text("runtime-files-failed=1\n");
        return 4;
    }
    if (persistent && access(CAMERA_CAPTURE, X_OK) == 0) {
        camera_service = launch_camera_service();
        log_number("camera-service-pid", camera_service);
    } else if (persistent) {
        log_text("camera-capture-helper-missing=1\n");
    }
    unlink(SOCKET);
    unlink(SOCKET ".lock");
    outer = launch_outer();
    log_number("outer-pid", outer);
    for (i = 0; i < 15; ++i) {
        if (access(SOCKET, F_OK) == 0) { ready = 1; break; }
        if (waitpid(outer, &status, WNOHANG) == outer) break;
        sleep(1);
    }
    log_number("outer-ready", ready);
    if (!ready) {
        terminate_group(outer);
        terminate_group(camera_service);
        return 5;
    }

    if (persistent) {
        log_number("android-dhcp-stop", service_action("stop", "dhcpcd_wlan0"));
        log_number("android-wpa-stop", service_action("stop", "wpa_supplicant"));
        log_number("android-p2p-stop", service_action("stop", "p2p_supplicant"));
        log_number("wlan-interface-up", service_action("start", "ifcfg_mac80211"));
        sleep(2);
    }

    phoc = launch_phoc();
    log_number("phoc-pid", phoc);
    for (i = 0; (persistent || i < duration) && !stop_requested; ++i) {
        if (waitpid(phoc, &status, WNOHANG) == phoc) {
            log_number("phoc-early-status", status);
            break;
        }
        if (waitpid(outer, &status, WNOHANG) == outer) {
            log_number("outer-early-status", status);
            break;
        }
        if (i == 8) {
            log_text("phosh-display-window=ready\n");
            if (persistent) {
                log_number("android-media-stop", service_action("stop", "media"));
                log_number("android-zygote-stop", service_action("stop", "zygote"));
                android_ui_stopped = 1;
                log_text("linux-only-ui=1\n");
            }
        }
        sleep(1);
    }
    terminate_group(phoc);
    terminate_group(outer);
    terminate_group(camera_service);
    if (android_ui_stopped) {
        log_number("android-zygote-restore", service_action("start", "zygote"));
        log_number("android-media-restore", service_action("start", "media"));
    }
    log_number("probe-seconds", i);
    log_text("probe-complete=1\n");
    close(out);
    return 0;
}
