/*
 * Copyright © 2016 Red Hat, Inc
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
 *
 * Authors:
 *       Matthias Clasen <mclasen@redhat.com>
 */

#include "config.h"

#include "gtkbackports.h"

#if !GTK_CHECK_VERSION(3,22,0)

typedef enum {
  FILTER_RULE_PATTERN,
  FILTER_RULE_MIME_TYPE,
  FILTER_RULE_PIXBUF_FORMATS,
  FILTER_RULE_CUSTOM
} FilterRuleType;

struct _GtkFileFilter
{
  GInitiallyUnowned parent_instance;

  gchar *name;
  GSList *rules;

  GtkFileFilterFlags needed;
};

typedef struct _FilterRule FilterRule;

struct _FilterRule
{
  FilterRuleType type;
  GtkFileFilterFlags needed;
  
  union {
    gchar *pattern;
    gchar *mime_type;
    GSList *pixbuf_formats;
    struct {
      GtkFileFilterFunc func;
      gpointer data;
      GDestroyNotify notify;
    } custom;
  } u;
};

GVariant *
gtk_file_filter_to_gvariant (GtkFileFilter *filter)
{
  GVariantBuilder builder;
  GSList *l;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(us)"));
  for (l = filter->rules; l; l = l->next)
    {
      FilterRule *rule = l->data;

      switch (rule->type)
        {
        case FILTER_RULE_PATTERN:
          g_variant_builder_add (&builder, "(us)", 0, rule->u.mime_type);
          break;
        case FILTER_RULE_MIME_TYPE:
          g_variant_builder_add (&builder, "(us)", 1, rule->u.mime_type);
          break;
        case FILTER_RULE_PIXBUF_FORMATS:
          {
	    GSList *f;

	    for (f = rule->u.pixbuf_formats; f; f = f->next)
	      {
                GdkPixbufFormat *fmt = f->data;
                gchar **mime_types;
                int i;

                mime_types = gdk_pixbuf_format_get_mime_types (fmt);
                for (i = 0; mime_types[i]; i++)
                  g_variant_builder_add (&builder, "(us)", 1, mime_types[i]);
                g_strfreev (mime_types);
              }
          }
          break;
        case FILTER_RULE_CUSTOM:
        default:
          break;
        }
    }

  return g_variant_new ("(s@a(us))", filter->name, g_variant_builder_end (&builder));
}

GtkFileFilter *
gtk_file_filter_new_from_gvariant (GVariant *variant)
{
  GtkFileFilter *filter;
  GVariantIter *iter;
  const char *name;
  int type;
  char *tmp;

  filter = gtk_file_filter_new ();

  g_variant_get (variant, "(&sa(us))", &name, &iter);

  gtk_file_filter_set_name (filter, name);

  while (g_variant_iter_next (iter, "(u&s)", &type, &tmp))
    {
      switch (type)
        {
        case 0:
          gtk_file_filter_add_pattern (filter, tmp);
          break;
        case 1:
          gtk_file_filter_add_mime_type (filter, tmp);
          break;
        default:
          break;
       }
    }
  g_variant_iter_free (iter);

  return filter;
}


static void
add_to_variant (const gchar *key,
                const gchar *value,
                gpointer     data)
{
  GVariantBuilder *builder = data;
  g_variant_builder_add (builder, "{sv}", key, g_variant_new_string (value));
}

GVariant *
gtk_print_settings_to_gvariant (GtkPrintSettings *settings)
{
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  gtk_print_settings_foreach (settings, add_to_variant, &builder);

  return g_variant_builder_end (&builder);
}

GtkPrintSettings *
gtk_print_settings_new_from_gvariant (GVariant *variant)
{
  GtkPrintSettings *settings;
  int i;

  g_return_val_if_fail (g_variant_is_of_type (variant, G_VARIANT_TYPE_VARDICT), NULL);

  settings = gtk_print_settings_new ();

  for (i = 0; i < g_variant_n_children (variant); i++)
    {
      const char *key;
      GVariant *v;

      g_variant_get_child (variant, i, "{&sv}", &key, &v);
      if (g_variant_is_of_type (v, G_VARIANT_TYPE_STRING))
        {
          const char *value;
          g_variant_get (v, "&s", &value);
          gtk_print_settings_set (settings, key, value);
        }
      g_variant_unref (v);
    }

  return settings;
}

