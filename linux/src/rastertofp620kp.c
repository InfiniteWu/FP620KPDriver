/*
 * rastertofp620kp.c - CUPS raster filter for the Jolimark FP-620K+
 *
 * The FP-620K+ is a 24-pin dot-matrix printer which implements the
 * ESC/P2K command set (an ESC/P2 superset).  The vendor's Windows GPD
 * drives it with the classic 24-pin block-graphics command
 *
 *     ESC *  mode  nL nH  <3 bytes per column>
 *
 * with mode 0x27 (39) for 180x180 dpi and mode 0x28 (40) for 360x180 dpi.
 * This filter emits the same commands.  It deliberately does not use the
 * ESC/P2 raster command (ESC .) found in some inkjet drivers, because the
 * vendor driver never issues it.
 *
 * Geometry
 * --------
 * CUPS gives us a raster which starts at the top-left corner of the PPD's
 * imageable area.  The print head, however, starts at the printer's
 * mechanical origin, which is a little inside the paper edge.  The two
 * constants below describe that origin relative to the paper edge.  They
 * were derived from Ghostscript's "epson" device output and then field
 * calibrated on the real printer with the paper guide at its 0 mark.
 *
 * The values can be overridden from the job options:
 *
 *     -o fp620k-origin-x=<points>
 *     -o fp620k-origin-y=<points>
 *     -o PrintDirection=Unidirectional|Bidirectional
 *     -o fp620k-unidirectional=0|1
 *     -o fp620k-output-y=180|360
 *
 * 360x180 is a high-quality mode: CUPS rasterizes at 360x360 and the filter
 * combines pairs of rows (OR) into 180 dpi.  Rendering CJK text directly at
 * 180 dpi makes Ghostscript drop many one-pixel horizontal strokes.
 *
 * Unidirectional printing is the default.  The printer's own "print
 * direction" setting defaults to AUTO, and neither this filter nor the
 * vendor GPD used to send ESC U, so the printer fell back to bidirectional
 * printing.  Its left-to-right and right-to-left registration differs by
 * about 1 mm, which makes vertical lines zigzag from band to band.  ESC U 01
 * forces every pass to run in the same direction; the PPD exposes this as
 * "Print Direction" with Quality (unidirectional) as the default.
 *
 * where <points> is the distance of the mechanical origin from the left /
 * top edge of the paper (1/72 inch).  Increase origin-x to move the image
 * left, increase origin-y to move the image up.
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

#include <cups/cups.h>
#include <cups/raster.h>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FILTER_NAME "rastertofp620kp"

/*
 * Mechanical origin of the print head, in points from the top-left corner
 * of the paper.  Calibrated for the FP-620K+:
 *
 *   - the verified Ghostscript output puts raster column 0 at head
 *     column -15 and raster row 0 at head row 61 (180 dpi);
 *   - the PPD imageable area starts at 18 pt / 36 pt.
 *
 * That gave origin_x = 18 + 15 * 72/180 = 24 pt, but a calibration page
 * printed 5 mm (14.2 pt) left of centre with the paper guide at 0, so the
 * real mechanical origin is:
 *
 *     origin_x = 24 - 14.2 = 9.8 pt
 *
 * origin_y = 36 - 61 * 72/180 = 11.6 pt looked correct on the same page.
 */
#define DEFAULT_ORIGIN_X_PT 9.8
#define DEFAULT_ORIGIN_Y_PT 11.6

/* Maximum printable width: the GPD sets an 84-column right margin at 10 cpi. */
#define MAX_PRINT_WIDTH_IN 8.4

struct resolution
{
  unsigned	xdpi;
  unsigned	ydpi;
  unsigned char	mode;
};

static const struct resolution resolutions[] =
{
  { 180, 180, 0x27 },		/* 24-pin, 180 dpi, mode 39 */
  { 360, 360, 0x28 },		/* 24-pin, 360x360 dpi, two-pass interlace */
  { 360, 180, 0x28 },		/* 24-pin, 360 dpi, mode 40 */
  { 120, 180, 0x21 },		/* 24-pin, 120 dpi, mode 33 */
  {  90, 180, 0x26 }		/* 24-pin,  90 dpi, mode 38 */
};

