///
/// Copyright Krisp, Inc
///
/// GStreamer element: krispnc
/// Wraps Krisp::AudioSdk::Nc for in-pipeline noise cancellation and voice isolation.
///
#include "gstkrispnc.hpp"
#include "krisp_session.hpp"

#include <krisp-audio-sdk-nc.hpp>

#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>

#include <memory>
#include <string>

GST_DEBUG_CATEGORY_EXTERN(krisp_sdk_debug);

// ---------------------------------------------------------------------------
// Element properties
// ---------------------------------------------------------------------------
enum
{
    PROP_0,
    PROP_MODEL,
    PROP_LICENSE_KEY,
    PROP_NOISE_SUPPRESSION_LEVEL,
    PROP_FRAME_DURATION,
};

// ---------------------------------------------------------------------------
// Private instance data
// ---------------------------------------------------------------------------
struct _GstKrispNcPrivate
{
    gchar* modelPath = nullptr;
    gchar* licenseKey = nullptr;
    gfloat nsLevel = 100.0f;
    gint frameDurationMs = 10;
    bool globalInitAcquired = false;
    bool licensingChecked = false; // checked async licensing error on first frame

    std::unique_ptr<KrispGst::IKrispSession> session;
};

// GObject boilerplate — embed private data in the instance struct
struct _GstKrispNc
{
    GstAudioFilter parent;
    _GstKrispNcPrivate priv;
};

// ---------------------------------------------------------------------------
// Pad caps template: all Krisp-supported rates, S16LE and F32LE, mono only
// ---------------------------------------------------------------------------
static GstStaticPadTemplate sSinkTemplate = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(
        "audio/x-raw, "
        "format = (string) { S16LE, F32LE }, "
        "rate = (int) { 8000, 16000, 24000, 32000, 44100, 48000, 88200, 96000 }, "
        "channels = (int) 1, "
        "layout = (string) interleaved"));

static GstStaticPadTemplate sSrcTemplate = GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(
        "audio/x-raw, "
        "format = (string) { S16LE, F32LE }, "
        "rate = (int) { 8000, 16000, 24000, 32000, 44100, 48000, 88200, 96000 }, "
        "channels = (int) 1, "
        "layout = (string) interleaved"));

G_DEFINE_TYPE(GstKrispNc, gst_krisp_nc, GST_TYPE_AUDIO_FILTER)

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void gst_krisp_nc_finalize(GObject* obj);
static void gst_krisp_nc_set_property(GObject* obj, guint id, const GValue* v, GParamSpec* ps);
static void gst_krisp_nc_get_property(GObject* obj, guint id, GValue* v, GParamSpec* ps);
static gboolean gst_krisp_nc_setup(GstAudioFilter* filter, const GstAudioInfo* info);
static GstFlowReturn gst_krisp_nc_filter(GstBaseTransform* bt, GstBuffer* buf);

