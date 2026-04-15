///
/// Copyright Krisp, Inc
///
/// Pipeline integration test.
///
/// Usage (set by meson test runner):
///   KRISP_NC_MODEL     — path to an NC .kef model file
///   KRISP_AR_MODEL     — path to an AR .kef model file
///   KRISP_TEST_INPUT   — path to a mono WAV file (any supported rate)
///   GST_PLUGIN_PATH    — must include the build/src directory
///
#include <gst/audio/audio.h>
#include <gst/gst.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string env(const char* var, const char* fallback = "")
{
    const char* v = std::getenv(var);
    return v ? std::string(v) : std::string(fallback);
}

/// Runs a pipeline described by @p desc.
/// Returns true on EOS, false on error.
/// Licensing warnings from the Krisp elements are printed but do not stop
/// the pipeline — the SDK passes audio through during its grace period.
static bool runPipeline(const std::string& desc)
{
    GError* err = nullptr;
    GstElement* pipeline = gst_parse_launch(desc.c_str(), &err);
    if (!pipeline || err)
    {
        g_printerr("Pipeline parse error: %s\n", err ? err->message : "unknown");
        if (err)
        {
            g_error_free(err);
        }
        return false;
    }

    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        g_printerr("Failed to start pipeline\n");
        gst_object_unref(pipeline);
        return false;
    }

    GstBus* bus = gst_element_get_bus(pipeline);
    bool success = false;

    while (true)
    {
        GstMessage* msg = gst_bus_timed_pop_filtered(
            bus,
            GST_CLOCK_TIME_NONE,
            (GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR | GST_MESSAGE_WARNING));

        switch (GST_MESSAGE_TYPE(msg))
        {
            case GST_MESSAGE_EOS:
                success = true;
                gst_message_unref(msg);
                goto done;

            case GST_MESSAGE_WARNING:
            {
                GError* gerr = nullptr;
                gchar* dbg = nullptr;
                gst_message_parse_warning(msg, &gerr, &dbg);
                g_printerr("Pipeline warning [%s]: %s\n%s\n",
                    GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)),
                    gerr->message,
                    dbg ? dbg : "");
                g_error_free(gerr);
                g_free(dbg);
                gst_message_unref(msg);
                break; // non-fatal — keep running
            }

            default: // GST_MESSAGE_ERROR
            {
                GError* gerr = nullptr;
                gchar* dbg = nullptr;
                gst_message_parse_error(msg, &gerr, &dbg);
                g_printerr("Pipeline error [%s]: %s\n%s\n",
                    GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)),
                    gerr->message,
                    dbg ? dbg : "");
                g_error_free(gerr);
                g_free(dbg);
                gst_message_unref(msg);
                goto done;
            }
        }
    }

done:
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    return success;
}

// ---------------------------------------------------------------------------
// Test cases
// ---------------------------------------------------------------------------

static int testNc(
    const std::string& input, const std::string& model, const std::string& output, const std::string& licenseKey)
{
    g_print("\n=== TEST: krispnc ===\n");

    if (model.empty())
    {
        g_print("SKIP: KRISP_NC_MODEL not set\n");
        return 0;
    }

    std::string desc = "filesrc location=\"" + input +
                       "\" "
                       "! wavparse "
                       "! audioconvert "
                       "! audioresample "
                       "! audio/x-raw,format=F32LE,channels=1 "
                       "! krispnc model=\"" +
                       model +
                       "\""
                       " license-key=\"" +
                       licenseKey +
                       "\""
                       " noise-suppression-level=100 "
                       "! audioconvert "
                       "! wavenc "
                       "! filesink location=\"" +
                       output + "\"";

    g_print("Pipeline:\n  %s\n", desc.c_str());

    if (!runPipeline(desc))
    {
        g_printerr("FAIL: krispnc pipeline returned error\n");
        return 1;
    }

    // Verify output file exists and has non-zero size
    FILE* f = std::fopen(output.c_str(), "rb");
    if (!f)
    {
        g_printerr("FAIL: output file not created: %s\n", output.c_str());
        return 1;
    }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fclose(f);

    if (sz < 44) // WAV header is 44 bytes minimum
    {
        g_printerr("FAIL: output file too small (%ld bytes)\n", sz);
        return 1;
    }

    g_print("PASS: output written to %s (%ld bytes)\n", output.c_str(), sz);
    return 0;
}

static int testAccent(
    const std::string& input, const std::string& model, const std::string& output, const std::string& licenseKey)
{
    g_print("\n=== TEST: krispaccent ===\n");

    if (model.empty())
    {
        g_print("SKIP: KRISP_AR_MODEL not set\n");
        return 0;
    }

    std::string desc = "filesrc location=\"" + input +
                       "\" "
                       "! wavparse "
                       "! audioconvert "
                       "! audioresample "
                       "! audio/x-raw,format=F32LE,channels=1 "
                       "! krispaccent model=\"" +
                       model +
                       "\""
                       " license-key=\"" +
                       licenseKey +
                       "\""
                       " noise-suppression-level=100 "
                       "! audioconvert "
                       "! wavenc "
                       "! filesink location=\"" +
                       output + "\"";

    g_print("Pipeline:\n  %s\n", desc.c_str());

    if (!runPipeline(desc))
    {
        g_printerr("FAIL: krispaccent pipeline returned error\n");
        return 1;
    }

    FILE* f = std::fopen(output.c_str(), "rb");
    if (!f)
    {
        g_printerr("FAIL: output file not created: %s\n", output.c_str());
        return 1;
    }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fclose(f);

    if (sz < 44)
    {
        g_printerr("FAIL: output file too small (%ld bytes)\n", sz);
        return 1;
    }

    g_print("PASS: output written to %s (%ld bytes)\n", output.c_str(), sz);
    return 0;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    gst_init(&argc, &argv);

    const std::string input = env("KRISP_TEST_INPUT");
    const std::string ncModel = env("KRISP_NC_MODEL");
    const std::string arModel = env("KRISP_AR_MODEL");
    const std::string licenseKey = env("KRISP_LICENSE_KEY");

    if (input.empty())
    {
        g_printerr("ERROR: KRISP_TEST_INPUT environment variable not set\n");
        return 1;
    }

    // Check the input file exists
    FILE* f = std::fopen(input.c_str(), "rb");
    if (!f)
    {
        g_printerr("ERROR: input file not found: %s\n", input.c_str());
        return 1;
    }
    std::fclose(f);

    const std::string ncOutput = env("KRISP_NC_OUTPUT", "./output_nc.wav");
    const std::string arOutput = env("KRISP_AR_OUTPUT", "./output_accent.wav");

    int result = 0;
    result += testNc(input, ncModel, ncOutput, licenseKey);
    result += testAccent(input, arModel, arOutput, licenseKey);

    if (result == 0)
    {
        g_print("\nAll tests PASSED\n");
    }
    else
    {
        g_printerr("\n%d test(s) FAILED\n", result);
    }

    return result;
}