static double	origin_x_pt = DEFAULT_ORIGIN_X_PT;
static double	origin_y_pt = DEFAULT_ORIGIN_Y_PT;
static int	unidirectional = 1;
static int	verbose = 0;

/*
 * 'get_option()' - Return the value of a job option, or NULL.
 */

static const char *
get_option(const char *options, const char *name)
{
  static char	value[128];
  size_t	namelen = strlen(name);
  const char	*p = options;

  while (p && *p)
  {
    while (*p == ' ')
      p ++;

    if (!strncmp(p, name, namelen) && p[namelen] == '=')
    {
      const char	*start = p + namelen + 1;
      size_t		i = 0;

      while (*start && *start != ' ' && i < sizeof(value) - 1)
        value[i ++] = *start ++;

      value[i] = '\0';
      return (value);
    }

    p = strchr(p, ' ');
  }

  return (NULL);
}

/*
 * 'get_option_double()' - Return a floating-point job option.
 */

static double
get_option_double(const char *options, const char *name, double dflt)
{
  const char	*value = get_option(options, name);
  char		*end;
  double	v;

  if (!value)
    return (dflt);

  v = strtod(value, &end);
  if (end == value)
    return (dflt);

  return (v);
}

/*
 * 'find_resolution()' - Look up the ESC * mode for a raster resolution.
 */

static const struct resolution *
find_resolution(unsigned xdpi, unsigned ydpi)
{
  size_t	i;

  for (i = 0; i < sizeof(resolutions) / sizeof(resolutions[0]); i ++)
    if (resolutions[i].xdpi == xdpi && resolutions[i].ydpi == ydpi)
      return (resolutions + i);

  return (NULL);
}

/*
 * 'feed_dots()' - Advance the paper by a number of vertical dots.
 *
 * ESC J takes a count in 1/180 inch units (the GPD declares
 * *YMoveUnit: 180), so the count is also the number of 180 dpi rows.
 */

static void
feed_dots(FILE *out, long dots)
{
  while (dots > 0)
  {
    int n = dots > 255 ? 255 : (int)dots;

    fputc(0x1b, out);
    fputc('J', out);
    fputc(n, out);

    dots -= n;
  }
}

/*
 * 'feed_rows()' - Advance the paper by raster rows at the given resolution.
 *
 * At 180 dpi ESC J takes a count in 1/180 inch units.  The 360x360 mode
 * needs half that step for its two-pass interlace, so it uses ESC ( v,
 * which addresses 1/360 inch units (verified on the hardware).
 */

static void
feed_rows(FILE *out, long rows, unsigned ydpi)
{
  if (ydpi >= 360)
  {
    while (rows > 0)
    {
      int n = rows > 65535 ? 65535 : (int)rows;

      fputc(0x1b, out);
      fputc('(', out);
      fputc('v', out);
      fputc(2, out);
      fputc(0, out);
      fputc(n & 0xff, out);
      fputc((n >> 8) & 0xff, out);

      rows -= n;
    }
  }
  else
    feed_dots(out, rows);
}

/*
 * 'start_page()' - Emit the page initialization sequence.
 */

static void
start_page(FILE *out, const struct resolution *res)
{
  /*
   * ESC @     reset printer
   * ESC U 1   unidirectional printing (ESC U 0 for bidirectional)
   * ESC ( U   set positioning unit to 1/360 inch (360x360 mode only)
   * ESC P     select 10 cpi (matches the verified reference output)
   * ESC l 0   left margin = 0
   * CR        carriage return
   * ESC Q 84  right margin = 84 columns (8.4 inch)
   */

  fputs("\033@", out);
  fputc(0x1b, out);
  fputc('U', out);
  fputc(unidirectional ? 1 : 0, out);

  if (res->ydpi >= 360)
  {
    static const unsigned char unit360[] = { 0x1b, '(', 'U', 1, 0, 10 };

    fwrite(unit360, 1, sizeof(unit360), out);
  }

  fputs("\033P", out);
  fputc(0x1b, out);
  fputc('l', out);
  fputc(0, out);
  fputc('\r', out);
  fputc(0x1b, out);
  fputc('Q', out);
  fputc(84, out);
}

