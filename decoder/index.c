#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <jansson.h>
#include "lib/helper.h"

static json_t *process_stream(AVFormatContext *format, AVStream *stream);

int do_index(const char *ts_file, const char *output_file, int stream, struct file_open_options *opts) {
    AVFormatContext *avf_context = NULL;
    int ret;
    json_t *result;
    FILE *fp_output;

    ret = open_file_with_opts(ts_file, &avf_context, opts);
    if (ret < 0) {
        fprintf(stderr, "Error: avformat_open_input returned %d\n", ret);
        return 10;
    }

    ret = avformat_find_stream_info(avf_context, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error: avformat_find_stream_info returned %d\n", ret);
        avformat_close_input(&avf_context);
        return 11;
    }

    AVStream *avs = NULL;
    if (stream >= 0) {
        if ((unsigned int)stream < avf_context->nb_streams) {
            avs = avf_context->streams[stream];
            if (avs->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
                fprintf(stderr, "Error: Stream %d found but not video.\n", ret);
                avformat_close_input(&avf_context);
                return 12;
            }
            // OK.
        } else {
            fprintf(stderr, "Error: Stream index %d is out of bound.\n", ret);
            avformat_close_input(&avf_context);
            return 13;
        }
    } else { // Find a video stream
        unsigned int i;

        // Choose the first one
        for (i = 0; i < avf_context->nb_streams; i++) {
            if (!avs &&
                avf_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                avf_context->streams[i]->start_time != AV_NOPTS_VALUE) {
                avs = avf_context->streams[i];
            }
        }
        if (!avs) {
            fprintf(stderr, "Error: No suitable video stream found.\n");
            avformat_close_input(&avf_context);
            return 14;
        }
    }

    if (output_file) {
        fp_output = fopen(output_file, "w");
        if (!fp_output) {
            fprintf(stderr, "Error: cannot open the output file \"%s\"\n", output_file);
            avformat_close_input(&avf_context);
            return 11;
        }
    } else {
        fp_output = stdout;
    }

    ret = 0;
    result = process_stream(avf_context, avs);

    if (result) {
        char *output_string = json_dumps(result, 0);
        fprintf(fp_output, "%s", output_string);
        free(output_string);

        json_decref(result);
    } else {
        fprintf(stderr, "Error: Processing the stream failed.\n");
        ret = 15;
    }

    if (output_file) {
        fclose(fp_output);
    }
    avformat_close_input(&avf_context);
    return ret;
}

#define MAX_REF_FRAMES 60

struct frame_info {
    unsigned long pts;
    unsigned long pos;
};

static int compare_frame_info(const void *a, const void *b) {
    const struct frame_info *fa = a, *fb = b;

    return fa->pts - fb->pts;
}

static void flush_frames(struct frame_info *frames, int *num_frames, json_t *frames_array) {
    // flush the frames buffer
    qsort(frames, *num_frames, sizeof(*frames), compare_frame_info);

    int i;
    for (i = 0; i < *num_frames; i++) {
        struct frame_info *f = frames + i;

        json_t *o = json_object();
        json_object_set_new(o, "pts", json_integer(f->pts));
        json_object_set_new(o, "pos", json_integer(f->pos));

        json_array_append_new(frames_array, o);
    }

    *num_frames = 0;
}

static json_t *process_stream(AVFormatContext *format, AVStream *stream) {
    json_t *root = NULL;

    AVCodecContext *avcc = open_decoder_for_stream(stream);
    if (!avcc) {
        fprintf(stderr, "Stream error: Failed to open the decoder for the stream");
        return root;
    }

    AVPacket *packet;
    int ret;
    long first_key_frame_pts = AV_NOPTS_VALUE;

    struct frame_info prev_frames[MAX_REF_FRAMES];
    int num_prev_frames = 0;

    json_t *frames = json_array();
    root = json_object();

    packet = av_packet_alloc();
    while ((ret = av_read_frame(format, packet)) == 0) {
        if (packet->stream_index != stream->index) {
            goto free_packet;
        }
        if (first_key_frame_pts == AV_NOPTS_VALUE) {
            if (packet->flags & AV_PKT_FLAG_KEY) {
                first_key_frame_pts = packet->pts;
            } else {
                goto free_packet;
            }
        }
        // first_key_frame_pts must be valid at this point.
        if (packet->pts < first_key_frame_pts) {
            goto free_packet;
        }
        if (packet->flags & AV_PKT_FLAG_KEY) {
            flush_frames(prev_frames, &num_prev_frames, frames);
        }
        if (num_prev_frames < MAX_REF_FRAMES) {
            prev_frames[num_prev_frames].pts = packet->pts;
            prev_frames[num_prev_frames].pos = packet->pos;

            num_prev_frames++;
        } else {
            fprintf(stderr, "Warning: Frame buffer overflowed\n");
        }

free_packet:
        av_packet_unref(packet);
    }
    // Do not flush frames after the last key frame
    av_packet_free(&packet);

    int num_frames = json_array_size(frames);
    long first_frame_pts = json_integer_value(json_array_get(frames, 0));
    long last_frame_pts = json_integer_value(json_array_get(frames, num_frames - 1));

    avcodec_close(avcc);
    avcodec_free_context(&avcc);

    json_object_set_new(root, "frames", frames);
    json_object_set_new(root, "stream", json_integer(stream->index));

    json_t *time_base = json_object();
    json_object_set_new(time_base, "num", json_integer(stream->time_base.num));
    json_object_set_new(time_base, "den", json_integer(stream->time_base.den));

    json_object_set_new(root, "timebase", time_base);

    char buf[64];
    json_t *info = json_object();
    json_object_set_new(info, "num_frames", json_integer(num_frames));
    snprintf(buf, sizeof(buf), "%.3f", (double)(num_frames - 1) * (double)stream->time_base.den / (double)(last_frame_pts - first_frame_pts) / (double)stream->time_base.num);
    json_object_set_new(info, "fps", json_string(buf));
    json_object_set_new(root, "info", info);

    return root;
}
