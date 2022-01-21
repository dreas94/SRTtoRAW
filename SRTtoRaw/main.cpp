#include "pipeline.h"

int main(int argc, char* argv[])
{
    /*Use the following command line in CMD: ffmpeg -stream_loop -1 -re -i D:\gsttest\bbb_sunflower_1080p_60fps_normal.mp4-f mpegts "srt://127.0.0.1:1234?mode=caller&pkt_size=1316"*/
    Pipeline* pipeline = new Pipeline();

    if (!pipeline->Init(argc, argv))
        return -1;

    pipeline->RunLoop();

    delete pipeline;
    return 0;
}