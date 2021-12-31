/*
 * Copyright (C) 2021 Thierry HUCHARD <thierry@ordissimo.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <gio/gio.h>
#include <poppler.h>
#include <gdk/gdk.h>


// 1 inch = 72 points
#define PTS 72.0

// Resolution for the printer, 150 my choice
#define DPI 150.0

static struct option long_options[] = {
    {"file",     required_argument, 0,  'f' },
    {"pages",    no_argument,       0,  'p' },
    {"test",     no_argument,       0,  't' },
    {"width",    required_argument, 0,  'w' },
    {"height",   required_argument, 0,  'W' },
    {"raw",      required_argument, 0,  'r' },
    {"help",     no_argument,       0,  'h' },
    {0,          0,                 0,  0   }
};

static void
print_usage (void)
{
    printf("Usage:\tpdftoraw  --filename=<filename>  --raw=<num-page>\n");
    printf("\tpdftoraw  --filename=<filename>  --test\n");
    printf("\tpdftoraw  --filename=<filename>  --pages\n");
    printf("\tpdftoraw  --filename=<filename>  --width=<num-page>\n");
    printf("\tpdftoraw  --filename=<filename>  --height=<num-page>\n");
    printf("\tpdftoraw  --help\n");
}

static int
pdf_number_pages_get (PopplerDocument *doc)
{
    return poppler_document_get_n_pages(doc);
}

static unsigned char*
pdf_page_raw_get (PopplerDocument *doc,
                 int page_nr)
{
    cairo_surface_t *s = NULL;
    cairo_t *cr = NULL;
    unsigned char *data = NULL;
    PopplerPage *page = NULL;
    GdkPixbuf *pix = NULL;
    double width = 0;
    double height = 0;
    int w = 0;
    int h = 0;

    page = poppler_document_get_page(doc, page_nr);
    poppler_page_get_size(page, &width, &height);
    width = DPI * width / PTS;
    height = DPI * height / PTS;
    w = (int)ceil(width);
    h = (int)ceil(height);
    s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cr = cairo_create(s);
    cairo_scale (cr, DPI / PTS, DPI / PTS);
    cairo_save (cr);
    poppler_page_render_for_printing(page, cr);
    cairo_restore (cr);
    pix = gdk_pixbuf_get_from_surface(s, 0, 0, w, h);
    g_object_unref(page);
    data = gdk_pixbuf_get_pixels (pix);
    fwrite(data, 1, (w * h * 4), stdout);
    return data;
}

static int
pdf_page_width_get (PopplerDocument *doc,
                   int page_nr)
{
    double width = 0;
    int w = 0;

    PopplerPage *page = poppler_document_get_page(doc, page_nr);
    poppler_page_get_size(page, &width, NULL);
    width = DPI * width / PTS;
    w = (int)ceil(width);
    g_object_unref(page);
    return w;
}

static int
pdf_page_height_get (PopplerDocument *doc,
                    int page_nr)
{
    double height = 0;
    int h = 0;

    PopplerPage *page = poppler_document_get_page(doc, page_nr);
    poppler_page_get_size(page, NULL, &height);
    height = DPI * height / PTS;
    h = (int)ceil(height);
    g_object_unref(page);
    return h;
}

int
main (int argc, char **argv)
{
    PopplerDocument *doc = NULL;
    GError *err = NULL;
    GFile *in = NULL;
    char *filename = NULL;
    int long_index =0;
    int page = 0;
    int opt= 0;
    int test = 0, raw = 0, npages = 0, width = 0, height = 0;

    while ((opt = getopt_long(argc, argv,"f:tpw:W:r:h",
                   long_options, &long_index )) != -1)
    {
        switch (opt)
       	{
             case 'f' :
                 filename = strdup(optarg);
                 break;
             case 't' :
                 test = 1;
                 break;
             case 'p' :
                 npages = 1;
                 break;
             case 'w' : page = atoi(optarg);
                 width = 1;
                 break;
             case 'W' : page = atoi(optarg);
                 height = 1;
                 break;
             case 'r' : page = atoi(optarg);
                 raw = 1;
                 break;
             default: print_usage();
                 exit(EXIT_FAILURE);
        }
    }
    if (filename == NULL)
    {
        print_usage();
        exit(EXIT_FAILURE);
    }
    in = g_file_new_for_path(filename);
    doc = poppler_document_new_from_gfile(in, NULL, NULL, &err);
    if (err)
    {
       g_error_free(err);
       g_object_unref(in);
       exit(EXIT_FAILURE);
    }

    if (test == 1)
    {
        printf("1");
    }
    else if (raw == 1)
    {
        pdf_page_raw_get(doc, page);
    }
    else if (npages == 1)
    {
        printf("%d", pdf_number_pages_get(doc));
    }
    else if (width == 1)
    {
        printf("%d", pdf_page_width_get(doc, page));
    }
    else if (height == 1)
    {
        printf("%d", pdf_page_height_get(doc, page));
    }
    else
    {
        print_usage();
        exit(EXIT_FAILURE);
    }
    g_object_unref(doc);
    return 0;
}

