#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define STAGED_BINARY "/data/local/tmp/p90-debian-autoboot"
#define STAGED_SCRIPT "/data/local/tmp/p90-debian-autoboot-service.sh"
#define TARGET_BINARY "/system/bin/p90-debian-autoboot-v34"
#define TARGET_SCRIPT "/system/etc/install-recovery.sh"
#define STOCK_SCRIPT "/system/etc/install-recovery.sh.p90-stock"
#define STATUS "/data/local/tmp/p90-debian-autoboot-install.txt"
#define SYSTEM_DEVICE "/dev/block/mmcblk0p8"
#define ROOTFS "/data/local/p90-debian"
#define STAGED_CAMERA_CAPTURE "/data/local/tmp/p90-camera-capture-v34"
#define TARGET_CAMERA_CAPTURE "/data/local/p90-camera-hal-surface-jpeg-capture"
#define STAGED_CAMERA_APP "/data/local/tmp/p90-camera-app-v34"
#define TARGET_CAMERA_APP ROOTFS "/usr/local/libexec/p90-camera-app"
#define PACKAGE_SOURCE "/data/local/tmp/p90-debs-v2"
#define PACKAGE_TARGET ROOTFS "/tmp/p90-debs-v2"
#define PACKAGE_COMPLETE "/data/local/tmp/p90-debs-v2-installed"

static int out = -1;

static void log_result(const char *name, int result, int saved_errno)
{
    char line[256];
    int length = snprintf(line, sizeof(line), "%s=%d errno=%d (%s)\n",
                          name, result, saved_errno, strerror(saved_errno));
    if (out >= 0 && length > 0) {
        write(out, line, length);
        fsync(out);
    }
}

static int copy_file(const char *source, const char *target, mode_t mode)
{
    char buffer[16384];
    int input = open(source, O_RDONLY);
    int output;
    ssize_t count;

    if (input < 0)
        return -1;
    output = open(target, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (output < 0) {
        close(input);
        return -1;
    }
    while ((count = read(input, buffer, sizeof(buffer))) > 0) {
        ssize_t offset = 0;
        while (offset < count) {
            ssize_t written = write(output, buffer + offset, count - offset);
            if (written <= 0) {
                close(input);
                close(output);
                return -1;
            }
            offset += written;
        }
    }
    fsync(output);
    fchmod(output, mode);
    close(input);
    close(output);
    return count < 0 ? -1 : 0;
}

static int install_debian_packages(void)
{
    pid_t child;
    int status = -1;
    int mounted_dev = 0, mounted_proc = 0, mounted_sys = 0, mounted_packages = 0;

    mkdir(PACKAGE_TARGET, 0755);
    if (mount("/dev", ROOTFS "/dev", NULL, MS_BIND, NULL) == 0)
        mounted_dev = 1;
    if (mount("/proc", ROOTFS "/proc", NULL, MS_BIND, NULL) == 0)
        mounted_proc = 1;
    if (mount("/sys", ROOTFS "/sys", NULL, MS_BIND, NULL) == 0)
        mounted_sys = 1;
    if (mount(PACKAGE_SOURCE, PACKAGE_TARGET, NULL, MS_BIND, NULL) == 0)
        mounted_packages = 1;
    if (!mounted_dev || !mounted_proc || !mounted_sys || !mounted_packages)
        goto cleanup;

    child = fork();
    if (child == 0) {
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        if (chroot(ROOTFS) != 0 || chdir("/") != 0)
            _exit(126);
        execl("/bin/sh", "sh", "-c",
              "export DEBIAN_FRONTEND=noninteractive "
              "PATH=/usr/sbin:/usr/bin:/sbin:/bin; "
              "dpkg -i /tmp/p90-debs-v2/*.deb || exit $?; "
              "dpkg --configure -a || exit $?; "
              "update-desktop-database /usr/share/applications 2>/dev/null || true",
              (char *)NULL);
        _exit(127);
    }
    if (child > 0)
        waitpid(child, &status, 0);

cleanup:
    if (mounted_packages)
        umount2(PACKAGE_TARGET, MNT_DETACH);
    if (mounted_sys)
        umount2(ROOTFS "/sys", MNT_DETACH);
    if (mounted_proc)
        umount2(ROOTFS "/proc", MNT_DETACH);
    if (mounted_dev)
        umount2(ROOTFS "/dev", MNT_DETACH);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

int main(void)
{
    int result;
    int saved_errno;
    int outcome = 0;

    out = open(STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    write(out, "uid=0\n", sizeof("uid=0\n") - 1);

    if (access(PACKAGE_COMPLETE, F_OK) == 0) {
        write(out, "debian-package-install=already-complete\n",
              sizeof("debian-package-install=already-complete\n") - 1);
    } else {
        result = install_debian_packages();
        saved_errno = errno;
        log_result("debian-package-install", result, saved_errno);
        if (result != 0)
            return 7;
        close(open(PACKAGE_COMPLETE, O_WRONLY | O_CREAT, 0644));
    }

    mkdir(ROOTFS "/usr/local/libexec", 0755);
    result = copy_file(STAGED_CAMERA_CAPTURE, TARGET_CAMERA_CAPTURE, 0755);
    saved_errno = errno;
    log_result("install-camera-capture", result, saved_errno);
    if (result != 0)
        return 8;
    result = copy_file(STAGED_CAMERA_APP, TARGET_CAMERA_APP, 0755);
    saved_errno = errno;
    log_result("install-camera-app", result, saved_errno);
    if (result != 0)
        return 9;

    result = mount(SYSTEM_DEVICE, "/system", "ext4",
                   MS_REMOUNT | MS_RELATIME, "seclabel,data=ordered");
    saved_errno = errno;
    log_result("remount-rw", result, saved_errno);
    if (result != 0)
        return 3;

    if (access(STOCK_SCRIPT, F_OK) != 0) {
        result = copy_file(TARGET_SCRIPT, STOCK_SCRIPT, 0544);
        saved_errno = errno;
        log_result("backup-stock-script", result, saved_errno);
        if (result != 0) {
            outcome = 4;
            goto remount_read_only;
        }
    } else {
        write(out, "backup-stock-script=already-present\n",
              sizeof("backup-stock-script=already-present\n") - 1);
    }

    result = copy_file(STAGED_BINARY, TARGET_BINARY, 0755);
    saved_errno = errno;
    log_result("install-launcher", result, saved_errno);
    if (result == 0) {
        result = copy_file(STAGED_SCRIPT, TARGET_SCRIPT, 0544);
        saved_errno = errno;
        log_result("install-hook", result, saved_errno);
        if (result == 0)
            sleep(5);
    }
    if (result != 0)
        outcome = 5;

remount_read_only:
    {
        int ro_result = mount(SYSTEM_DEVICE, "/system", "ext4",
                              MS_REMOUNT | MS_RDONLY | MS_RELATIME,
                              "seclabel,data=ordered");
        int ro_errno = errno;
        log_result("remount-ro", ro_result, ro_errno);
        if (ro_result != 0 && outcome == 0)
            outcome = 6;
    }
    write(out, "complete=1\n", sizeof("complete=1\n") - 1);
    fsync(out);
    close(out);
    return outcome;
}
