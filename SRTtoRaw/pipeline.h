#pragma once
#include <gst/gst.h>
#include <map>
#include <string>

class Pipeline
{
public:
    Pipeline() : m_bus(NULL), m_mainLoop(NULL) {};
    ~Pipeline();
    bool Init(int argc, char* argv[]);
    void RunLoop();
private:
    typedef std::map<std::string, GstElement*> GstElementPointers;

    /* Structure to contain all our information, so we can pass it to callbacks */
    struct CustomData
    {
        GMainLoop* loop;
        GstElementPointers pipelineElements;
        gboolean isLive;

        CustomData() { loop = NULL, isLive = FALSE; };
    };

    bool CreatePipeline(CustomData& data, GstBus*& bus, GMainLoop*& mainLoop);
    bool BuildAndLinkPipeline(CustomData& data);

    static void PadAddedHandler(GstElement* src, GstPad* pad, CustomData* data);
    static void BusMessageHandler(GstBus* bus, GstMessage* message, CustomData* data);

    GMainLoop* m_mainLoop;
    GstBus* m_bus;
    CustomData m_data;
};
