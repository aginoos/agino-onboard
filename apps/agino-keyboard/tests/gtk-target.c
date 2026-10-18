/* Test fixture only: exposes the contents of its own entry in private DBus sessions. */
#include <gtk/gtk.h>
static GtkWidget *window,*entry;
static GDBusNodeInfo *node;
static gchar *preedit;
static void preedit_changed(GtkEntry *entry,const gchar *text,gpointer unused){g_free(preedit);preedit=g_strdup(text);}
static const char xml[]="<node><interface name='org.agino.KeyboardTestTarget1'><method name='GetPreedit'><arg type='s' direction='out'/></method><method name='GetText'><arg type='s' direction='out'/></method><method name='Sensitive'><arg type='b' direction='in'/></method><method name='Focus'/><method name='Hide'/></interface></node>";
static void call(GDBusConnection *c,const gchar *sender,const gchar *path,const gchar *interface,const gchar *method,GVariant *args,GDBusMethodInvocation *reply,gpointer data){
 if(!g_strcmp0(method,"GetPreedit"))g_dbus_method_invocation_return_value(reply,g_variant_new("(s)",preedit?preedit:""));
 else if(!g_strcmp0(method,"GetText"))g_dbus_method_invocation_return_value(reply,g_variant_new("(s)",gtk_entry_get_text(GTK_ENTRY(entry))));
 else{if(!g_strcmp0(method,"Sensitive")){gboolean v;g_variant_get(args,"(b)",&v);gtk_entry_set_input_purpose(GTK_ENTRY(entry),v?GTK_INPUT_PURPOSE_PASSWORD:GTK_INPUT_PURPOSE_FREE_FORM);gtk_entry_set_visibility(GTK_ENTRY(entry),!v);gtk_entry_set_text(GTK_ENTRY(entry),"");gtk_widget_grab_focus(entry);}else if(!g_strcmp0(method,"Focus")){gtk_window_present(GTK_WINDOW(window));gdk_window_focus(gtk_widget_get_window(window),GDK_CURRENT_TIME);gtk_widget_grab_focus(entry);}else if(!g_strcmp0(method,"Hide"))gtk_widget_hide(window);g_dbus_method_invocation_return_value(reply,NULL);}
}
static const GDBusInterfaceVTable table={call,NULL,NULL};
static void acquired(GDBusConnection *connection,const gchar *name,gpointer data){g_dbus_connection_register_object(connection,"/org/agino/TestTarget",node->interfaces[0],&table,NULL,NULL,NULL);}
int main(int argc,char **argv){gtk_init(&argc,&argv);node=g_dbus_node_info_new_for_xml(xml,NULL);window=gtk_window_new(GTK_WINDOW_TOPLEVEL);entry=gtk_entry_new();g_signal_connect(entry,"preedit-changed",G_CALLBACK(preedit_changed),NULL);gtk_container_add(GTK_CONTAINER(window),entry);gtk_window_set_title(GTK_WINDOW(window),"Agino GTK input test");gtk_window_set_default_size(GTK_WINDOW(window),500,100);gtk_widget_show_all(window);gtk_widget_grab_focus(entry);g_signal_connect(window,"destroy",G_CALLBACK(gtk_main_quit),NULL);g_bus_own_name(G_BUS_TYPE_SESSION,"org.agino.KeyboardTestTarget1",G_BUS_NAME_OWNER_FLAGS_NONE,acquired,NULL,NULL,NULL,NULL);gtk_main();return 0;}