#define MM_PER_INCH 25.4
#define POINTS_PER_INCH 72

gdouble
_gtk_print_convert_from_mm (gdouble len, 
                            GtkUnit unit)
{
  switch (unit)
    {
    case GTK_UNIT_MM:
      return len;
    case GTK_UNIT_INCH:
      return len / MM_PER_INCH;
    default:
      g_warning ("Unsupported unit");
      /* Fall through */
    case GTK_UNIT_POINTS:
      return len / (MM_PER_INCH / POINTS_PER_INCH);
      break;
    }
}

GVariant *
gtk_paper_size_to_gvariant (GtkPaperSize *paper_size)
{
  const char *name;
  const char *ppd_name;
  const char *display_name;
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);

  name = gtk_paper_size_get_name (paper_size);
  ppd_name = gtk_paper_size_get_ppd_name (paper_size);
  display_name = gtk_paper_size_get_display_name (paper_size);

  if (ppd_name != NULL)
    g_variant_builder_add (&builder, "{sv}", "PPDName", g_variant_new_string (ppd_name));
  else
    g_variant_builder_add (&builder, "{sv}", "Name", g_variant_new_string (name));

  if (display_name != NULL)
    g_variant_builder_add (&builder, "{sv}", "DisplayName", g_variant_new_string (display_name));

  g_variant_builder_add (&builder, "{sv}", "Width", g_variant_new_double (gtk_paper_size_get_width (paper_size, GTK_UNIT_MM)));
  g_variant_builder_add (&builder, "{sv}", "Height", g_variant_new_double (gtk_paper_size_get_height (paper_size, GTK_UNIT_MM)));

  return g_variant_builder_end (&builder);
}

GtkPaperSize *
gtk_paper_size_new_from_gvariant (GVariant *variant)
{
  GtkPaperSize *paper_size;
  const char *name;
  const char *ppd_name;
  const char *display_name;
  gdouble width, height;

  g_return_val_if_fail (g_variant_is_of_type (variant, G_VARIANT_TYPE_VARDICT), NULL);

  if (!g_variant_lookup (variant, "Width", "d", &width) ||
      !g_variant_lookup (variant, "Height", "d", &height))
    return NULL;

  if (!g_variant_lookup (variant, "Name", "&s", &name))
    name = NULL;

  if (!g_variant_lookup (variant, "PPDName", "&s", &ppd_name))
    ppd_name = NULL;

  if (!g_variant_lookup (variant, "DisplayName", "&s", &display_name))
    display_name = name;

  if (ppd_name != NULL)
    paper_size = gtk_paper_size_new_from_ppd (ppd_name,
                                              display_name,
                                              _gtk_print_convert_from_mm (width, GTK_UNIT_POINTS),
                                              _gtk_print_convert_from_mm (height, GTK_UNIT_POINTS));
  else if (name != NULL)
    paper_size = gtk_paper_size_new_custom (name, display_name,
                                            width, height, GTK_UNIT_MM);
  else
    paper_size = NULL;

  return paper_size;
}

static char *
enum_to_string (GType type,
                guint enum_value)
{
  GEnumClass *enum_class;
  GEnumValue *value;
  char *retval = NULL;

  enum_class = g_type_class_ref (type);

  value = g_enum_get_value (enum_class, enum_value);
  if (value)
    retval = g_strdup (value->value_nick);

  g_type_class_unref (enum_class);

  return retval;
}

static guint
string_to_enum (GType type,
                const char *enum_string)
{
  GEnumClass *enum_class;
  const GEnumValue *value;
  guint retval = 0;

  g_return_val_if_fail (enum_string != NULL, 0);

  enum_class = g_type_class_ref (type);
  value = g_enum_get_value_by_nick (enum_class, enum_string);
  if (value)
    retval = value->value;

  g_type_class_unref (enum_class);

  return retval;
}

