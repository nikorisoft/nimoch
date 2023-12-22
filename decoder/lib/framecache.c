#include <libavformat/avformat.h>
#include "framecache.h"

void init_framecache(struct framecache *cache, int first_array_size, long delta, int seek_threshold, int seek_amount) {
    cache->frames = calloc(sizeof(struct frame), first_array_size);
    cache->num_allocated_frames = first_array_size;
    cache->num_frames = 0;
    cache->pts_range_end = AV_NOPTS_VALUE;
    cache->pts_range_start = AV_NOPTS_VALUE;
    cache->pts_last = AV_NOPTS_VALUE;

    cache->delta = delta;
    cache->seek_threshold = seek_threshold;
    cache->seek_amount = seek_amount;
}

static void __destroy_frame(struct frame *frame) {
    unsigned int i;

    for (i = 0; i < sizeof(frame->encoded) / sizeof(frame->encoded[0]); i++) {
        if (frame->encoded[i]) {
            av_packet_free(&frame->encoded[i]);
        }
    }

    av_frame_free(&frame->avf);
}

void destroy_framecache(struct framecache *cache) {
    int i;

    if (cache->frames) {
        for (i = 0; i < cache->num_frames; i++) {
            __destroy_frame(cache->frames + i);
        }
        free(cache->frames);
        cache->frames = NULL;
    }
    cache->num_frames = 0;
    cache->pts_range_end = AV_NOPTS_VALUE;
    cache->pts_range_start = AV_NOPTS_VALUE;
    cache->pts_last = AV_NOPTS_VALUE;
    cache->num_allocated_frames = 0;
}

static void __recalculate_pts_range(struct framecache *cache) {
    int i;

    cache->pts_range_start = AV_NOPTS_VALUE;
    cache->pts_range_end = AV_NOPTS_VALUE;

    for (i = 0; i < cache->num_frames; i++) {
        long pts = cache->frames[i].pts;
        if (cache->pts_range_start == AV_NOPTS_VALUE || cache->pts_range_start > pts) {
            cache->pts_range_start = pts;
        }
        if (cache->pts_range_end == AV_NOPTS_VALUE || cache->pts_range_end < pts) {
            cache->pts_range_end = pts;
        }
    }
}

int add_framecache(struct framecache *cache, AVFrame *frame) {
    if (cache->num_frames == cache->num_allocated_frames) {
        //
        int i, slide = cache->num_allocated_frames / 4;
        for (i = 0; i < slide; i++) {
            __destroy_frame(cache->frames + i);
        }
        memmove(cache->frames, cache->frames + slide, sizeof(cache->frames[0]) * (cache->num_frames - slide));

        cache->num_frames -= slide;

        __recalculate_pts_range(cache);
    }
    struct frame *fc_frame = cache->frames + cache->num_frames;
    unsigned int i;

    fc_frame->avf = frame;
    fc_frame->pts = frame->pts;
    for (i = 0; i < sizeof(fc_frame->encoded) / sizeof(fc_frame->encoded[0]); i++) {
        fc_frame->encoded[i] = NULL;
    }

    cache->num_frames++;
    if (cache->pts_range_start == AV_NOPTS_VALUE || cache->pts_range_start > frame->pts) {
        cache->pts_range_start = frame->pts;
    }
    if (cache->pts_range_end == AV_NOPTS_VALUE || cache->pts_range_end < frame->pts) {
        cache->pts_range_end = frame->pts;
    }
    cache->pts_last = frame->pts;

    return 0;
}

/**
 * >= 0 : Found in index
 * -1 : Not in cache. Seek it.
 * -2 : Not in cache. Continue to decode.
 */
int find_in_framecache(struct framecache *cache, long pts) {
    int i;

    if (pts >= cache->pts_range_start && pts <= cache->pts_range_end) {
        for (i = 0; i < cache->num_frames; i++) {
            if (cache->frames[i].pts == pts) {
                return i;
            }
        }
    }
    if ((pts - cache->pts_last) > 0 && (pts - cache->pts_last) < cache->delta * cache->seek_threshold) {
        return -2;
    }
    return -1;
}

/**
 * @brief Find the nearest frame
 * 
 * @param cache 
 * @param pts 
 * @return int The index in the cache (>= 0), not in the range (negative)
 */
int find_nearest_frame(struct framecache *cache, long pts) {
    int i;

    if (pts >= cache->pts_range_start && pts <= cache->pts_range_end) {
        for (i = 0; i < cache->num_frames; i++) {
            long frame_pts = cache->frames[i].pts;
            long frame_duration = cache->frames[i].avf->duration;

            if (frame_pts <= pts && frame_pts + frame_duration > pts) {
                return i;
            }
        }
    }

    return -1;
}
