/*
 * pjl_probe - send a PJL status query to a USB printer via usblp and
 * print the raw response.
 *
 * Build: cc -O2 -D_GNU_SOURCE -o pjl_probe pjl_probe.c
 * Run:   sudo ./pjl_probe [/dev/usb/lp0]
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t timed_out;

static void on_alarm(int sig)
{
    (void)sig;
    timed_out = 1;
}

static const char query[] =
    "\x1b%-12345X"
    "@PJL USTATUS DEVICE = ON\r\n"
    "@PJL USTATUS JOB = ON\r\n"
    "@PJL INFO STATUS\r\n"
    "\x1b%-12345X";

static void dump(const unsigned char *buf, ssize_t r)
{
    printf("--- read %zd bytes ---\n", r);
    for (ssize_t j = 0; j < r; j++) {
        unsigned char c = buf[j];
        if (c >= 32 && c < 127)
            putchar(c);
        else if (c == '\r')
            fputs("\\r", stdout);
        else if (c == '\n')
            fputs("\\n\n", stdout);
        else
            printf("\\x%02x", c);
    }
    putchar('\n');
    fflush(stdout);
}

int main(int argc, char **argv)
{
    const char *dev = argc > 1 ? argv[1] : "/dev/usb/lp0";
    struct sigaction sa = { .sa_handler = on_alarm };

    sigaction(SIGALRM, &sa, NULL);

    int fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "open(%s): %s\n", dev, strerror(errno));
        return 1;
    }

    ssize_t n = write(fd, query, sizeof(query) - 1);
    if (n < 0) {
        fprintf(stderr, "write: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    fprintf(stderr, "sent %zd bytes\n", n);

    unsigned char buf[4096];
    int total = 0;
    for (int i = 0; i < 10; i++) {
        timed_out = 0;
        alarm(3);
        ssize_t r = read(fd, buf, sizeof(buf));
        alarm(0);
        if (r > 0) {
            dump(buf, r);
            total += (int)r;
            if (memmem(buf, (size_t)r, "\x0c", 1) != NULL)
                break;
        } else if (r < 0 && errno == EINTR && timed_out) {
            fprintf(stderr, "read timeout\n");
            break;
        } else if (r < 0) {
            fprintf(stderr, "read: %s\n", strerror(errno));
            break;
        } else {
            break;
        }
    }
    fprintf(stderr, "total %d bytes\n", total);
    close(fd);
    return total > 0 ? 0 : 2;
}
