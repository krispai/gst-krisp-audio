///
/// Copyright Krisp, Inc
///
/// Shared GObject/GStreamer helpers for Krisp audio filter elements.
/// Include this from element-specific .cpp files only (after their SDK header).
///
#pragma once

#include "krisp_session.hpp"

#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>
#include <gst/gst.h>

#include <functional>
#include <memory>
#include <string>

// ---------------------------------------------------------------------------
// Property enum — shared across all Krisp filter elements
// ---------------------------------------------------------------------------
enum KrispElementProp
{
    KRISP_PROP_0,
    KRISP_PROP_MODEL,
    KRISP_PROP_LICENSE_KEY,
    KRISP_PROP_NOISE_SUPPRESSION_LEVEL,
    KRISP_PROP_FRAME_DURATION,
};

// ---------------------------------------------------------------------------
// Shared private data
//
// Embedded via placement-new in each element's instance struct (instance_init),
// and destroyed via explicit destructor call in finalize.
// ---------------------------------------------------------------------------
struct KrispElementPrivate
{
    gchar* modelPath = nullptr;
    gchar* licenseKey = nullptr;
    gfloat nsLevel = 100.0f;
    gint frameDurationMs = 10;
    bool globalInitAcquired = false;
    bool licensingChecked = false; // checked async licensing error on first frame

    std::unique_ptr<KrispGst::IKrispSession> session;
};

// ---------------------------------------------------------------------------
// Session factory callback type
//
// Defined as a lambda in the element-specific .cpp where the SDK-specific
// header (krisp-audio-sdk-nc.hpp / krisp-audio-sdk-ar.hpp) is included.
//
// Parameters:
//   fmt       — GST_AUDIO_FORMAT_S16LE or GST_AUDIO_FORMAT_F32LE
//   rate      — sample rate in Hz
//   fd        — frame duration in ms (from priv.frameDurationMs)
//   frameSz   — rate * fd / 1000 (pre-computed)
//   modelPath — UTF-8 path string
//
// Returns a non-null unique_ptr on success; throws std::exception on failure.
// ---------------------------------------------------------------------------
using KrispSessionFactory = std::function<std::unique_ptr<KrispGst::IKrispSession>(
    GstAudioFormat fmt, int rate, int fd, int frameSz, const std::string& modelPath)>;

// ---------------------------------------------------------------------------
// Property helpers
// ---------------------------------------------------------------------------

// Installs the four standard Krisp properties onto gobjectClass.
// modelDescription is element-specific (NC vs AR model path description).
void krispElementInstallProperties(GObjectClass* klass, const gchar* modelDescription);

// Handles the set_property switch. Returns FALSE for unknown ids; caller
// should then call G_OBJECT_WARN_INVALID_PROPERTY_ID.
gboolean krispElementSetProperty(KrispElementPrivate* priv, guint id, const GValue* v);

// Handles the get_property switch. Returns FALSE for unknown or write-only ids.
gboolean krispElementGetProperty(KrispElementPrivate* priv, guint id, GValue* v);

// ---------------------------------------------------------------------------
// Finalize helper
//
// Tears down the session, frees gchar* fields, and releases GlobalInit.
// The caller must follow this with:
//   priv->~KrispElementPrivate();
//   G_OBJECT_CLASS(xxx_parent_class)->finalize(obj);
// (Both of these name the concrete element type and cannot be shared.)
// ---------------------------------------------------------------------------
void krispElementFinalizePriv(KrispElementPrivate* priv);

// ---------------------------------------------------------------------------
// Setup helper — called from GstAudioFilter::setup
//
// Validates modelPath, resets session state, acquires GlobalInit (with the
// shared SDK log callback), then calls factory to create the session.
//
// elementName  — used in log/error messages ("NC" or "AR")
// filter       — the GstAudioFilter* (IS-A GstElement*, used in error posting)
// ---------------------------------------------------------------------------
gboolean krispElementSetup(
    const gchar* elementName,
    GstAudioFilter* filter,
    KrispElementPrivate* priv,
    const GstAudioInfo* info,
    KrispSessionFactory factory);

// ---------------------------------------------------------------------------
// Filter helper — called from GstBaseTransform::transform_ip
//
// Checks session existence, performs first-frame licensing check, maps the
// buffer, calls processInplace, then unmaps.
//
// elementName  — used in error messages ("NC" or "AR")
// bt           — the GstBaseTransform* (IS-A GstElement*)
// ---------------------------------------------------------------------------
GstFlowReturn krispElementFilter(
    const gchar* elementName, GstBaseTransform* bt, KrispElementPrivate* priv, GstBuffer* buf);
