///
/// Copyright Krisp, Inc
///
#include "gstkrisp_common.hpp"

#include <gst/gst.h>

GST_DEBUG_CATEGORY_EXTERN(krisp_sdk_debug);

// ---------------------------------------------------------------------------
// SDK log callback — maps Krisp log levels to GStreamer debug levels.
// Passed to GlobalInit::acquire; defined once here rather than duplicated
// in each element's setup function.
// ---------------------------------------------------------------------------
static void krisp_sdk_log_cb(const std::string& msg, Krisp::AudioSdk::LogLevel lvl)
{
    static const GstDebugLevel levelMap[] = {
        GST_LEVEL_MEMDUMP, // Trace
        GST_LEVEL_DEBUG,   // Debug
        GST_LEVEL_INFO,    // Info
        GST_LEVEL_WARNING, // Warn
        GST_LEVEL_ERROR,   // Err
        GST_LEVEL_ERROR,   // Critical
    };
    const auto idx = static_cast<int>(lvl);
    const GstDebugLevel gstLvl = (idx >= 0 && idx < 6) ? levelMap[idx] : GST_LEVEL_DEBUG;
    GST_CAT_LEVEL_LOG(krisp_sdk_debug, gstLvl, nullptr, "%s", msg.c_str());
}

// ---------------------------------------------------------------------------
// Property installation
// ---------------------------------------------------------------------------
void krispElementInstallProperties(GObjectClass* klass, const gchar* modelDescription)
{
    g_object_class_install_property(
        klass,
        KRISP_PROP_MODEL,
        g_param_spec_string(
            "model", "Model", modelDescription, nullptr, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        klass,
        KRISP_PROP_LICENSE_KEY,
        g_param_spec_string(
            "license-key",
            "License Key",
            "Krisp SDK license key (required for the server SDK)",
            nullptr,
            (GParamFlags)(G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        klass,
        KRISP_PROP_NOISE_SUPPRESSION_LEVEL,
        g_param_spec_float(
            "noise-suppression-level",
            "Noise Suppression Level",
            "Noise suppression intensity [0.0 = off, 100.0 = full]",
            0.0f,
            100.0f,
            100.0f,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        klass,
        KRISP_PROP_FRAME_DURATION,
        g_param_spec_int(
            "frame-duration",
            "Frame Duration (ms)",
            "Internal processing frame duration in ms (10, 15, 20, 30, 32)",
            10,
            32,
            10,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

// ---------------------------------------------------------------------------
// Property set/get
// ---------------------------------------------------------------------------
gboolean krispElementSetProperty(KrispElementPrivate* priv, guint id, const GValue* v)
{
    switch (id)
    {
        case KRISP_PROP_MODEL:
            g_free(priv->modelPath);
            priv->modelPath = g_value_dup_string(v);
            return TRUE;
        case KRISP_PROP_LICENSE_KEY:
            g_free(priv->licenseKey);
            priv->licenseKey = g_value_dup_string(v);
            return TRUE;
        case KRISP_PROP_NOISE_SUPPRESSION_LEVEL:
            priv->nsLevel = g_value_get_float(v);
            return TRUE;
        case KRISP_PROP_FRAME_DURATION:
            priv->frameDurationMs = g_value_get_int(v);
            return TRUE;
        default:
            return FALSE;
    }
}

gboolean krispElementGetProperty(KrispElementPrivate* priv, guint id, GValue* v)
{
    switch (id)
    {
        case KRISP_PROP_MODEL:
            g_value_set_string(v, priv->modelPath);
            return TRUE;
        case KRISP_PROP_NOISE_SUPPRESSION_LEVEL:
            g_value_set_float(v, priv->nsLevel);
            return TRUE;
        case KRISP_PROP_FRAME_DURATION:
            g_value_set_int(v, priv->frameDurationMs);
            return TRUE;
        default:
            return FALSE;
    }
}

// ---------------------------------------------------------------------------
// Finalize helper
// ---------------------------------------------------------------------------
void krispElementFinalizePriv(KrispElementPrivate* priv)
{
    priv->session.reset();
    g_free(priv->modelPath);
    g_free(priv->licenseKey);
    if (priv->globalInitAcquired)
    {
        KrispGst::GlobalInit::release();
    }
    // Caller must follow with:
    //   priv->~KrispElementPrivate();
    //   G_OBJECT_CLASS(xxx_parent_class)->finalize(obj);
}

// ---------------------------------------------------------------------------
// Setup helper
// ---------------------------------------------------------------------------
gboolean krispElementSetup(
    const gchar* elementName,
    GstAudioFilter* filter,
    KrispElementPrivate* priv,
    const GstAudioInfo* info,
    KrispSessionFactory factory)
{
    if (!priv->modelPath || priv->modelPath[0] == '\0')
    {
        GST_ELEMENT_ERROR(
            filter, RESOURCE, NOT_FOUND, ("'model' property must be set before the pipeline starts"), (nullptr));
        return FALSE;
    }

    priv->session.reset();
    priv->licensingChecked = false;

    const int rate = GST_AUDIO_INFO_RATE(info);
    const int fd = priv->frameDurationMs;
    const gint64 frameSz64 = static_cast<gint64>(rate) * fd / 1000;
    if (frameSz64 <= 0 || frameSz64 > G_MAXINT)
    {
        GST_ELEMENT_ERROR(
            filter,
            STREAM,
            FAILED,
            ("Computed frame size %" G_GINT64_FORMAT " is out of range (rate=%d fd=%dms)", frameSz64, rate, fd),
            (nullptr));
        return FALSE;
    }
    const int frameSz = static_cast<int>(frameSz64);
    const GstAudioFormat fmt = GST_AUDIO_INFO_FORMAT(info);
    const std::string modelPath(priv->modelPath);

    try
    {
        if (!priv->globalInitAcquired)
        {
            KrispGst::GlobalInit::acquire(priv->licenseKey ? priv->licenseKey : "", krisp_sdk_log_cb);
            priv->globalInitAcquired = true;
        }

        priv->session = factory(fmt, rate, fd, frameSz, modelPath);
    }
    catch (const std::exception& e)
    {
        GST_ELEMENT_ERROR(
            filter, LIBRARY, INIT, ("Failed to create Krisp %s session: %s", elementName, e.what()), (nullptr));
        return FALSE;
    }

    GST_INFO_OBJECT(
        filter, "Krisp %s session created: rate=%d fd=%dms fmt=%s", elementName, rate, fd, GST_AUDIO_INFO_NAME(info));
    return TRUE;
}

// ---------------------------------------------------------------------------
// Filter helper
// ---------------------------------------------------------------------------
GstFlowReturn krispElementFilter(
    const gchar* elementName, GstBaseTransform* bt, KrispElementPrivate* priv, GstBuffer* buf)
{
    if (!priv->session)
    {
        GST_ELEMENT_ERROR(bt, STREAM, FAILED, ("No Krisp %s session", elementName), (nullptr));
        return GST_FLOW_ERROR;
    }

    // Check for a licensing error posted asynchronously by the SDK callback.
    // We only need to check once. A licensing error is non-fatal — the SDK
    // continues to pass audio through its grace period, so we post a warning
    // to the bus and keep processing.
    if (!priv->licensingChecked)
    {
        priv->licensingChecked = true;
        const std::string licErr = KrispGst::GlobalInit::lastLicensingError();
        if (!licErr.empty())
        {
            GST_ELEMENT_WARNING(bt, RESOURCE, OPEN_READ, ("Krisp SDK licensing error: %s", licErr.c_str()), (nullptr));
        }
    }

    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_READWRITE))
    {
        GST_ELEMENT_ERROR(bt, STREAM, FAILED, ("Failed to map buffer"), (nullptr));
        return GST_FLOW_ERROR;
    }

    const int bps = priv->session->bytesPerSample();
    if (map.size % static_cast<gsize>(bps) != 0)
    {
        gst_buffer_unmap(buf, &map);
        GST_ELEMENT_ERROR(
            bt,
            STREAM,
            FAILED,
            ("Buffer size %" G_GSIZE_FORMAT " is not a multiple of sample size %d", map.size, bps),
            (nullptr));
        return GST_FLOW_ERROR;
    }

    priv->session->processInplace(map.data, map.size / bps, priv->nsLevel);

    gst_buffer_unmap(buf, &map);

    return GST_FLOW_OK;
}
