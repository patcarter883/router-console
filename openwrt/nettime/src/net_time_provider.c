/* nettime - serve the video router's clock over GStreamer's net-time protocol.
 *
 * The router is the video LAN's time authority (PTP grandmaster + NTP server).
 * This exposes the SAME clock through GStreamer's net time provider so any
 * unprivileged consumer - a GStreamer pipeline using GstNetClientClock, or a
 * custom transport implementing the same 16-byte protocol - can lock to it
 * with no privileged ports and no root.
 *
 * The clock type is forced to REALTIME so the served time is UTC epoch, which
 * matches the PTP grandmaster (phc2sys -O 0) and the camera nodes'
 * absolute-PTS scheme. GStreamer's default system clock is MONOTONIC on Linux,
 * which would put clients on a different base and break cross-camera alignment.
 */
#include <signal.h>
#include <gst/gst.h>
#include <gst/net/net.h>
#include <glib-unix.h>

static GMainLoop *g_loop;

static gboolean
on_signal (gpointer user_data)
{
  g_main_loop_quit (g_loop);
  return G_SOURCE_REMOVE;
}

int
main (int argc, char **argv)
{
  GstClock *clk;
  GstNetTimeProvider *provider;
  guint port = 5637;
  const gchar *addr = "0.0.0.0";

  gst_init (&argc, &argv);

  if (argc > 1)
    port = (guint) g_ascii_strtoull (argv[1], NULL, 10);
  if (argc > 2)
    addr = argv[2];

  /* REALTIME, not the Linux default (MONOTONIC): the LAN shares one UTC-epoch
   * timeline with the PTP grandmaster. */
  clk = gst_system_clock_obtain ();
  g_object_set (clk, "clock-type", GST_CLOCK_TYPE_REALTIME, NULL);

  provider = gst_net_time_provider_new (clk, addr, port);
  if (provider == NULL) {
    g_printerr ("nettime: failed to start provider on %s:%u\n", addr, port);
    return 1;
  }

  g_print ("nettime: serving %s on %s:%u\n",
      GST_OBJECT_NAME (clk), addr, port);
  fflush (stdout);

  g_loop = g_main_loop_new (NULL, FALSE);
  g_unix_signal_add (SIGINT, on_signal, NULL);
  g_unix_signal_add (SIGTERM, on_signal, NULL);
  g_main_loop_run (g_loop);

  gst_object_unref (provider);
  g_main_loop_unref (g_loop);
  return 0;
}