GVariant *
gtk_page_setup_to_gvariant (GtkPageSetup *setup)
{
  GtkPaperSize *paper_size;
  GVariant *variant;
  int i;
  GVariantBuilder builder;
  char *orientation;

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);

  paper_size = gtk_page_setup_get_paper_size (setup);

  variant = g_variant_ref_sink (gtk_paper_size_to_gvariant (paper_size));
  for (i = 0; i < g_variant_n_children (variant); i++)
    g_variant_builder_add_value (&builder, g_variant_get_child_value (variant, i));
  g_variant_unref (variant);

  g_variant_builder_add (&builder, "{sv}", "MarginTop", g_variant_new_double (gtk_page_setup_get_top_margin (setup, GTK_UNIT_MM)));
  g_variant_builder_add (&builder, "{sv}", "MarginBottom", g_variant_new_double (gtk_page_setup_get_bottom_margin (setup, GTK_UNIT_MM)));
  g_variant_builder_add (&builder, "{sv}", "MarginLeft", g_variant_new_double (gtk_page_setup_get_left_margin (setup, GTK_UNIT_MM)));
  g_variant_builder_add (&builder, "{sv}", "MarginRight", g_variant_new_double (gtk_page_setup_get_right_margin (setup, GTK_UNIT_MM)));

  orientation = enum_to_string (GTK_TYPE_PAGE_ORIENTATION,
                                gtk_page_setup_get_orientation (setup));
  g_variant_builder_add (&builder, "{sv}", "Orientation", g_variant_new_take_string (orientation));

  return g_variant_builder_end (&builder);
}

GtkPageSetup *
gtk_page_setup_new_from_gvariant (GVariant *variant)
{
  GtkPageSetup *setup;
  const char *orientation;
  gdouble margin;
  GtkPaperSize *paper_size;

  g_return_val_if_fail (g_variant_is_of_type (variant, G_VARIANT_TYPE_VARDICT), NULL);

  setup = gtk_page_setup_new ();

  paper_size = gtk_paper_size_new_from_gvariant (variant);
  if (paper_size)
    {
      gtk_page_setup_set_paper_size (setup, paper_size);
      gtk_paper_size_free (paper_size);
    }

  if (g_variant_lookup (variant, "MarginTop", "d", &margin))
    gtk_page_setup_set_top_margin (setup, margin, GTK_UNIT_MM);
  if (g_variant_lookup (variant, "MarginBottom", "d", &margin))
    gtk_page_setup_set_bottom_margin (setup, margin, GTK_UNIT_MM);
  if (g_variant_lookup (variant, "MarginLeft", "d", &margin))
    gtk_page_setup_set_left_margin (setup, margin, GTK_UNIT_MM);
  if (g_variant_lookup (variant, "MarginRight", "d", &margin))
    gtk_page_setup_set_right_margin (setup, margin, GTK_UNIT_MM);

  if (g_variant_lookup (variant, "Orientation", "&s", &orientation))
    gtk_page_setup_set_orientation (setup, string_to_enum (GTK_TYPE_PAGE_ORIENTATION,
                                                           orientation));

  return setup;
}

struct _GtkPrintJobPrivate
{
  gchar *title;

  GIOChannel *spool_io;
  cairo_surface_t *surface;

  GtkPrintStatus status;
  GtkPrintBackend *backend;
  GtkPrinter *printer;
  GtkPrintSettings *settings;
  GtkPageSetup *page_setup;

  GtkPrintPages print_pages;
  GtkPageRange *page_ranges;
  gint num_page_ranges;
  GtkPageSet page_set;
  gint num_copies;
  gdouble scale;
  guint number_up;
  GtkNumberUpLayout number_up_layout;

  guint printer_set           : 1;
  guint page_setup_set        : 1;
  guint settings_set          : 1;
  guint track_print_status    : 1;
  guint rotate_to_orientation : 1;
  guint collate               : 1;
  guint reverse               : 1;
};

gboolean
gtk_print_job_set_source_fd (GtkPrintJob  *job,
                             int           fd,
                             GError      **error)
{
  g_return_val_if_fail (GTK_IS_PRINT_JOB (job), FALSE);

  job->priv->spool_io = g_io_channel_unix_new (fd);
  if (g_io_channel_set_encoding (job->priv->spool_io, NULL, error) != G_IO_STATUS_NORMAL)
    return FALSE;

  return TRUE;
}

#endif
