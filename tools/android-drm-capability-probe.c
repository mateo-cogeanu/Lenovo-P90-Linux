#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

static void print_cap(int fd, const char *name, unsigned long long capability)
{
    struct drm_get_cap cap;
    memset(&cap, 0, sizeof(cap));
    cap.capability = capability;
    if (ioctl(fd, DRM_IOCTL_GET_CAP, &cap) == 0)
        printf("cap.%s=%llu\n", name, cap.value);
    else
        printf("cap.%s=error:%d:%s\n", name, errno, strerror(errno));
}

static void set_client_cap(int fd, const char *name,
                           unsigned long long capability)
{
    struct drm_set_client_cap cap;
    memset(&cap, 0, sizeof(cap));
    cap.capability = capability;
    cap.value = 1;
    if (ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cap) == 0)
        printf("client-cap.%s=1\n", name);
    else
        printf("client-cap.%s=error:%d:%s\n", name, errno, strerror(errno));
}

int main(void)
{
    struct drm_version version;
    struct drm_mode_card_res resources;
    unsigned int *connectors = NULL, *crtcs = NULL, *encoders = NULL;
    char name[128], date[128], description[256];
    int fd, i;

    fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open card0");
        return 2;
    }
    memset(&version, 0, sizeof(version));
    memset(name, 0, sizeof(name));
    memset(date, 0, sizeof(date));
    memset(description, 0, sizeof(description));
    version.name = name;
    version.name_len = sizeof(name) - 1;
    version.date = date;
    version.date_len = sizeof(date) - 1;
    version.desc = description;
    version.desc_len = sizeof(description) - 1;
    if (ioctl(fd, DRM_IOCTL_VERSION, &version) == 0)
        printf("driver=%s version=%d.%d.%d date=%s description=%s\n",
               name, version.version_major, version.version_minor,
               version.version_patchlevel, date, description);

    print_cap(fd, "dumb-buffer", DRM_CAP_DUMB_BUFFER);
    print_cap(fd, "prime", DRM_CAP_PRIME);
    print_cap(fd, "timestamp-monotonic", DRM_CAP_TIMESTAMP_MONOTONIC);
#ifdef DRM_CAP_ADDFB2_MODIFIERS
    print_cap(fd, "addfb2-modifiers", DRM_CAP_ADDFB2_MODIFIERS);
#endif
    set_client_cap(fd, "universal-planes", DRM_CLIENT_CAP_UNIVERSAL_PLANES);
#ifdef DRM_CLIENT_CAP_ATOMIC
    set_client_cap(fd, "atomic", DRM_CLIENT_CAP_ATOMIC);
#endif

    memset(&resources, 0, sizeof(resources));
    if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &resources) != 0) {
        printf("resources=error:%d:%s\n", errno, strerror(errno));
        close(fd);
        return 3;
    }
    printf("resources connectors=%u crtcs=%u encoders=%u fbs=%u\n",
           resources.count_connectors, resources.count_crtcs,
           resources.count_encoders, resources.count_fbs);
    connectors = calloc(resources.count_connectors, sizeof(*connectors));
    crtcs = calloc(resources.count_crtcs, sizeof(*crtcs));
    encoders = calloc(resources.count_encoders, sizeof(*encoders));
    resources.connector_id_ptr = (unsigned long long)(unsigned long)connectors;
    resources.crtc_id_ptr = (unsigned long long)(unsigned long)crtcs;
    resources.encoder_id_ptr = (unsigned long long)(unsigned long)encoders;
    if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &resources) != 0) {
        printf("resource-ids=error:%d:%s\n", errno, strerror(errno));
        close(fd);
        return 4;
    }
    for (i = 0; i < (int)resources.count_crtcs; ++i)
        printf("crtc[%d]=%u\n", i, crtcs[i]);
    for (i = 0; i < (int)resources.count_encoders; ++i)
        printf("encoder[%d]=%u\n", i, encoders[i]);
    for (i = 0; i < (int)resources.count_connectors; ++i) {
        struct drm_mode_get_connector connector;
        memset(&connector, 0, sizeof(connector));
        connector.connector_id = connectors[i];
        if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &connector) == 0)
            printf("connector[%d]=%u type=%u connection=%u modes=%u encoder=%u\n",
                   i, connector.connector_id, connector.connector_type,
                   connector.connection, connector.count_modes,
                   connector.encoder_id);
        else
            printf("connector[%d]=error:%d:%s\n", i, errno, strerror(errno));
    }
    free(encoders);
    free(crtcs);
    free(connectors);
    close(fd);
    return 0;
}