// ---------------------------------------------------------------------------
// Class init
// ---------------------------------------------------------------------------
static void gst_krisp_nc_class_init(GstKrispNcClass* klass)
{
    auto* gobjectClass = G_OBJECT_CLASS(klass);
    auto* elementClass = GST_ELEMENT_CLASS(klass);
    auto* btransClass = GST_BASE_TRANSFORM_CLASS(klass);
    auto* audiofilterClass = GST_AUDIO_FILTER_CLASS(klass);

    gobjectClass->finalize = gst_krisp_nc_finalize;
    gobjectClass->set_property = gst_krisp_nc_set_property;
    gobjectClass->get_property = gst_krisp_nc_get_property;

    g_object_class_install_property(
        gobjectClass,
        PROP_MODEL,
        g_param_spec_string(
            "model",
            "Model",
            "Path to the Krisp NC/VI .kef model file",
            nullptr,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobjectClass,
        PROP_LICENSE_KEY,
        g_param_spec_string(
            "license-key",
            "License Key",
            "Krisp SDK license key (required for the server SDK)",
            nullptr,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobjectClass,
        PROP_NOISE_SUPPRESSION_LEVEL,
        g_param_spec_float(
            "noise-suppression-level",
            "Noise Suppression Level",
            "Noise suppression intensity [0.0 = off, 100.0 = full]",
            0.0f,
            100.0f,
            100.0f,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobjectClass,
        PROP_FRAME_DURATION,
        g_param_spec_int(
            "frame-duration",
            "Frame Duration (ms)",
            "Internal processing frame duration in ms (10, 15, 20, 30, 32)",
            10,
            32,
            10,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(
        elementClass,
        "Krisp Noise Cancellation & Voice Isolation",
        "Filter/Effect/Audio",
        "Real-time AI noise cancellation and voice isolation powered by the Krisp SDK",
        "Krisp Inc.");

    gst_element_class_add_static_pad_template(elementClass, &sSinkTemplate);
    gst_element_class_add_static_pad_template(elementClass, &sSrcTemplate);

    audiofilterClass->setup = gst_krisp_nc_setup;
    btransClass->transform_ip = gst_krisp_nc_filter;
    btransClass->passthrough_on_same_caps = FALSE;
}

// ---------------------------------------------------------------------------
// Instance init
// ---------------------------------------------------------------------------
static void gst_krisp_nc_init(GstKrispNc* self)
{
    new (&self->priv) _GstKrispNcPrivate();
}

// ---------------------------------------------------------------------------
// Finalize
// ---------------------------------------------------------------------------
static void gst_krisp_nc_finalize(GObject* obj)
{
    auto* self = GST_KRISP_NC(obj);
    self->priv.session.reset();
    g_free(self->priv.modelPath);
    g_free(self->priv.licenseKey);
    if (self->priv.globalInitAcquired)
    {
        KrispGst::GlobalInit::release();
    }
    self->priv.~_GstKrispNcPrivate();
    G_OBJECT_CLASS(gst_krisp_nc_parent_class)->finalize(obj);
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------
static void gst_krisp_nc_set_property(GObject* obj, guint id, const GValue* v, GParamSpec* ps)
{
    auto* self = GST_KRISP_NC(obj);
    switch (id)
    {
        case PROP_MODEL:
            g_free(self->priv.modelPath);
            self->priv.modelPath = g_value_dup_string(v);
            break;
        case PROP_LICENSE_KEY:
            g_free(self->priv.licenseKey);
            self->priv.licenseKey = g_value_dup_string(v);
            break;
        case PROP_NOISE_SUPPRESSION_LEVEL:
            self->priv.nsLevel = g_value_get_float(v);
            break;
        case PROP_FRAME_DURATION:
            self->priv.frameDurationMs = g_value_get_int(v);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, ps);
    }
}

static void gst_krisp_nc_get_property(GObject* obj, guint id, GValue* v, GParamSpec* ps)
{
    auto* self = GST_KRISP_NC(obj);
    switch (id)
    {
        case PROP_MODEL:
            g_value_set_string(v, self->priv.modelPath);
            break;
        case PROP_LICENSE_KEY:
            g_value_set_string(v, self->priv.licenseKey);
            break;
        case PROP_NOISE_SUPPRESSION_LEVEL:
            g_value_set_float(v, self->priv.nsLevel);
            break;
        case PROP_FRAME_DURATION:
            g_value_set_int(v, self->priv.frameDurationMs);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, ps);
    }
}

// ---------------------------------------------------------------------------
// Setup — called when caps are negotiated
// ---------------------------------------------------------------------------
static gboolean gst_krisp_nc_setup(GstAudioFilter* filter, const GstAudioInfo* info)
{
    auto* self = GST_KRISP_NC(filter);

    if (!self->priv.modelPath || self->priv.modelPath[0] == '\0')
    {
        GST_ELEMENT_ERROR(
            self, RESOURCE, NOT_FOUND, ("'model' property must be set before the pipeline starts"), (nullptr));
        return FALSE;
    }

    self->priv.session.reset();
    self->priv.licensingChecked = false;

    const int rate = GST_AUDIO_INFO_RATE(info);
    const int fd = self->priv.frameDurationMs;
    const int frameSz = rate * fd / 1000;

    std::string pathStr(self->priv.modelPath);
    std::wstring modelWpath(pathStr.begin(), pathStr.end());

    Krisp::AudioSdk::ModelInfo modelInfo;
    modelInfo.path = modelWpath;

    const GstAudioFormat fmt = GST_AUDIO_INFO_FORMAT(info);

    try
    {
        if (!self->priv.globalInitAcquired)
        {
            KrispGst::GlobalInit::acquire(
                self->priv.licenseKey ? self->priv.licenseKey : "",
                [](const std::string& msg, Krisp::AudioSdk::LogLevel lvl)
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
                });
            self->priv.globalInitAcquired = true;
        }

        if (fmt == GST_AUDIO_FORMAT_F32LE)
        {
            Krisp::AudioSdk::NcSessionConfig cfg{};
            cfg.inputSampleRate = KrispGst::toKrispRate(rate);
            cfg.inputFrameDuration = KrispGst::toKrispFrameDuration(fd);
            cfg.outputSampleRate = KrispGst::toKrispRate(rate);
            cfg.modelInfo = &modelInfo;
            cfg.enableSessionStats = false;

            auto session = Krisp::AudioSdk::Nc<float>::create(cfg);
            self->priv.session = std::make_unique<KrispGst::KrispSession<Krisp::AudioSdk::Nc<float>, float>>(
                std::move(session), frameSz);
        }
        else
        { // S16LE
            Krisp::AudioSdk::NcSessionConfig cfg{};
            cfg.inputSampleRate = KrispGst::toKrispRate(rate);
            cfg.inputFrameDuration = KrispGst::toKrispFrameDuration(fd);
            cfg.outputSampleRate = KrispGst::toKrispRate(rate);
            cfg.modelInfo = &modelInfo;
            cfg.enableSessionStats = false;

            auto session = Krisp::AudioSdk::Nc<int16_t>::create(cfg);
            self->priv.session = std::make_unique<KrispGst::KrispSession<Krisp::AudioSdk::Nc<int16_t>, int16_t>>(
                std::move(session), frameSz);
        }
    }
    catch (const std::exception& e)
    {
        GST_ELEMENT_ERROR(self, LIBRARY, INIT, ("Failed to create Krisp NC session: %s", e.what()), (nullptr));
        return FALSE;
    }

    GST_INFO_OBJECT(self, "Krisp NC session created: rate=%d fd=%dms fmt=%s", rate, fd, GST_AUDIO_INFO_NAME(info));
    return TRUE;
}

// ---------------------------------------------------------------------------
// Filter — called per GstBuffer (in-place)
// ---------------------------------------------------------------------------
static GstFlowReturn gst_krisp_nc_filter(GstBaseTransform* bt, GstBuffer* buf)
{
    auto* self = GST_KRISP_NC(bt);

    if (!self->priv.session)
    {
        GST_ELEMENT_ERROR(self, STREAM, FAILED, ("No Krisp NC session"), (nullptr));
        return GST_FLOW_ERROR;
    }

    // Check for a licensing error posted asynchronously by the SDK callback.
    // We only need to check once. A licensing error is non-fatal — the SDK
    // continues to pass audio through its grace period, so we post a warning
    // to the bus and keep processing.
    if (!self->priv.licensingChecked)
    {
        self->priv.licensingChecked = true;
        const std::string licErr = KrispGst::GlobalInit::lastLicensingError();
        if (!licErr.empty())
        {
            GST_ELEMENT_WARNING(
                self, RESOURCE, OPEN_READ, ("Krisp SDK licensing error: %s", licErr.c_str()), (nullptr));
        }
    }

    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_READWRITE))
    {
        GST_ELEMENT_ERROR(self, STREAM, FAILED, ("Failed to map buffer"), (nullptr));
        return GST_FLOW_ERROR;
    }

    const int bps = self->priv.session->bytesPerSample();
    self->priv.session->processInplace(map.data, map.size / bps, self->priv.nsLevel);

    gst_buffer_unmap(buf, &map);

    return GST_FLOW_OK;
}
