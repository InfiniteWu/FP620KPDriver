/*
 * fp620kp.c - CUPS backend for the Jolimark FP-620K+
 *
 * The printer is a standard USB printer-class device handled by the Linux
 * "usblp" driver (/dev/usb/lp0).  This backend writes the job to the device
 * and polls LPGETSTATUS while doing so, translating the IEEE-1284 status
 * bits into CUPS printer-state-reasons.
 *
 * The device is selected with a device URI of the form
 *
 *     fp620kp:/dev/usb/lp0
 *
 * The status bit mapping follows <linux/lp.h>:
 *
 *     0x80  LP_PBUSY    printer busy
 *     0x40  LP_PACK     acknowledge
 *     0x20  LP_POUTPA   paper out
 *     0x10  LP_PSELECD  selected
 *     0x08  LP_PERRORP  no error (active low input, inverted by usblp)
 *
 * A healthy, idle FP-620K+ reports 0x18 (selected + no error, not busy).
 *
 * For manual testing the backend can also be run directly:
 *
 *     fp620kp --status [/dev/usb/lp0]    print one status sample
 *     fp620kp --monitor [/dev/usb/lp0]   print a sample every second
 *
 * Copyright (C) 2026
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/lp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BACKEND_NAME	"fp620kp"
#define DEFAULT_DEVICE	"/dev/usb/lp0"

static volatile sig_atomic_t	stop_monitor = 0;

/*
 * 'read_status()' - Read the 1284 status byte via LPGETSTATUS.
 */

static int
read_status(int fd)
{
  int status = -1;

  if (ioctl(fd, LPGETSTATUS, &status) < 0)
    return (-1);

  return (status & 0xff);
}

/*
 * 'status_text()' - Human-readable status for the log.
 */

static void
status_text(int status, char *buf, size_t bufsize)
{
  snprintf(buf, bufsize,
           "0x%02x%s%s%s%s%s%s", status,
           (status & LP_POUTPA) ? " paper-out" : "",
           (status & LP_PSELECD) ? " selected" : " not-selected",
           (status & LP_PERRORP) ? "" : " error",
           (status & LP_PBUSY) ? " busy" : " idle",
           (status & LP_PACK) ? " ack" : "",
           (status & 0x07) ? " bits0-2" : "");
}

/*
 * 'report_state()' - Send CUPS STATE messages when the status changes.
 *
 * The backend protocol is line based; "STATE: +reason" turns a reason on
 * and "STATE: -reason" turns it off.  Sending the full set every time the
 * status changes keeps the printer state in sync.
 */

static void
report_state(int status, int force)
{
  static int	last = -1;
  char		text[128];

  if (!force && status == last)
    return;

  last = status;
  status_text(status, text, sizeof(text));
  fprintf(stderr, "INFO: FP-620K+ status %s\n", text);

  if (status & LP_POUTPA)
    fprintf(stderr, "STATE: +media-empty\n");
  else
    fprintf(stderr, "STATE: -media-empty\n");

  if (!(status & LP_PSELECD))
    fprintf(stderr, "STATE: +offline\n");
  else
    fprintf(stderr, "STATE: -offline\n");

  /*
   * The FP-620K+ does not have a distinct cover bit in the 1284 status
   * byte, so an asserted error is reported as a generic device error
   * rather than guessing "cover open".  The reason is refined in
   * linux/README.md after the hardware calibration.
   */

  if (!(status & LP_PERRORP))
    fprintf(stderr, "STATE: +com.jolimark.fp620kp.error\n");
  else
    fprintf(stderr, "STATE: -com.jolimark.fp620kp.error\n");

  fflush(stderr);
}

/*
 * 'write_all()' - Write a buffer, retrying on EINTR.
 */

static int
write_all(int fd, const char *buf, size_t len)
{
  while (len > 0)
  {
    ssize_t n = write(fd, buf, len);

    if (n < 0)
    {
      if (errno == EINTR)
        continue;

      return (-1);
    }

    if (n == 0)
      return (-1);

    buf += n;
    len -= (size_t)n;
  }

  return (0);
}

/*
 * 'monitor_status()' - --status / --monitor helper.
 */

static void
on_signal(int sig)
{
  (void)sig;
  stop_monitor = 1;
}

static int
monitor_status(const char *device, int forever)
{
  int	fd;
  int	status;
  char	text[128];

  if ((fd = open(device, O_RDWR | O_NOCTTY)) < 0)
  {
    fprintf(stderr, "ERROR: " BACKEND_NAME ": cannot open %s: %s\n",
            device, strerror(errno));
    return (1);
  }

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  do
  {
    status = read_status(fd);

    if (status < 0)
    {
      fprintf(stderr, "ERROR: " BACKEND_NAME ": LPGETSTATUS: %s\n",
              strerror(errno));
      close(fd);
      return (1);
    }

    status_text(status, text, sizeof(text));
    printf("status=%s\n", text);
    fflush(stdout);

    if (forever && !stop_monitor)
      sleep(1);
  }
  while (forever && !stop_monitor);

  close(fd);
  return (0);
}

