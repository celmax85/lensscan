// SPDX-License-Identifier: GPL-3.0-or-later
// Bypasses uvcvideo to find out which alternate settings of the CZUR video
// streaming interface accept SET_INTERFACE, with and without a UVC commit,
// and whether isochronous data then flows. uvcvideo is re-attached at the end.
// Build: gcc -O2 -Wall -o tools/usbfs-alt-test.bin tools/usbfs-alt-test.c
// Usage: tools/usbfs-alt-test.bin /dev/bus/usb/BBB/DDD
#include <errno.h>
#include <fcntl.h>
#include <linux/usbdevice_fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int fd;
static const int kMaxPkt[7] = {0, 128, 256, 800, 2 * 800, 3 * 800, 3 * 1024};

static int ctrl(unsigned char rt, unsigned char req, unsigned short val, unsigned short idx,
                void *data, unsigned short len)
{
    struct usbdevfs_ctrltransfer c = {rt, req, val, idx, len, 1000, data};
    int r = ioctl(fd, USBDEVFS_CONTROL, &c);
    return r < 0 ? -errno : r;
}

static int setAlt(unsigned iface, unsigned alt)
{
    struct usbdevfs_setinterface s = {iface, alt};
    return ioctl(fd, USBDEVFS_SETINTERFACE, &s) < 0 ? -errno : 0;
}

static void detach(unsigned iface)
{
    struct usbdevfs_ioctl io = {(int)iface, USBDEVFS_DISCONNECT, NULL};
    if (ioctl(fd, USBDEVFS_IOCTL, &io) < 0 && errno != ENODATA)
        printf("detach if%u: %s\n", iface, strerror(errno));
    unsigned i = iface;
    if (ioctl(fd, USBDEVFS_CLAIMINTERFACE, &i) < 0)
        printf("claim if%u: %s\n", iface, strerror(errno));
}

static void attach(unsigned iface)
{
    unsigned i = iface;
    ioctl(fd, USBDEVFS_RELEASEINTERFACE, &i);
    struct usbdevfs_ioctl io = {(int)iface, USBDEVFS_CONNECT, NULL};
    if (ioctl(fd, USBDEVFS_IOCTL, &io) < 0)
        printf("reattach if%u: %s\n", iface, strerror(errno));
}

// UVC 1.0 probe/commit on the streaming interface (1). 26-byte control.
static int probeCommit(int format, int frame, unsigned interval, unsigned char out[26])
{
    unsigned char p[26] = {0};
    p[0] = 1; // bmHint: frame interval fixed
    p[2] = (unsigned char)format;
    p[3] = (unsigned char)frame;
    memcpy(p + 4, &interval, 4);
    int r = ctrl(0x21, 0x01, 0x0100, 1, p, 26); // SET_CUR PROBE
    if (r < 0) return r;
    r = ctrl(0xa1, 0x81, 0x0100, 1, out, 26); // GET_CUR PROBE
    if (r < 0) return r;
    return ctrl(0x21, 0x01, 0x0200, 1, out, 26); // SET_CUR COMMIT
}

// Submits isochronous URBs on EP 0x81 for ~1 s and counts payload bytes.
static long isoRead(int pktSize)
{
    enum { NURB = 4, NPKT = 32 };
    struct usbdevfs_urb *urbs[NURB];
    for (int u = 0; u < NURB; ++u) {
        urbs[u] = calloc(1, sizeof(struct usbdevfs_urb) + NPKT * sizeof(struct usbdevfs_iso_packet_desc));
        urbs[u]->type = USBDEVFS_URB_TYPE_ISO;
        urbs[u]->endpoint = 0x81;
        urbs[u]->flags = USBDEVFS_URB_ISO_ASAP;
        urbs[u]->buffer = malloc((size_t)pktSize * NPKT);
        urbs[u]->buffer_length = pktSize * NPKT;
        urbs[u]->number_of_packets = NPKT;
        for (int k = 0; k < NPKT; ++k)
            urbs[u]->iso_frame_desc[k].length = (unsigned)pktSize;
        if (ioctl(fd, USBDEVFS_SUBMITURB, urbs[u]) < 0) {
            printf("    submit iso: %s\n", strerror(errno));
            return -1;
        }
    }
    long bytes = 0;
    int errors = 0, reaped = 0;
    struct timespec t0, t;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &t);
        if ((t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000 > 1000)
            break;
        struct usbdevfs_urb *u = NULL;
        if (ioctl(fd, USBDEVFS_REAPURBNDELAY, &u) < 0) {
            usleep(2000);
            continue;
        }
        ++reaped;
        for (int k = 0; k < u->number_of_packets; ++k) {
            if (u->iso_frame_desc[k].status)
                ++errors;
            else if (u->iso_frame_desc[k].actual_length > 12) // UVC payload header
                bytes += u->iso_frame_desc[k].actual_length;
        }
        ioctl(fd, USBDEVFS_SUBMITURB, u);
    }
    for (int u = 0; u < NURB; ++u)
        ioctl(fd, USBDEVFS_DISCARDURB, urbs[u]);
    for (int i = 0; i < NURB; ++i) {
        struct usbdevfs_urb *u = NULL;
        ioctl(fd, USBDEVFS_REAPURB, &u);
    }
    printf("    iso: %d urbs reaped, %ld payload bytes, %d packet errors\n", reaped, bytes, errors);
    return bytes;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s /dev/bus/usb/BBB/DDD\n", argv[0]);
        return 2;
    }
    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror(argv[1]);
        return 1;
    }
    detach(0);
    detach(1);

    puts("A: SET_INTERFACE without commit");
    for (unsigned alt = 1; alt <= 6; ++alt) {
        int r = setAlt(1, alt);
        printf("  alt %u (%4d B): %s\n", alt, kMaxPkt[alt], r ? strerror(-r) : "OK");
        setAlt(1, 0);
        usleep(100000);
    }

    // bFormatIndex 2 = MJPEG on this camera; frame 1 = 3264x2448, frame 6 = 1280x960 (from GET_CUR traces).
    const int frames[] = {6, 1};
    for (unsigned fi = 0; fi < 2; ++fi) {
        unsigned char out[26];
        int r = probeCommit(2, frames[fi], 666666, out);
        unsigned payload;
        memcpy(&payload, out + 22, 4);
        printf("B%u: MJPEG frame %d commit: %s, dwMaxPayloadTransferSize %u\n", fi, frames[fi],
               r < 0 ? strerror(-r) : "OK", payload);
        for (unsigned alt = 1; alt <= 6; ++alt) {
            r = setAlt(1, alt);
            printf("  alt %u (%4d B): %s\n", alt, kMaxPkt[alt], r ? strerror(-r) : "OK");
            if (!r)
                isoRead(kMaxPkt[alt]);
            setAlt(1, 0);
            usleep(100000);
        }
    }

    attach(1);
    attach(0);
    close(fd);
    return 0;
}
