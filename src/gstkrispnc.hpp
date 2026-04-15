///
/// Copyright Krisp, Inc
///
#pragma once

#include <gst/audio/gstaudiofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_KRISP_NC (gst_krisp_nc_get_type())
G_DECLARE_FINAL_TYPE(GstKrispNc, gst_krisp_nc, GST, KRISP_NC, GstAudioFilter)

G_END_DECLS
