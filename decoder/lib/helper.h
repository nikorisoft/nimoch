#pragma once
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include "../nicm.h"

#define PTS_MASK ((1UL << 33) - 1)

int open_file(const char *ts_file, AVFormatContext **avf_context);
int open_file_with_opts(const char *ts_file, AVFormatContext **avf_context, const struct file_open_options *open_opts);
AVCodecContext *open_decoder_for_stream(AVStream *stream);
void print_av_error(FILE *fp, const char *prefix, int ret);

struct video_stream_frame_index {
    unsigned long pts;
    unsigned long pos;
};
struct video_stream_frame_index *build_index_stream(AVFormatContext *avf_context, AVStream *stream, AVCodecContext *codec, int *frames);
struct video_stream_frame_index *find_index(struct video_stream_frame_index *indices, int num, unsigned long pts);
struct video_stream_frame_index *nearest_earlier_index(struct video_stream_frame_index *indices, int num, unsigned long pts);

int seek_frame(AVFormatContext *avf_context, AVStream *stream, unsigned long pts, struct video_stream_frame_index *indices, int frames_in_indices);
