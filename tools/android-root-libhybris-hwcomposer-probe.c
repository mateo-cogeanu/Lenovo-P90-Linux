#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

#define ROOT "/data/local/tmp/p90-hybris"
#define LOG "/data/local/tmp/p90-libhybris-hwcomposer-probe.txt"

static int out = -1;

static void log_line(const char *line)
{
    write(out, line, strlen(line));
    fsync(out);
}

static int set_service(const char *action, const char *service)
{
    pid_t child = fork();
    int status = -1;
    if (child == 0) {
        execl("/system/bin/setprop", "setprop", action, service, (char *)NULL);
        _exit(127);
    }
    if (child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

static void dump_maps(pid_t child)
{
    char path[64];
    char buffer[4096];
    ssize_t count;
    int fd;
    snprintf(path, sizeof(path), "/proc/%d/maps", child);
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return;
    log_line("process-maps-begin\n");
    while ((count = read(fd, buffer, sizeof(buffer))) > 0)
        write(out, buffer, count);
    log_line("process-maps-end\n");
    close(fd);
}

int main(void)
{
    const char *library_path =
        ROOT "/lib:" ROOT "/lib/libhybris:" ROOT "/runtime";
    pid_t child;
    int status = -1;
    int seconds;
    char line[128];

    out = open(LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    log_line("uid=0\nprobe=libhybris-hwcomposer\n");
    snprintf(line, sizeof(line), "surfaceflinger-stop=%d\n",
             set_service("ctl.stop", "surfaceflinger"));
    log_line(line);
    sleep(2);

    child = fork();
    if (child == 0) {
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);
        raise(SIGSTOP);
        setenv("HYBRIS_EGLPLATFORM", "hwcomposer", 1);
        setenv("HYBRIS_EGLPLATFORM_DIR", ROOT "/lib/libhybris", 1);
        setenv("HYBRIS_LINKER_DIR", ROOT "/lib/libhybris/linker", 1);
        setenv("HYBRIS_LD_LIBRARY_PATH",
               ROOT "/android-patched:/system/vendor/lib:/system/lib", 1);
        setenv("HYBRIS_LINKER_DEBUG", "1", 1);
        setenv("HYBRIS_LINKER_STDOUT", "1", 1);
        setenv("PATH", "/system/bin:/system/xbin", 1);
        execl(ROOT "/runtime/ld-linux.so.2", "ld-linux.so.2",
              "--library-path", library_path,
              ROOT "/bin/p90-libhybris-hwcomposer-tls-test", (char *)NULL);
        _exit(127);
    }
    snprintf(line, sizeof(line), "test-pid=%d\n", child);
    log_line(line);
    if (waitpid(child, &status, WUNTRACED) == child && WIFSTOPPED(status))
        ptrace(PTRACE_CONT, child, NULL, NULL);
    for (seconds = 0; seconds < 150; ++seconds) {
        pid_t result = waitpid(child, &status, WNOHANG | WUNTRACED);
        if (result == child) {
            if (WIFSTOPPED(status)) {
                int signal = WSTOPSIG(status);
                if (signal == SIGSEGV) {
                    struct user_regs_struct registers;
                    memset(&registers, 0, sizeof(registers));
                    ptrace(PTRACE_GETREGS, child, NULL, &registers);
                    snprintf(line, sizeof(line),
                             "sigsegv eip=%08lx esp=%08lx ebp=%08lx eax=%08lx\n",
                             registers.eip, registers.esp, registers.ebp,
                             registers.eax);
                    log_line(line);
                    dump_maps(child);
                    kill(child, SIGKILL);
                    waitpid(child, &status, 0);
                    break;
                }
                ptrace(PTRACE_CONT, child, NULL,
                       signal == SIGTRAP ? NULL : (void *)(long)signal);
            } else {
                break;
            }
        }
        usleep(100000);
    }
    if (seconds == 150) {
        kill(child, SIGTERM);
        sleep(1);
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
        log_line("test-timeout=15\n");
    }
    snprintf(line, sizeof(line), "test-status=%d\n", status);
    log_line(line);
    snprintf(line, sizeof(line), "surfaceflinger-start=%d\n",
             set_service("ctl.start", "surfaceflinger"));
    log_line(line);
    close(out);
    return 0;
}
