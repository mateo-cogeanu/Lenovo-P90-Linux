#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define LOG "/data/local/tmp/p90-input-owner-probe.txt"

int main(void)
{
    DIR *proc;
    struct dirent *process;
    int out = open(LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int touch, grab;
    if (out < 0 || geteuid() != 0) return 2;
    fchmod(out, 0644);
    proc = opendir("/proc");
    while (proc && (process = readdir(proc))) {
        pid_t pid = atoi(process->d_name);
        char fd_dir_path[64], cmd_path[64], command[256] = {0};
        DIR *fd_dir;
        struct dirent *descriptor;
        int cmd_fd, command_length;
        if (pid <= 0) continue;
        snprintf(fd_dir_path, sizeof(fd_dir_path), "/proc/%d/fd", pid);
        fd_dir = opendir(fd_dir_path);
        if (!fd_dir) continue;
        snprintf(cmd_path, sizeof(cmd_path), "/proc/%d/cmdline", pid);
        cmd_fd = open(cmd_path, O_RDONLY);
        command_length = cmd_fd >= 0 ? read(cmd_fd, command, sizeof(command)-1) : 0;
        if (cmd_fd >= 0) close(cmd_fd);
        if (command_length > 0) {
            int i;
            for (i = 0; i < command_length - 1; ++i)
                if (!command[i]) command[i] = ' ';
        }
        while ((descriptor = readdir(fd_dir))) {
            char link_path[96], target[256], line[640];
            int length;
            snprintf(link_path, sizeof(link_path), "/proc/%d/fd/%s",
                     pid, descriptor->d_name);
            length = readlink(link_path, target, sizeof(target)-1);
            if (length <= 0) continue;
            target[length] = '\0';
            if (strcmp(target, "/dev/input/event2")) continue;
            length = snprintf(line, sizeof(line), "owner-pid=%d fd=%s cmd=%s\n",
                              pid, descriptor->d_name, command);
            write(out, line, length);
        }
        closedir(fd_dir);
    }
    if (proc) closedir(proc);
    touch = open("/dev/input/event2", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    grab = touch >= 0 ? ioctl(touch, EVIOCGRAB, 1) : -2;
    dprintf(out, "probe-open=%d grab=%d\n", touch >= 0, grab);
    if (grab == 0) ioctl(touch, EVIOCGRAB, 0);
    if (touch >= 0) close(touch);
    fsync(out);
    close(out);
    return 0;
}
