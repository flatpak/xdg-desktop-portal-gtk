#include <gtk/gtk.h>
#include "appchooserdialog.h"

static void
done_cb (GtkWidget *dialog,
         GAppInfo  *info,
         gpointer   data)
{
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

        gtk_init (NULL, NULL);

        apps = g_new (const char *, argc);
        for (i = 0; i + 1 < argc; i++)
                apps[i] = argv[i + 1];
        apps[argc - 1] = NULL;

        window = GTK_WIDGET (app_chooser_dialog_new (apps, NULL, "Cancel", "Select", "Open With", "A heading goes here"));
        g_signal_connect (window, "done", G_CALLBACK (done_cb), NULL);

        gtk_widget_show (window);

        gtk_main ();

        return 0;
}
