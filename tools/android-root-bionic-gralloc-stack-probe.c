#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

#define LOG "/data/local/tmp/p90-libhybris-hwcomposer-probe.txt"
#define TEST "/data/local/tmp/p90-bionic-powervr-egl-tls-test"

static int out = -1;

static void log_text(const char *text)
{
    write(out, text, strlen(text));
    fsync(out);
}

static int service(const char *action, const char *name)
{
    pid_t child = fork();
    int status = -1;
    if (child == 0) {
        execl("/system/bin/setprop", "setprop", action, name, (char *)0);
        _exit(127);
    }
    if (child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

static void dump_stack(pid_t child, unsigned long esp)
{
    char line[128];
    unsigned long address;
    int i;
    log_text("stack-words-begin\n");
    for (i = 0; i < 48; ++i) {
        long value;
        errno = 0;
        address = esp + (unsigned long)i * sizeof(long);
        value = ptrace(PTRACE_PEEKDATA, child, (void *)address, 0);
        snprintf(line, sizeof(line), "%08lx: %08lx errno=%d\n",
                 address, (unsigned long)value, errno);
        log_text(line);
    }
    log_text("stack-words-end\n");
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
    log_text("process-maps-begin\n");
    while ((count = read(fd, buffer, sizeof(buffer))) > 0)
        write(out, buffer, count);
    close(fd);
    log_text("process-maps-end\n");
}

int main(void)
{
    pid_t child;
    int status = -1;
    int ticks;
    char line[192];

    out = open(LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0 || geteuid() != 0)
        return 2;
    fchmod(out, 0644);
    snprintf(line, sizeof(line), "uid=0\nsurfaceflinger-stop=%d\n",
             service("ctl.stop", "surfaceflinger"));
    log_text(line);
    sleep(2);

    child = fork();
    if (child == 0) {
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        raise(SIGSTOP);
        setenv("LD_PRELOAD", "/data/local/tmp/p90-wsbm-null-unmap-guard.so", 1);
        execl(TEST, "p90-bionic-gralloc-test", (char *)0);
        _exit(127);
    }
    snprintf(line, sizeof(line), "test-pid=%d\n", child);
    log_text(line);
    if (waitpid(child, &status, WUNTRACED) == child && WIFSTOPPED(status))
        ptrace(PTRACE_CONT, child, 0, 0);

    for (ticks = 0; ticks < 150; ++ticks) {
        pid_t result = waitpid(child, &status, WNOHANG | WUNTRACED);
        if (result == child) {
            if (WIFSTOPPED(status)) {
                int sig = WSTOPSIG(status);
                if (sig == SIGSEGV) {
                    struct user_regs_struct regs;
                    memset(&regs, 0, sizeof(regs));
                    ptrace(PTRACE_GETREGS, child, 0, &regs);
                    snprintf(line, sizeof(line),
                             "sigsegv eip=%08lx esp=%08lx ebp=%08lx "
                             "eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx "
                             "esi=%08lx edi=%08lx\n",
                             regs.eip, regs.esp, regs.ebp, regs.eax, regs.ebx,
                             regs.ecx, regs.edx, regs.esi, regs.edi);
                    log_text(line);
                    dump_stack(child, regs.esp);
                    dump_maps(child);
                    kill(child, SIGKILL);
                    waitpid(child, &status, 0);
                    break;
                }
                ptrace(PTRACE_CONT, child, 0,
                       sig == SIGTRAP ? 0 : (void *)(long)sig);
            } else {
                break;
            }
        }
        usleep(100000);
    }
    if (ticks == 150) {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
        log_text("test-timeout=15\n");
    }
    snprintf(line, sizeof(line), "test-status=%d\nsurfaceflinger-start=%d\n",
             status, service("ctl.start", "surfaceflinger"));
    log_text(line);
    close(out);
    return 0;
}
