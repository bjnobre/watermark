/*
 * whatermak - a click-through image watermark for wlroots compositors
 * Copyright (C) 2026 whatermak contributors
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <gtk/gtk.h>
#include <gtk-layer-shell.h>

typedef enum {
  POSITION_TOP_LEFT,
  POSITION_TOP,
  POSITION_TOP_RIGHT,
  POSITION_LEFT,
  POSITION_CENTER,
  POSITION_RIGHT,
  POSITION_BOTTOM_LEFT,
  POSITION_BOTTOM,
  POSITION_BOTTOM_RIGHT,
} Position;

typedef struct {
  gchar *image_path;
  gchar *output;
  gchar *position_name;
  gchar *layer_name;
  Position position;
  GtkLayerShellLayer layer;
  gint width;
  gint margin;
  gdouble opacity;
  gboolean all_outputs;
  gboolean version;
} Options;

static const gchar *VERSION = "0.1.0";
static Options options = {
    .position_name = NULL,
    .layer_name = NULL,
    .position = POSITION_BOTTOM_RIGHT,
    .layer = GTK_LAYER_SHELL_LAYER_OVERLAY,
    .width = 240,
    .margin = 32,
    .opacity = 0.22,
    .all_outputs = TRUE,
};
static GPtrArray *windows;

static void
install_transparent_style(void)
{
  static const gchar css[] = "window.whatermak { background: transparent; }";
  g_autoptr(GtkCssProvider) provider = gtk_css_provider_new();

  gtk_css_provider_load_from_data(provider, css, -1, NULL);
  gtk_style_context_add_provider_for_screen(
      gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static gboolean
parse_position(const gchar *name, Position *position)
{
  static const struct {
    const gchar *name;
    Position position;
  } positions[] = {
      {"top-left", POSITION_TOP_LEFT},
      {"top", POSITION_TOP},
      {"top-right", POSITION_TOP_RIGHT},
      {"left", POSITION_LEFT},
      {"center", POSITION_CENTER},
      {"right", POSITION_RIGHT},
      {"bottom-left", POSITION_BOTTOM_LEFT},
      {"bottom", POSITION_BOTTOM},
      {"bottom-right", POSITION_BOTTOM_RIGHT},
  };

  for (guint i = 0; i < G_N_ELEMENTS(positions); i++) {
    if (g_str_equal(name, positions[i].name)) {
      *position = positions[i].position;
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean
parse_layer(const gchar *name, GtkLayerShellLayer *layer)
{
  static const struct {
    const gchar *name;
    GtkLayerShellLayer layer;
  } layers[] = {
      {"background", GTK_LAYER_SHELL_LAYER_BACKGROUND},
      {"bottom", GTK_LAYER_SHELL_LAYER_BOTTOM},
      {"top", GTK_LAYER_SHELL_LAYER_TOP},
      {"overlay", GTK_LAYER_SHELL_LAYER_OVERLAY},
  };

  for (guint i = 0; i < G_N_ELEMENTS(layers); i++) {
    if (g_str_equal(name, layers[i].name)) {
      *layer = layers[i].layer;
      return TRUE;
    }
  }
  return FALSE;
}

static gchar *
monitor_name(GdkMonitor *monitor)
{
  const gchar *model = gdk_monitor_get_model(monitor);
  const gchar *manufacturer = gdk_monitor_get_manufacturer(monitor);

  if (manufacturer && model)
    return g_strdup_printf("%s %s", manufacturer, model);
  return g_strdup(model ? model : "unknown");
}

static gboolean
monitor_matches(GdkMonitor *monitor, const gchar *wanted)
{
  g_autofree gchar *name = monitor_name(monitor);
  GdkDisplay *display = gdk_monitor_get_display(monitor);
  gint count = gdk_display_get_n_monitors(display);

  if (g_strcmp0(name, wanted) == 0)
    return TRUE;

  for (gint i = 0; i < count; i++) {
    if (gdk_display_get_monitor(display, i) == monitor) {
      g_autofree gchar *index = g_strdup_printf("%d", i);
      return g_str_equal(index, wanted);
    }
  }
  return FALSE;
}

static void
anchor_window(GtkWindow *window)
{
  gboolean top = options.position <= POSITION_TOP_RIGHT;
  gboolean bottom = options.position >= POSITION_BOTTOM_LEFT;
  gboolean left = options.position == POSITION_TOP_LEFT ||
                  options.position == POSITION_LEFT ||
                  options.position == POSITION_BOTTOM_LEFT;
  gboolean right = options.position == POSITION_TOP_RIGHT ||
                   options.position == POSITION_RIGHT ||
                   options.position == POSITION_BOTTOM_RIGHT;

  gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, top);
  gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, bottom);
  gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, left);
  gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, right);

  if (!left && !right) {
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
  }
  if (!top && !bottom) {
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
  }

  if (top)
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_TOP, options.margin);
  if (bottom)
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_BOTTOM, options.margin);
  if (left)
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_LEFT, options.margin);
  if (right)
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_RIGHT, options.margin);
}

static void
make_click_through(GtkWidget *widget, gpointer unused)
{
  cairo_region_t *empty = cairo_region_create();
  (void)unused;
  gtk_widget_input_shape_combine_region(widget, empty);
  cairo_region_destroy(empty);
}

static GtkWidget *
create_window(GdkMonitor *monitor)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GdkPixbuf) original = NULL;
  g_autoptr(GdkPixbuf) scaled = NULL;
  GtkWidget *window;
  GtkWidget *image;
  gint source_width;
  gint source_height;
  gint target_height;

  original = gdk_pixbuf_new_from_file(options.image_path, &error);
  if (!original) {
    g_printerr("whatermak: could not load '%s': %s\n",
               options.image_path, error->message);
    return NULL;
  }

  source_width = gdk_pixbuf_get_width(original);
  source_height = gdk_pixbuf_get_height(original);
  target_height = MAX(1, (gint)((gdouble)source_height * options.width /
                                source_width));
  scaled = gdk_pixbuf_scale_simple(original, options.width, target_height,
                                   GDK_INTERP_BILINEAR);
  if (!scaled) {
    g_printerr("whatermak: could not scale the image\n");
    return NULL;
  }

  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window), "whatermak");
  gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
  gtk_style_context_add_class(gtk_widget_get_style_context(window),
                              "whatermak");
  gtk_widget_set_app_paintable(window, TRUE);
  gtk_widget_set_opacity(window, options.opacity);

  gtk_layer_init_for_window(GTK_WINDOW(window));
  gtk_layer_set_namespace(GTK_WINDOW(window), "whatermak");
  gtk_layer_set_layer(GTK_WINDOW(window), options.layer);
  gtk_layer_set_keyboard_interactivity(GTK_WINDOW(window), FALSE);
  gtk_layer_set_exclusive_zone(GTK_WINDOW(window), 0);
  gtk_layer_set_monitor(GTK_WINDOW(window), monitor);
  anchor_window(GTK_WINDOW(window));

  image = gtk_image_new_from_pixbuf(scaled);
  gtk_container_add(GTK_CONTAINER(window), image);
  g_signal_connect(window, "realize", G_CALLBACK(make_click_through), NULL);
  g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  gtk_widget_show_all(window);
  return window;
}

static void
show_watermarks(void)
{
  GdkDisplay *display = gdk_display_get_default();
  gint count = gdk_display_get_n_monitors(display);
  gboolean matched = FALSE;

  windows = g_ptr_array_new();
  for (gint i = 0; i < count; i++) {
    GdkMonitor *monitor = gdk_display_get_monitor(display, i);
    gboolean selected = options.output
                            ? monitor_matches(monitor, options.output)
                            : options.all_outputs ||
                                  monitor == gdk_display_get_primary_monitor(display);
    if (!selected)
      continue;

    GtkWidget *window = create_window(monitor);
    if (!window)
      exit(EXIT_FAILURE);
    g_ptr_array_add(windows, window);
    matched = TRUE;
  }

  if (!matched) {
    g_printerr("whatermak: no output matched '%s'\n",
               options.output ? options.output : "(primary)");
    exit(EXIT_FAILURE);
  }
}

int
main(int argc, char **argv)
{
  g_autoptr(GError) error = NULL;
  GOptionEntry entries[] = {
      {"output", 'o', 0, G_OPTION_ARG_STRING, &options.output,
       "Show only on output index or monitor name", "OUTPUT"},
      {"primary", 'p', 0, G_OPTION_ARG_NONE, &options.all_outputs,
       "Show only on the primary output", NULL},
      {"position", 0, 0, G_OPTION_ARG_STRING, &options.position_name,
       "Watermark position", "POSITION"},
      {"layer", 'l', 0, G_OPTION_ARG_STRING, &options.layer_name,
       "Wayland layer: background, bottom, top, or overlay", "LAYER"},
      {"width", 'w', 0, G_OPTION_ARG_INT, &options.width,
       "Rendered width in logical pixels", "PIXELS"},
      {"margin", 'm', 0, G_OPTION_ARG_INT, &options.margin,
       "Distance from anchored screen edges", "PIXELS"},
      {"opacity", 0, 0, G_OPTION_ARG_DOUBLE, &options.opacity,
       "Opacity from 0.0 to 1.0", "VALUE"},
      {"version", 'v', 0, G_OPTION_ARG_NONE, &options.version,
       "Print version and exit", NULL},
      {NULL},
  };
  g_autoptr(GOptionContext) context =
      g_option_context_new("IMAGE - display a click-through watermark on Sway");

  /* G_OPTION_ARG_NONE sets TRUE, so invert the primary flag after parsing. */
  gboolean primary = FALSE;
  entries[1].arg_data = &primary;
  g_option_context_add_main_entries(context, entries, NULL);
  g_option_context_add_group(context, gtk_get_option_group(FALSE));

  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("whatermak: %s\n", error->message);
    return EXIT_FAILURE;
  }
  if (options.version) {
    g_print("whatermak %s\n", VERSION);
    return EXIT_SUCCESS;
  }
  if (argc != 2) {
    g_autofree gchar *help = g_option_context_get_help(context, TRUE, NULL);
    g_printerr("%s", help);
    return EXIT_FAILURE;
  }

  options.image_path = argv[1];
  options.all_outputs = !primary;
  if (options.output && primary) {
    g_printerr("whatermak: --output and --primary cannot be used together\n");
    return EXIT_FAILURE;
  }
  if (options.width < 1 || options.margin < 0 ||
      options.opacity <= 0.0 || options.opacity > 1.0) {
    g_printerr("whatermak: width must be positive, margin non-negative, and "
               "opacity greater than 0.0 and at most 1.0\n");
    return EXIT_FAILURE;
  }
  if (options.position_name &&
      !parse_position(options.position_name, &options.position)) {
    g_printerr("whatermak: invalid position '%s'\n", options.position_name);
    return EXIT_FAILURE;
  }
  if (options.layer_name && !parse_layer(options.layer_name, &options.layer)) {
    g_printerr("whatermak: invalid layer '%s'\n", options.layer_name);
    return EXIT_FAILURE;
  }
  if (!gtk_init_check(NULL, NULL)) {
    g_printerr("whatermak: could not connect to a Wayland display\n");
    return EXIT_FAILURE;
  }
  if (!gtk_layer_is_supported()) {
    g_printerr("whatermak: this compositor does not support wlr-layer-shell\n");
    return EXIT_FAILURE;
  }

  install_transparent_style();
  show_watermarks();
  gtk_main();
  g_ptr_array_free(windows, TRUE);
  return EXIT_SUCCESS;
}