/*
 * 'end_page()' - Eject the page.
 */

static void
end_page(FILE *out)
{
  fputc('\r', out);
  fputc('\f', out);
  fputs("\033@", out);
}

/*
 * 'is_black()' - Return 1 if the given pixel is to be printed.
 *
 * CUPS bitmaps are packed MSB first.  For CUPS_CSPACE_K a set bit means
 * ink; for CUPS_CSPACE_W the sense is inverted.
 */

static int
is_black(const cups_page_header2_t *h, const unsigned char *row,
         unsigned column)
{
  int bit = (row[column >> 3] & (0x80 >> (column & 7))) != 0;

  if (h->cupsColorSpace == CUPS_CSPACE_K)
    return (bit);
  else
    return (!bit);
}

/*
 * 'print_page()' - Read one raster page and write the ESC/P2K data.
 */

static int
print_page(cups_raster_t *ras, const cups_page_header2_t *h,
           const char *options, FILE *out)
{
  const struct resolution *res;
  unsigned	width, height, bpl, max_cols, block_rows, passes, pass;
  unsigned	out_ydpi, combine, out_height;
  unsigned char	*row = NULL, *band = NULL, *data = NULL;
  unsigned	x, y, r, k, i, pin, src;
  long		start_col, indent, feed, pos;
  long		top_pt;
  int		ok = 0;

  /*
   * 360x180 is rendered by CUPS at 360x360 and then reduced vertically
   * by OR-ing pairs of source rows.  Ghostscript drops many one-pixel
   * horizontal strokes when it rasterizes CJK text directly at 180 dpi;
   * rendering at 360 dpi first and combining the rows keeps them.
   */

  out_ydpi = h->HWResolution[1];
  combine  = 1;

  {
    const char *output_y = get_option(options, "fp620k-output-y");

    if (output_y)
    {
      unsigned requested = (unsigned)atoi(output_y);

      if (requested == 180 || requested == 360)
        out_ydpi = requested;
    }
    else if (h->HWResolution[0] == 360 && h->HWResolution[1] == 360)
    {
      const char *resolution = get_option(options, "Resolution");

      if (resolution && !strcmp(resolution, "360x180dpi"))
      {
        out_ydpi = 180;
        combine  = 2;
      }
    }
  }

  if (out_ydpi == 180 && h->HWResolution[1] == 360)
    combine = 2;
  else if (out_ydpi != h->HWResolution[1])
  {
    fprintf(stderr, "ERROR: " FILTER_NAME ": cannot convert %u dpi to %u dpi\n",
            h->HWResolution[1], out_ydpi);
    return (0);
  }

  if ((res = find_resolution(h->HWResolution[0], out_ydpi)) == NULL)
  {
    fprintf(stderr, "ERROR: " FILTER_NAME ": unsupported resolution %ux%u\n",
            h->HWResolution[0], out_ydpi);
    return (0);
  }

  if (h->cupsBitsPerPixel != 1 && h->cupsBitsPerPixel != 8)
  {
    fprintf(stderr, "ERROR: " FILTER_NAME ": unsupported %u bits per pixel\n",
            h->cupsBitsPerPixel);
    return (0);
  }

  if (h->cupsColorSpace != CUPS_CSPACE_K &&
      h->cupsColorSpace != CUPS_CSPACE_W)
  {
    fprintf(stderr, "ERROR: " FILTER_NAME ": unsupported color space %d\n",
            h->cupsColorSpace);
    return (0);
  }

  width  = h->cupsWidth;
  height = h->cupsHeight;
  bpl    = h->cupsBytesPerLine;
  out_height = (height + combine - 1) / combine;

  if (width == 0 || height == 0 || bpl == 0)
  {
    fprintf(stderr, "ERROR: " FILTER_NAME ": empty page\n");
    return (0);
  }

  /*
   * Where does the imageable area start on the paper?  Margins[0] is the
   * left margin; the top margin follows from the page height.
   */

  top_pt = lround(h->cupsPageSize[1] -
                  (double)height * 72.0 / h->HWResolution[1] -
                  h->Margins[1]);
  if (top_pt < 0)
    top_pt = 0;

  /*
   * Convert the mechanical origin into a horizontal indent (positive) or
   * a number of source columns to clip (negative).
   */

  indent = lround((h->Margins[0] - origin_x_pt) * h->HWResolution[0] / 72.0);
  if (indent >= 0)
    start_col = 0;
  else
  {
    start_col = -indent;
    indent = 0;
  }

  feed = lround((top_pt - origin_y_pt) * out_ydpi / 72.0);
  if (feed < 0)
    feed = 0;

  max_cols = (unsigned)(MAX_PRINT_WIDTH_IN * h->HWResolution[0]);

  /*
   * 360x360 needs two physical passes: with a 24-pin head the odd source
   * rows can only be printed after a 1/360 inch paper step.  Every other
   * output resolution is a single pass of 24 rows.
   */

  block_rows = out_ydpi >= 360 ? 48 : 24;
  passes     = out_ydpi >= 360 ? 2 : 1;

  if (verbose)
    fprintf(stderr, "DEBUG: " FILTER_NAME ": page %ux%u px, in %ux%u dpi, "
            "out %ux%u dpi, combine %u, left margin %dpt, top margin %ldpt, "
            "indent %ld, clip %ld, feed %ld, block %u rows, %u pass(es)\n",
            width, height, h->HWResolution[0], h->HWResolution[1],
            h->HWResolution[0], out_ydpi, combine, h->Margins[0], top_pt,
            indent, start_col, feed, block_rows, passes);

  row  = malloc(bpl);
  band = calloc((size_t)block_rows * width, 1);
  data = malloc((size_t)3 * (width + (size_t)indent));

  if (!row || !band || !data)
  {
    fprintf(stderr, "ERROR: " FILTER_NAME ": out of memory\n");
    goto out;
  }

  start_page(out, res);

  feed_rows(out, feed, out_ydpi);
  pos = feed;

  for (y = 0; y < out_height; y += block_rows)
  {
    unsigned rows = out_height - y < block_rows ? out_height - y : block_rows;
    unsigned src_y = y * combine;
    unsigned last = start_col;
    int any = 0;

    memset(band, 0, (size_t)block_rows * width);

    for (r = 0; r < rows; r ++)
    {
      unsigned sr = src_y + r * combine;

      if (sr >= height)
        break;

      for (k = 0; k < combine && sr + k < height; k ++)
      {
        if (cupsRasterReadPixels(ras, row, bpl) != bpl)
        {
          fprintf(stderr, "ERROR: " FILTER_NAME ": short raster row\n");
          goto out;
        }

        for (x = start_col; x < width; x ++)
        {
          if (is_black(h, row, x))
          {
            band[(size_t)r * width + x] = 1;
            any = 1;
          }
        }
      }
    }

    if (!any)
      continue;

    /*
     * Find the last column that contains a dot, so trailing white space is
     * not sent to the printer.
     */

    for (x = start_col; x < width; x ++)
    {
      for (r = 0; r < block_rows; r ++)
        if (band[(size_t)r * width + x])
        {
          last = x;
          break;
        }
    }

    if (last < start_col)
      continue;

    /* Limit the line to the printer's carriage width. */

    if (indent + (long)(last - start_col + 1) > (long)max_cols)
    {
      if (max_cols <= (unsigned)indent)
        continue;

      last = start_col + (unsigned)(max_cols - indent) - 1;
    }

    for (pass = 0; pass < passes; pass ++)
    {
      unsigned char *dataptr = data;

      for (i = 0; i < (unsigned)indent; i ++)
      {
        *dataptr ++ = 0;
        *dataptr ++ = 0;
        *dataptr ++ = 0;
      }

      for (i = start_col; i <= last; i ++)
      {
        unsigned char b0 = 0, b1 = 0, b2 = 0;

        /*
         * In two-pass mode pin k carries source row 2k on the first pass
         * and row 2k+1 on the second one, so the two passes interleave
         * into a continuous 360 dpi column.
         */

        for (pin = 0; pin < 24; pin ++)
        {
          src = passes == 2 ? 2 * pin + pass : pin;

          if (src < rows && band[(size_t)src * width + i])
          {
            if (pin < 8)
              b0 |= (unsigned char)(0x80 >> pin);
            else if (pin < 16)
              b1 |= (unsigned char)(0x80 >> (pin - 8));
            else
              b2 |= (unsigned char)(0x80 >> (pin - 16));
          }
        }

        *dataptr ++ = b0;
        *dataptr ++ = b1;
        *dataptr ++ = b2;
      }

      {
        unsigned ncols = (unsigned)(dataptr - data) / 3;

        if (ncols == 0)
          continue;

        if (pass == 0)
        {
          feed_rows(out, (long)y + feed - pos, out_ydpi);
          pos = (long)y + feed;
        }
        else
        {
          feed_rows(out, 1, out_ydpi);
          pos += 1;
        }

        fputc(0x1b, out);
        fputc('*', out);
        fputc(res->mode, out);
        fputc(ncols & 0xff, out);
        fputc((ncols >> 8) & 0xff, out);
        fwrite(data, 3, ncols, out);
        fputc('\r', out);
      }
    }
  }

  end_page(out);
  ok = 1;

out:
  free(row);
  free(band);
  free(data);

  return (ok);
}

