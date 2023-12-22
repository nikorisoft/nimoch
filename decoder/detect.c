#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <jansson.h>
#include "lib/helper.h"

static json_t *compose_result(AVFormatContext *avf_context);
static const char *type_table[] = {"video", "audio", "data", "subtitle", "attachment", "none"};

int do_detect(const char *ts_file, const char *output_file, struct file_open_options *opts) {
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

    // OK, now analyze the stream and compose the result
    result = compose_result(avf_context);

    char *output_string = json_dumps(result, 0);
    fprintf(fp_output, "%s", output_string);
    free(output_string);

    json_decref(result);

    if (output_file) {
        fclose(fp_output);
    }
    avformat_close_input(&avf_context);
    return 0;
}

static json_t *compose_result(AVFormatContext *avf_context) {
    json_t *result = json_object();
    unsigned int i;

    json_object_set_new(result, "video", json_array());
    json_object_set_new(result, "audio", json_array());
    json_object_set_new(result, "subtitle", json_array());

    for (i = 0; i < avf_context->nb_streams; i++) {
        AVStream *avs = avf_context->streams[i];

        if (avs->codecpar->codec_type != AVMEDIA_TYPE_VIDEO && avs->codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            avs->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            continue;
        }

        // Sanity check for the stream
        if (avs->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (avs->codecpar->ch_layout.nb_channels == 0) {
                fprintf(stderr, "Audio stream %d: Ch (%d) is invalid. Ignored\n", i, avs->codecpar->ch_layout.nb_channels);
                continue;
            }
        } else if (avs->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (avs->codecpar->width == 0 || avs->codecpar->height == 0) {
                fprintf(stderr, "Video stream %d: Width (%d) or height (%d) is invalid. Ignored\n", i, avs->codecpar->width, avs->codecpar->height);
                continue;
            }
        }

        json_t *stream = json_object();
        json_object_set_new(stream, "index", json_integer(i));
        if (avs->start_time == AV_NOPTS_VALUE) {
            json_object_set_new(stream, "pts", json_null());
        } else {
            json_object_set_new(stream, "pts", json_integer(avs->start_time));
        }

        json_t *time_base = json_object();
        json_object_set_new(time_base, "num", json_integer(avs->time_base.num));
        json_object_set_new(time_base, "den", json_integer(avs->time_base.den));

        json_object_set_new(stream, "timebase", time_base);

        if (avs->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            json_t *frame_rate = json_object();
            json_object_set_new(frame_rate, "num", json_integer(avs->r_frame_rate.num));
            json_object_set_new(frame_rate, "den", json_integer(avs->r_frame_rate.den));

            json_object_set_new(stream, "fps", frame_rate);
        }

        json_object_set_new(stream, "pid", json_integer(avs->id));
        json_object_set_new(stream, "codec", json_string(avcodec_get_name(avs->codecpar->codec_id)));

        if (avs->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            json_t *video = json_object();

            json_object_set_new(video, "width", json_integer(avs->codecpar->width));
            json_object_set_new(video, "height", json_integer(avs->codecpar->height));
            json_object_set_new(video, "format", json_integer(avs->codecpar->format));

            json_object_set_new(stream, "video", video);
        } else if (avs->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            json_t *audio = json_object();
            char format[64];

            av_channel_layout_describe(&avs->codecpar->ch_layout, format, sizeof(format));

            json_object_set_new(audio, "channels", json_integer(avs->codecpar->ch_layout.nb_channels));
            json_object_set_new(audio, "layout", json_string(format));

            json_object_set_new(stream, "audio", audio);
        }

        const char *type_str = type_table[avs->codecpar->codec_type];
        json_t *stream_array = json_object_get(result, type_str);
        if (stream_array) {
            json_array_append_new(stream_array, stream);
        } else {
            json_decref(stream);
        }
    }

    return result;
}
