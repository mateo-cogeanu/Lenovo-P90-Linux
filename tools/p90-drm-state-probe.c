#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void)
{
    struct drm_mode_card_res res;
    unsigned int fbs[64], crtcs[16], conns[32], encoders[32];
    int fd = open("/dev/dri/card0", O_RDWR);
    unsigned int i;

    if (fd < 0) {
        printf("open=-1 errno=%d (%s)\n", errno, strerror(errno));
        return 2;
    }
    memset(&res, 0, sizeof(res));
    res.fb_id_ptr = (unsigned long long)(unsigned long)fbs;
    res.crtc_id_ptr = (unsigned long long)(unsigned long)crtcs;
    res.connector_id_ptr = (unsigned long long)(unsigned long)conns;
    res.encoder_id_ptr = (unsigned long long)(unsigned long)encoders;
    res.count_fbs = 64;
    res.count_crtcs = 16;
    res.count_connectors = 32;
    res.count_encoders = 32;
    if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) {
        printf("getresources=-1 errno=%d (%s)\n", errno, strerror(errno));
        close(fd);
        return 3;
    }
    printf("fbs=%u crtcs=%u connectors=%u encoders=%u min=%ux%u max=%ux%u\n",
           res.count_fbs, res.count_crtcs, res.count_connectors,
           res.count_encoders, res.min_width, res.min_height,
           res.max_width, res.max_height);
    for (i = 0; i < res.count_fbs && i < 64; ++i) {
        struct drm_mode_fb_cmd fb;
        memset(&fb, 0, sizeof(fb));
        fb.fb_id = fbs[i];
        if (ioctl(fd, DRM_IOCTL_MODE_GETFB, &fb) == 0)
            printf("fb[%u]=%u %ux%u pitch=%u bpp=%u depth=%u handle=%u\n",
                   i, fb.fb_id, fb.width, fb.height, fb.pitch,
                   fb.bpp, fb.depth, fb.handle);
        else
            printf("fb[%u]=%u get-error=%d (%s)\n", i, fbs[i], errno,
                   strerror(errno));
    }
    for (i = 0; i < res.count_crtcs && i < 16; ++i) {
        struct drm_mode_crtc crtc;
        memset(&crtc, 0, sizeof(crtc));
        crtc.crtc_id = crtcs[i];
        if (ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &crtc) == 0)
            printf("crtc[%u]=%u fb=%u pos=%u,%u gamma=%u mode_valid=%u "
                   "mode=%s %ux%u\n", i, crtc.crtc_id, crtc.fb_id,
                   crtc.x, crtc.y, crtc.gamma_size, crtc.mode_valid,
                   crtc.mode.name, crtc.mode.hdisplay, crtc.mode.vdisplay);
        else
            printf("crtc[%u]=%u get-error=%d (%s)\n", i, crtcs[i], errno,
                   strerror(errno));
    }
    printf("scan-existing-fbs:\n");
    for (i = 1; i <= 128; ++i) {
        struct drm_mode_fb_cmd fb;
        memset(&fb, 0, sizeof(fb));
        fb.fb_id = i;
        if (ioctl(fd, DRM_IOCTL_MODE_GETFB, &fb) == 0)
            printf("fb-id=%u %ux%u pitch=%u bpp=%u depth=%u handle=%u\n",
                   fb.fb_id, fb.width, fb.height, fb.pitch,
                   fb.bpp, fb.depth, fb.handle);
    }
    for (i = 0; i < res.count_connectors && i < 32; ++i) {
        struct drm_mode_get_connector conn;
        struct drm_mode_modeinfo modes[32];
        unsigned int props[64], propvals[64], conn_encoders[16];
        memset(&conn, 0, sizeof(conn));
        conn.connector_id = conns[i];
        conn.modes_ptr = (unsigned long long)(unsigned long)modes;
        conn.props_ptr = (unsigned long long)(unsigned long)props;
        conn.prop_values_ptr = (unsigned long long)(unsigned long)propvals;
        conn.encoders_ptr = (unsigned long long)(unsigned long)conn_encoders;
        conn.count_modes = 32;
        conn.count_props = 64;
        conn.count_encoders = 16;
        if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) == 0)
            printf("connector[%u]=%u encoder=%u connection=%u type=%u "
                   "modes=%u first=%s %ux%u\n", i, conn.connector_id,
                   conn.encoder_id, conn.connection, conn.connector_type,
                   conn.count_modes, conn.count_modes ? modes[0].name : "",
                   conn.count_modes ? modes[0].hdisplay : 0,
                   conn.count_modes ? modes[0].vdisplay : 0);
        else
            printf("connector[%u]=%u get-error=%d (%s)\n", i, conns[i],
                   errno, strerror(errno));
    }
    close(fd);
    return 0;
}
