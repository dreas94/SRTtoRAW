#pragma once
#include "pipeline.h"
#include <iostream>
#include <direct.h>

Pipeline::~Pipeline()
{
    /* Free resources */
    g_main_loop_unref(m_mainLoop);
    gst_object_unref(m_bus);
    gst_element_set_state(m_data.pipelineElements["pipeline"], GST_STATE_NULL);
    gst_object_unref(m_data.pipelineElements["pipeline"]);
}

bool Pipeline::Init(int argc, char* argv[])
{
    /* Initialize GStreamer */
    gst_init(&argc, &argv);

    /* Initialize our data structure */
    m_data.loop = NULL;
    m_data.pipelineElements["source"] = gst_element_factory_make("srtsrc", "source");
    m_data.pipelineElements["srtqueue"] = gst_element_factory_make("queue", "srtqueue");
    m_data.pipelineElements["demux"] = gst_element_factory_make("tsdemux", "demux");
    m_data.pipelineElements["audioqueue"] = gst_element_factory_make("queue", "audioqueue");
    m_data.pipelineElements["videoqueue"] = gst_element_factory_make("queue", "videoqueue");
    m_data.pipelineElements["videosink"] = gst_element_factory_make("filesink", "videosink");
    m_data.pipelineElements["audiosink"] = gst_element_factory_make("filesink", "audiosink");
    m_data.isLive = FALSE;

    if (!CreatePipeline(m_data, m_bus, m_mainLoop))
        return false;

    return true;
}

void Pipeline::RunLoop()
{
    g_main_loop_run(m_mainLoop);
}

bool Pipeline::CreatePipeline(CustomData& data, GstBus*& bus, GMainLoop*& mainLoop)
{
    /* Create the empty pipeline */
    data.pipelineElements["pipeline"] = gst_pipeline_new("SRTtoFile-pipeline");
    bus = gst_pipeline_get_bus(GST_PIPELINE(data.pipelineElements["pipeline"]));

    GstElementPointers::iterator pipelineElement;
    for (pipelineElement = data.pipelineElements.begin(); pipelineElement != data.pipelineElements.end(); pipelineElement++)
    {
        if (!pipelineElement->second)
        {
            g_printerr("'%s' could not be created.\n", pipelineElement->first.c_str());
            return false;
        }
    }

    if (!BuildAndLinkPipeline(data))
        return false;

    /* Set the URI to play */
    g_object_set(data.pipelineElements["source"], "uri", "srt://127.0.0.1:1234?mode=listener", NULL);

    /* Decide what path is used for the file sinks */
    std::string path = "";
    std::cout << "Please enter the path you desire to create Audio/Video folders in (do not add a final / in the end): ";
    std::cin >> path;

    std::string audioPath = path + "/Audio";

    /* Creating Audio Folder */
    if (_mkdir(audioPath.c_str()) == -1 && errno != EEXIST)
    {
        g_printerr("Audio Folder could not be created.\n");
        return false;
    }
    
    audioPath += "/audio.raw";

    std::string videoPath = path + "/Video";

    /* Creating File */
    if (_mkdir(videoPath.c_str()) == -1 && errno != EEXIST)
    {
        g_printerr("Video Folder could not be created.\n");
        return false;
    }
    
    videoPath += "/video.raw";


    g_object_set(data.pipelineElements["audiosink"], "location", audioPath.c_str(), NULL);
    g_object_set(data.pipelineElements["videosink"], "location", videoPath.c_str(), NULL);

    /* Connect to the pad-added signal */
    g_signal_connect(data.pipelineElements["demux"], "pad-added", G_CALLBACK(PadAddedHandler), &data);

    /* Start playing */
    GstStateChangeReturn ret = gst_element_set_state(data.pipelineElements["pipeline"], GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        g_printerr("Unable to set the pipeline to the playing state.\n");
        gst_object_unref(data.pipelineElements["pipeline"]);
        return false;
    }
    else if (ret == GST_STATE_CHANGE_NO_PREROLL)
    {
        data.isLive = TRUE;
    }

    mainLoop = g_main_loop_new(NULL, FALSE);
    data.loop = mainLoop;

    gst_bus_add_signal_watch(bus);
    g_signal_connect(bus, "message", G_CALLBACK(BusMessageHandler), &data);

    return true;
}

bool Pipeline::BuildAndLinkPipeline(CustomData& data)
{
    /* Build the pipeline. Avoid linkink the source->queue->demux to the remaining pipeline for now, this will be done later. */
    gst_bin_add_many(GST_BIN(data.pipelineElements["pipeline"]), data.pipelineElements["source"], data.pipelineElements["srtqueue"], data.pipelineElements["demux"],
        data.pipelineElements["videoqueue"], data.pipelineElements["audioqueue"], data.pipelineElements["videosink"], data.pipelineElements["audiosink"], NULL);

    if (!gst_element_link_many(data.pipelineElements["source"], data.pipelineElements["srtqueue"], data.pipelineElements["demux"], NULL))
    {
        g_printerr("Elements (source, srtqueue, demux) could not be linked.\n");
        gst_object_unref(data.pipelineElements["pipeline"]);
        return false;
    }

    if (!gst_element_link_many(data.pipelineElements["audioqueue"], data.pipelineElements["audiosink"], NULL))
    {
        g_printerr("Elements (audioqueue, audioparse, audiosink) could not be linked.\n");
        gst_object_unref(data.pipelineElements["pipeline"]);
        return false;
    }

    if (!gst_element_link_many(data.pipelineElements["videoqueue"], data.pipelineElements["videosink"], NULL))
    {
        g_printerr("Elements (videoqueue, videoparse, videosink) could not be linked.\n");
        gst_object_unref(data.pipelineElements["pipeline"]);
        return false;
    }

    return true;
}