/*
 * 'main()' - Standard CUPS filter entry point.
 */

int
main(int argc, char *argv[])
{
  const char	*options = argc > 5 ? argv[5] : "";
  const char	*filename = argc > 6 ? argv[6] : NULL;
  int		fd = 0;
  cups_raster_t	*ras;
  cups_page_header2_t header;
  int		pages = 0;
  int		status = 0;

  if (getenv("CUPS_FILTER_VERBOSE"))
    verbose = 1;

  origin_x_pt = get_option_double(options, "fp620k-origin-x",
                                  DEFAULT_ORIGIN_X_PT);
  origin_y_pt = get_option_double(options, "fp620k-origin-y",
                                  DEFAULT_ORIGIN_Y_PT);

  {
    const char *direction = get_option(options, "PrintDirection");

    if (direction)
      unidirectional = strcmp(direction, "Bidirectional") != 0 &&
                       strcmp(direction, "0") != 0;
    else
      unidirectional = get_option_double(options, "fp620k-unidirectional",
                                         1.0) != 0.0;
  }

  if (filename && strcmp(filename, "-") != 0)
  {
    if ((fd = open(filename, O_RDONLY)) < 0)
    {
      fprintf(stderr, "ERROR: " FILTER_NAME ": cannot open %s: %s\n",
              filename, strerror(errno));
      return (1);
    }
  }

  if ((ras = cupsRasterOpen(fd, CUPS_RASTER_READ)) == NULL)
  {
    fprintf(stderr, "ERROR: " FILTER_NAME ": cannot read CUPS raster\n");
    if (fd != 0)
      close(fd);
    return (1);
  }

  setvbuf(stdout, NULL, _IOFBF, 65536);

  while (cupsRasterReadHeader2(ras, &header))
  {
    pages ++;

    if (!print_page(ras, &header, options, stdout))
    {
      status = 1;
      break;
    }
  }

  fflush(stdout);
  cupsRasterClose(ras);

  if (fd != 0)
    close(fd);

  if (verbose)
    fprintf(stderr, "DEBUG: " FILTER_NAME ": %d page(s), status %d\n",
            pages, status);

  return (status);
}
