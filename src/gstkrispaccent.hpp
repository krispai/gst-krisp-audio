///
/// Copyright Krisp, Inc
///
#pragma once

#include <gst/audio/gstaudiofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_KRISP_ACCENT (gst_krisp_accent_get_type())
G_DECLARE_FINAL_TYPE(GstKrispAccent, gst_krisp_accent, GST, KRISP_ACCENT, GstAudioFilter)

G_END_DECLS
