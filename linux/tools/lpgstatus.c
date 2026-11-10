/*
 * lpgstatus - read the USB printer port status via usblp's LPGETSTATUS.
 *
 * Build: cc -O2 -o lpgstatus lpgstatus.c
 * Run:   sudo ./lpgstatus [/dev/usb/lp0]
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef LPGETSTATUS
#define LPGETSTATUS 0x060b
#endif

int main(int argc, char **argv)
{
    const char *dev = argc > 1 ? argv[1] : "/dev/usb/lp0";
    int fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "open(%s): %s\n", dev, strerror(errno));
        return 1;
    }

    int status = -1;
    if (ioctl(fd, LPGETSTATUS, &status) < 0) {
        fprintf(stderr, "LPGETSTATUS: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    printf("status=0x%02x\n", status & 0xff);
    printf("  paper_out=%d\n", !!(status & 0x01));
    printf("  selected=%d\n", !!(status & 0x02));
    printf("  error=%d\n", !!(status & 0x04));
    printf("  busy=%d\n", !!(status & 0x08));
    close(fd);
    return 0;
}