/*
 * 'main()' - CUPS backend entry point.
 */

int
main(int argc, char *argv[])
{
  const char	*uri;
  const char	*device = DEFAULT_DEVICE;
  const char	*filename;
  int		fd, in = 0;
  char		buf[16384];
  ssize_t	n;
  time_t	last_poll;
  int		status;
  int		wait_secs = 120;

  /*
   * Manual status mode, for installation and calibration.
   */

  if (argc > 1 && !strcmp(argv[1], "--status"))
    return (monitor_status(argc > 2 ? argv[2] : DEFAULT_DEVICE, 0));
  if (argc > 1 && !strcmp(argv[1], "--monitor"))
    return (monitor_status(argc > 2 ? argv[2] : DEFAULT_DEVICE, 1));

  /*
   * Find the device: DEVICE_URI is the authoritative source, argv[0] is a
   * fallback for manual runs ("fp620kp:/dev/usb/lp0").
   */

  uri = getenv("DEVICE_URI");
  if (!uri || strncmp(uri, BACKEND_NAME ":", strlen(BACKEND_NAME) + 1))
    uri = argv[0];

  if (uri && !strncmp(uri, BACKEND_NAME ":", strlen(BACKEND_NAME) + 1))
  {
    const char *p = uri + strlen(BACKEND_NAME) + 1;

    if (!strncmp(p, "//", 2))
      p += 2;
    if (*p == '/')
      device = p;
  }

  if (getenv("FP620K_DEVICE"))
    device = getenv("FP620K_DEVICE");

  if (getenv("FP620K_PAPER_WAIT"))
  {
    int v = atoi(getenv("FP620K_PAPER_WAIT"));

    if (v >= 0)
      wait_secs = v;
  }

  filename = argc > 6 ? argv[6] : NULL;

  if (filename && strcmp(filename, "-") != 0)
  {
    if ((in = open(filename, O_RDONLY)) < 0)
    {
      fprintf(stderr, "ERROR: " BACKEND_NAME ": cannot open %s: %s\n",
              filename, strerror(errno));
      return (1);
    }
  }

  if ((fd = open(device, O_RDWR | O_NOCTTY)) < 0)
  {
    fprintf(stderr, "ERROR: " BACKEND_NAME ": cannot open %s: %s\n",
            device, strerror(errno));
    fprintf(stderr, "STATE: +offline\n");
    if (in != 0)
      close(in);
    return (1);
  }

  /*
   * If the printer is out of paper, wait for the user to load it instead
   * of dropping the job on the floor.  CUPS shows the media-empty reason
   * while we wait.
   */

  status = read_status(fd);
  if (status >= 0)
    report_state(status, 1);

  if (status >= 0 && (status & LP_POUTPA) && wait_secs > 0)
  {
    int waited = 0;

    fprintf(stderr, "INFO: " BACKEND_NAME ": waiting for paper\n");

    while ((status & LP_POUTPA) && waited < wait_secs)
    {
      sleep(1);
      waited ++;
      status = read_status(fd);
      if (status < 0)
        break;
      report_state(status, 0);
    }

    if (status >= 0 && (status & LP_POUTPA))
    {
      fprintf(stderr, "ERROR: " BACKEND_NAME ": timed out waiting for paper\n");
      close(fd);
      if (in != 0)
        close(in);
      return (1);
    }
  }

  last_poll = time(NULL);

  while ((n = read(in, buf, sizeof(buf))) > 0)
  {
    if (write_all(fd, buf, (size_t)n) < 0)
    {
      fprintf(stderr, "ERROR: " BACKEND_NAME ": write failed: %s\n",
              strerror(errno));
      close(fd);
      if (in != 0)
        close(in);
      return (1);
    }

    if (time(NULL) != last_poll)
    {
      status = read_status(fd);
      if (status >= 0)
        report_state(status, 0);
      last_poll = time(NULL);
    }
  }

  if (n < 0)
  {
    fprintf(stderr, "ERROR: " BACKEND_NAME ": read failed: %s\n",
            strerror(errno));
    close(fd);
    if (in != 0)
      close(in);
    return (1);
  }

  /*
   * Give the printer a moment to drain its buffer so the final state is
   * meaningful.  This also keeps the backend alive long enough to report
   * a paper-out or cover-open condition that happens at the end of a job.
   */

  {
    int elapsed_ms = 0;

    while (elapsed_ms < 30000)
    {
      status = read_status(fd);
      if (status < 0)
        break;

      report_state(status, 0);

      if (!(status & LP_PBUSY) && (status & LP_PERRORP))
        break;

      usleep(200000);
      elapsed_ms += 200;
    }
  }

  close(fd);
  if (in != 0)
    close(in);

  return (0);
}
