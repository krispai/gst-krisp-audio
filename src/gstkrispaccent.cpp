///
/// Copyright Krisp, Inc
///
/// GStreamer element: krispaccent
/// Wraps Krisp::AudioSdk::Ar for in-pipeline accent reduction.
///
#include "gstkrispaccent.hpp"
#include "gstkrisp_common.hpp"

#include <krisp-audio-sdk-ar.hpp>

#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>

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

// GObject boilerplate — embed shared private data in the instance struct
struct _GstKrispAccent
{
    GstAudioFilter parent;
    KrispElementPrivate priv;
};

G_DEFINE_TYPE(GstKrispAccent, gst_krisp_accent, GST_TYPE_AUDIO_FILTER)

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void gst_krisp_accent_finalize(GObject* obj);
static void gst_krisp_accent_set_property(GObject* obj, guint id, const GValue* v, GParamSpec* ps);
static void gst_krisp_accent_get_property(GObject* obj, guint id, GValue* v, GParamSpec* ps);
static gboolean gst_krisp_accent_setup(GstAudioFilter* filter, const GstAudioInfo* info);
static GstFlowReturn gst_krisp_accent_filter(GstBaseTransform* bt, GstBuffer* buf);

// ---------------------------------------------------------------------------
// Class init
// ---------------------------------------------------------------------------
static void gst_krisp_accent_class_init(GstKrispAccentClass* klass)
{
    auto* gobjectClass = G_OBJECT_CLASS(klass);
    auto* elementClass = GST_ELEMENT_CLASS(klass);
    auto* btransClass = GST_BASE_TRANSFORM_CLASS(klass);
    auto* audiofilterClass = GST_AUDIO_FILTER_CLASS(klass);

    gobjectClass->finalize = gst_krisp_accent_finalize;
    gobjectClass->set_property = gst_krisp_accent_set_property;
    gobjectClass->get_property = gst_krisp_accent_get_property;

    krispElementInstallProperties(gobjectClass, "Path to the Krisp AR .kef model file");

    gst_element_class_set_static_metadata(
        elementClass,
        "Krisp Accent Reduction",
        "Filter/Effect/Audio",
        "Real-time AI accent reduction powered by the Krisp SDK",
        "Krisp Inc.");

    gst_element_class_add_static_pad_template(elementClass, &sSinkTemplate);
    gst_element_class_add_static_pad_template(elementClass, &sSrcTemplate);

    audiofilterClass->setup = gst_krisp_accent_setup;
    btransClass->transform_ip = gst_krisp_accent_filter;
    btransClass->passthrough_on_same_caps = FALSE;
}

// ---------------------------------------------------------------------------
// Instance init
// ---------------------------------------------------------------------------
static void gst_krisp_accent_init(GstKrispAccent* self)
{
    new (&self->priv) KrispElementPrivate();
}

// ---------------------------------------------------------------------------
// Finalize
// ---------------------------------------------------------------------------
static void gst_krisp_accent_finalize(GObject* obj)
{
    auto* self = GST_KRISP_ACCENT(obj);
    krispElementFinalizePriv(&self->priv);
    self->priv.~KrispElementPrivate();
    G_OBJECT_CLASS(gst_krisp_accent_parent_class)->finalize(obj);
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------
static void gst_krisp_accent_set_property(GObject* obj, guint id, const GValue* v, GParamSpec* ps)
{
    if (!krispElementSetProperty(&GST_KRISP_ACCENT(obj)->priv, id, v))
    {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, ps);
    }
}

static void gst_krisp_accent_get_property(GObject* obj, guint id, GValue* v, GParamSpec* ps)
{
    if (!krispElementGetProperty(&GST_KRISP_ACCENT(obj)->priv, id, v))
    {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, ps);
    }
}

// ---------------------------------------------------------------------------
// Setup — shared preamble + AR-specific session factory
// ---------------------------------------------------------------------------
static gboolean gst_krisp_accent_setup(GstAudioFilter* filter, const GstAudioInfo* info)
{
    return krispElementSetup(
        "AR",
        filter,
        &GST_KRISP_ACCENT(filter)->priv,
        info,
        [](GstAudioFormat fmt, int rate, int fd, int frameSz, const std::string& modelPath)
            -> std::unique_ptr<KrispGst::IKrispSession>
        {
            std::wstring modelWpath = KrispGst::utf8ToWide(modelPath);
            Krisp::AudioSdk::ModelInfo mi;
            mi.path = modelWpath;

            Krisp::AudioSdk::ArSessionConfig cfg{};
            cfg.inputSampleRate = KrispGst::toKrispRate(rate);
            cfg.inputFrameDuration = KrispGst::toKrispFrameDuration(fd);
            cfg.outputSampleRate = KrispGst::toKrispRate(rate);
            cfg.modelInfo = &mi;

            if (fmt == GST_AUDIO_FORMAT_F32LE)
            {
                return std::make_unique<KrispGst::KrispSession<Krisp::AudioSdk::Ar<float>, float>>(
                    Krisp::AudioSdk::Ar<float>::create(cfg), frameSz);
            }
            return std::make_unique<KrispGst::KrispSession<Krisp::AudioSdk::Ar<int16_t>, int16_t>>(
                Krisp::AudioSdk::Ar<int16_t>::create(cfg), frameSz);
        });
}

// ---------------------------------------------------------------------------
// Filter
// ---------------------------------------------------------------------------
static GstFlowReturn gst_krisp_accent_filter(GstBaseTransform* bt, GstBuffer* buf)
{
    return krispElementFilter("AR", bt, &GST_KRISP_ACCENT(bt)->priv, buf);
}
