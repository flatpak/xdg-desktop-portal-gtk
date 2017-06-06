#include <gtk/gtk.h>
#include "appchooserdialog.h"

static void
close_cb (AppChooserDialog *dialog,
          gpointer   data)
{
        GAppInfo *info;

        info = app_chooser_dialog_get_info (dialog);
        if (info)
                g_print ("%s\n", g_app_info_get_id (info));
        else
                g_print ("canceled\n");

        gtk_main_quit ();
}


int
main (int argc, char *argv[])
{
        GtkWidget *window;
        const char **apps;
        int i;
        const char *default_id = NULL;
        const char *content_type = NULL;
        const char *location = NULL;
        GOptionEntry entries[] = {
          { "default", 0, 0, G_OPTION_ARG_STRING, &default_id, "The default choice", "ID" },
          { "content-type", 0, 0, G_OPTION_ARG_STRING, &content_type, "The content type", "TYPE" },
          { "location", 0, 0, G_OPTION_ARG_STRING, &location, "The location (file or uri)", "LOCATION" },
          { NULL, }
        };

        gtk_init_with_args (&argc, &argv, "APP...", entries, NULL, NULL);

        apps = g_new (const char *, argc);
        for (i = 0; i + 1 < argc; i++)
                apps[i] = argv[i + 1];
        apps[argc - 1] = NULL;

        window = GTK_WIDGET (app_chooser_dialog_new (apps, default_id, content_type, location));
        g_signal_connect (window, "close", G_CALLBACK (close_cb), NULL);

        gtk_widget_show (window);

        gtk_main ();

        return 0;
}
