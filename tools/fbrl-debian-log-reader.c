#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define FALLBACK_ADBD "/tmp/p90-original-adbd"

int main(void)
{
    int watchdog = open("/sys/class/misc/watchdog/disable", O_WRONLY);
    char *argv[] = {FALLBACK_ADBD, NULL};

    if (watchdog >= 0) {
        write(watchdog, "0\n", 2);
        close(watchdog);
    }
    chmod("/data/local/tmp/p90-debian-install.txt", 0644);
    chmod("/data/local/tmp/p90-debian-state.txt", 0644);
    chmod(FALLBACK_ADBD, 0755);
    execv(FALLBACK_ADBD, argv);
    return 127;
}
