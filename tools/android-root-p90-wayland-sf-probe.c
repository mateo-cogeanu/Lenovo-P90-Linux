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

#define ROOT "/data/local/tmp/p90-hybris"
#define RUNTIME_DIR "/data/local/tmp/p90-wayland-run"
#define LOG "/data/local/tmp/p90-wayland-sf-probe.txt"

static int out = -1;

static void log_text(const char *text)
{
    write(out, text, strlen(text));
    fsync(out);
}

int main(void)
{
    const char *library_path = ROOT "/lib:" ROOT "/runtime";
    pid_t child;
    int status = -1;
    int elapsed;
    char line[160];

    out = open(LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    log_text("uid=0\nprobe=p90-wayland-surfaceflinger\n");

    /* Reveal the untouched on-disk factory executable immediately. */
    snprintf(line, sizeof(line), "dumpstate-detach=%d errno=%d\n",
             umount2("/system/bin/dumpstate", MNT_DETACH), errno);
    log_text(line);

    mkdir(RUNTIME_DIR, 0700);
    chmod(RUNTIME_DIR, 0700);
    unlink(RUNTIME_DIR "/wayland-0");
    unlink(RUNTIME_DIR "/wayland-0.lock");

    child = fork();
    if (child == 0) {
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        setenv("XDG_RUNTIME_DIR", RUNTIME_DIR, 1);
        setenv("HYBRIS_EGLPLATFORM", "surfaceflinger", 1);
        setenv("HYBRIS_LINKER_DIR", ROOT "/lib/libhybris/linker", 1);
        setenv("HYBRIS_LD_LIBRARY_PATH",
               ROOT "/android-patched:/system/vendor/lib:/system/lib", 1);
        setenv("PATH", "/system/bin:/system/xbin", 1);
        execl(ROOT "/runtime/ld-linux.so.2", "ld-linux.so.2",
              "--library-path", library_path,
              ROOT "/bin/p90-wayland-sf-compositor",
              "wayland-0", "/dev/input/event2", (char *)NULL);
        _exit(127);
    }
    snprintf(line, sizeof(line), "compositor-pid=%d\n", child);
    log_text(line);

    for (elapsed = 0; elapsed < 200; ++elapsed) {
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child)
            break;
        usleep(100000);
    }
    if (elapsed == 200) {
        kill(child, SIGTERM);
        usleep(300000);
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
        log_text("probe-timeout=20\n");
    }
    snprintf(line, sizeof(line), "compositor-status=%d elapsed-ms=%d\n",
             status, elapsed * 100);
    log_text(line);
    close(out);
    return 0;
}
