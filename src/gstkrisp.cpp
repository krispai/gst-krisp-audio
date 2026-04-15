///
/// Copyright Krisp, Inc
///
/// GStreamer plugin entry point — registers krispnc and/or krispaccent elements
/// depending on which features were enabled at build time.
///
#ifdef KRISP_HAS_NC
#include "gstkrispnc.hpp"
#endif
#ifdef KRISP_HAS_AR
#include "gstkrispaccent.hpp"
#endif
#include "krisp_session.hpp"

#include <gst/gst.h>

// ---------------------------------------------------------------------------
// Shared debug category — routes Krisp SDK log messages into GStreamer's debug
// system. Enable with GST_DEBUG=krisp-sdk:5 (5 = TRACE, 4 = DEBUG, …).
// Defined here (non-static) so element translation units can extern it.
// ---------------------------------------------------------------------------
GST_DEBUG_CATEGORY(krisp_sdk_debug);

// ---------------------------------------------------------------------------
// GlobalInit static members
// ---------------------------------------------------------------------------
namespace KrispGst
{
std::mutex GlobalInit::_mutex;
std::mutex GlobalInit::_errorMutex;
int GlobalInit::_refcount = 0;
std::string GlobalInit::_lastError;
} // namespace KrispGst

// ---------------------------------------------------------------------------
// Plugin init
// ---------------------------------------------------------------------------
static gboolean pluginInit(GstPlugin* plugin)
{
    GST_DEBUG_CATEGORY_INIT(krisp_sdk_debug, "krisp-sdk", 0, "Krisp Audio SDK messages");

    gboolean ok = TRUE;
#ifdef KRISP_HAS_NC
    ok = ok && gst_element_register(plugin, "krispnc", GST_RANK_NONE, GST_TYPE_KRISP_NC);
#endif
#ifdef KRISP_HAS_AR
    ok = ok && gst_element_register(plugin, "krispaccent", GST_RANK_NONE, GST_TYPE_KRISP_ACCENT);
#endif
    (void)plugin; // suppressed when neither feature macro is defined (prevented by Meson)
    return ok;
}

#ifndef PACKAGE
#define PACKAGE "gst-krisp-audio"
#endif

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    krisp,
    "Krisp AI audio processing plugin: noise cancellation, voice isolation, and accent reduction",
    pluginInit,
    "1.0.0",
    "Proprietary",
    PACKAGE,
    "https://krisp.ai")