/* This function will be called by the pad-added signal */
void Pipeline::PadAddedHandler(GstElement* src, GstPad* new_pad, Pipeline::CustomData* data)
{
    GstPadLinkReturn ret;
    GstCaps* new_pad_caps = NULL;
    GstStructure* new_pad_struct = NULL;
    const gchar* new_pad_type = NULL;
    GstPad* sink_pad = NULL;

    g_print("Received new pad '%s' from '%s':\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

    /* Check the new pad's type */
    new_pad_caps = gst_pad_get_current_caps(new_pad);
    new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
    new_pad_type = gst_structure_get_name(new_pad_struct);
    if (g_str_has_prefix(new_pad_type, "audio/mpeg"))
    {
        g_print("It has type '%s' Test it.\n", new_pad_type);
        goto test_audio;
    }
    else if (g_str_has_prefix(new_pad_type, "video/mpeg"))
    {
        g_print("It has type '%s' Test it.\n", new_pad_type);
        goto test_video;
    }
    else
    {
        g_print("It has type '%s' Something is wrong.\n", new_pad_type);
    }

    goto exit;

test_audio:
    sink_pad = gst_element_get_static_pad(data->pipelineElements["audioqueue"], "sink");
    if (gst_pad_is_linked(sink_pad))
    {
        g_print("We are already linked. Ignoring.\n");
        goto exit;
    }
    goto next_step;

test_video:
    sink_pad = gst_element_get_static_pad(data->pipelineElements["videoqueue"], "sink");
    if (gst_pad_is_linked(sink_pad))
    {
        g_print("We are already linked. Ignoring.\n");
        goto exit;
    }
    goto next_step;

next_step:

    /* Attempt the link */
    ret = gst_pad_link(new_pad, sink_pad);
    if (GST_PAD_LINK_FAILED(ret))
    {
        g_print("Type is '%s' but link failed.\n", new_pad_type);
    }
    else
    {
        g_print("Link succeeded (type '%s').\n", new_pad_type);
    }

exit:
    /* Unreference the new pad's caps, if we got them */
    if (new_pad_caps != NULL)
        gst_caps_unref(new_pad_caps);

    /* Unreference the sink pad */
    if (sink_pad != NULL)
        gst_object_unref(sink_pad);
}

/* This function will be called by the message signal */
void Pipeline::BusMessageHandler(GstBus* bus, GstMessage* msg, Pipeline::CustomData* data)
{
    switch (GST_MESSAGE_TYPE(msg))
    {
    case GST_MESSAGE_ERROR:
    {
        GError* err;
        gchar* debug;

        gst_message_parse_error(msg, &err, &debug);
        g_print("Error: %s\n", err->message);
        g_error_free(err);
        g_free(debug);

        gst_element_set_state(data->pipelineElements["pipeline"], GST_STATE_READY);
        g_main_loop_quit(data->loop);
        break;
    }
    case GST_MESSAGE_STATE_CHANGED:
    {
        /* We are only interested in state-changed messages from the pipeline */
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(data->pipelineElements["pipeline"]))
        {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
            g_print("Pipeline state changed from %s to %s:\n",
                gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));
        }
        break;
    }
    case GST_MESSAGE_EOS:
    {
        /* end-of-stream */
        gst_element_set_state(data->pipelineElements["pipeline"], GST_STATE_READY);
        g_main_loop_quit(data->loop);
        g_print("Pipeline has reached End of Stream\n");
        break;
    }
    case GST_MESSAGE_BUFFERING:
    {
        gint percent = 0;

        /* If the stream is live, we do not care about buffering. */
        if (data->isLive) break;

        gst_message_parse_buffering(msg, &percent);
        g_print("Buffering (%3d%%)\r", percent);
        /* Wait until buffering is complete before start/resume playing */
        if (percent < 100)
            gst_element_set_state(data->pipelineElements["pipeline"], GST_STATE_PAUSED);
        else
            gst_element_set_state(data->pipelineElements["pipeline"], GST_STATE_PLAYING);
        break;
    }
    case GST_MESSAGE_CLOCK_LOST:
    {
        /* Get a new clock */
        gst_element_set_state(data->pipelineElements["pipeline"], GST_STATE_PAUSED);
        gst_element_set_state(data->pipelineElements["pipeline"], GST_STATE_PLAYING);
        break;
    }
    default:
        /* Unhandled message */
        break;
    }
}