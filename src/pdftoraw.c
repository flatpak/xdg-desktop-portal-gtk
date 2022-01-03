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

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PopplerDocument, g_object_unref);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PopplerPage, g_object_unref);

static gchar *filename = "";
static gboolean opt_test = FALSE;
static gboolean opt_pages = FALSE;
static int opt_raw = -1;
static int opt_width = -1;
static int opt_height = -1;

static GOptionEntry entries[] = {
  { "file", 'f', 0, G_OPTION_ARG_FILENAME, &filename, "PDF file path", NULL },
  { "test", 't', 0, G_OPTION_ARG_NONE, &opt_test, "Test if the file is a PDF file", NULL },
  { "pages", 'p', 0, G_OPTION_ARG_NONE, &opt_pages, "Returns the number of pages in the PDF file", NULL },
  { "width", 'w', 0, G_OPTION_ARG_INT, &opt_width, "Give the size of the page provided in arguments", NULL},
  { "height", 'W', 0, G_OPTION_ARG_INT, &opt_height, "Gives the height of the page provided in arguments", NULL},
  { "raw", 'r', 0, G_OPTION_ARG_INT, &opt_raw, "Retrieves data in pixels from the page provided as arguments", NULL},
  { NULL }
};

static int
pdf_number_pages_get (PopplerDocument *doc)
{
    return poppler_document_get_n_pages (doc);
}

static void
pdf_page_raw_get (PopplerDocument *doc,
                 int page_nr)
{
    cairo_surface_t *s = NULL;
    cairo_t *cr = NULL;
    unsigned char *data = NULL;
    g_autoptr (PopplerPage) page = NULL;
    g_autoptr (GdkPixbuf) pix = NULL;
    double width = 0;
    double height = 0;
    int w = 0;
    int h = 0;

    page = poppler_document_get_page (doc, page_nr);
    if (page == NULL)
    {
       g_error("failure poppler_document_get_page");
       return;
    }
    poppler_page_get_size (page, &width, &height);
    width = DPI * width / PTS;
    height = DPI * height / PTS;
    w = (int)ceil(width);
    h = (int)ceil(height);
    s = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, w, h);
    if (s == NULL)
    {
       g_error ("failure cairo_image_surface_create");
       return;
    }
    cr = cairo_create (s);
    if (cr == NULL)
    {
       cairo_surface_destroy (s);
       g_error ("failure cairo_create");
       return;
    }
    cairo_scale (cr, DPI / PTS, DPI / PTS);
    cairo_save (cr);
    poppler_page_render_for_printing (page, cr);
    cairo_restore (cr);
    pix = gdk_pixbuf_get_from_surface (s, 0, 0, w, h);
    cairo_surface_destroy (s);
    data = gdk_pixbuf_get_pixels (pix);
    fwrite (data, 1, (w * h * 4), stdout);
    cairo_destroy (cr);
}

static int
pdf_page_width_get (PopplerDocument *doc,
                   int page_nr)
{
    g_autoptr (PopplerPage) page = NULL;
    double width = 0;
    int w = 0;

    page = poppler_document_get_page (doc, page_nr);
    if (page == NULL)
       return -1;
    poppler_page_get_size (page, &width, NULL);
    width = DPI * width / PTS;
    w = (int)ceil (width);
    return w;
}

static int
pdf_page_height_get (PopplerDocument *doc,
                    int page_nr)
{
    g_autoptr (PopplerPage) page = NULL;
    double height = 0;
    int h = 0;

    page = poppler_document_get_page (doc, page_nr);
    if (page == NULL)
       return -1;
    poppler_page_get_size (page, NULL, &height);
    height = DPI * height / PTS;
    h = (int)ceil (height);
    return h;
}

int
main (int argc, char **argv)
{
    g_autoptr (PopplerDocument) doc = NULL;
    g_autoptr(GError) err = NULL;
    g_autoptr (GFile) in = NULL;
    g_autoptr (GOptionContext) context = NULL;
    g_autofree char *help = NULL;

    context = g_option_context_new ("- PDF utility for portal backends");
    g_option_context_add_main_entries (context, entries, NULL);
    if (!g_option_context_parse (context, &argc, &argv, &err))
    {
      g_printerr ("%s: %s", g_get_application_name (), err->message);
      g_printerr ("\n");
      g_printerr ("Try \"%s --help\" for more information.",
                  g_get_prgname ());
      g_printerr ("\n");
      exit (EXIT_FAILURE);
      return 1;
    }

    if (filename == NULL)
    {
	help = g_option_context_get_help (context, TRUE, NULL);
        g_printerr ("%s", help);
        exit (EXIT_FAILURE);
    }
    in = g_file_new_for_path (filename);
    doc = poppler_document_new_from_gfile (in, NULL, NULL, &err);
    if (err)
    {
	help = g_option_context_get_help (context, TRUE, NULL);
        g_printerr ("%s", help);
        exit (EXIT_FAILURE);
    }

    if (opt_test)
    {
        printf ("1");
    }
    else if (opt_raw > -1)
    {
        pdf_page_raw_get (doc, opt_raw);
    }
    else if (opt_pages)
    {
        printf ("%d", pdf_number_pages_get (doc));
    }
    else if (opt_width > -1)
    {
        printf ("%d", pdf_page_width_get (doc, opt_width));
    }
    else if (opt_height > -1)
    {
        printf ("%d", pdf_page_height_get (doc, opt_height));
    }
    else
    {
	help = g_option_context_get_help (context, TRUE, NULL);
        g_printerr ("%s", help);
        exit (EXIT_FAILURE);
    }
    return 0;
}

