#pragma once
#include <libavcodec/avcodec.h>

struct frame {
    long pts;
    AVFrame *avf;
    AVPacket *encoded[8];
};

struct framecache {
    long pts_range_start;
    long pts_range_end;
    long pts_last;
    int num_frames;
    int num_allocated_frames;
    struct frame *frames;
    // configurations
    long delta;
    int seek_threshold;
    int seek_amount;
};

void init_framecache(struct framecache *cache, int first_array_size, long delta, int seek_threshold, int seek_amount);
void destroy_framecache(struct framecache *cache);
int add_framecache(struct framecache *cache, AVFrame *frame);
int find_in_framecache(struct framecache *cache, long pts);
int find_nearest_frame(struct framecache *cache, long pts);
