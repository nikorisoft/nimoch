#include "helper.h"
#include "../nicm.h"

const struct file_open_options DEFAULT_OPTS = {
    .analyze_duration = (30L * 1000 * 1000), // 30 sec
    .probe_size = (100UL << 20), // 100 MB
    .skip_initial_bytes = 0
};

static char *safe_av_dict_get(AVDictionary *opts, const char *key) {
    AVDictionaryEntry *entry = av_dict_get(opts, key, NULL, 0);

    if (entry != NULL) {
        return entry->value;
    }
    return NULL;
}

int open_file_with_opts(const char *ts_file, AVFormatContext **avf_context, const struct file_open_options *open_opts) {
    int ret;
    char *input = calloc(1, strlen(ts_file) + 6);
    AVDictionary *opts = NULL;

    if (open_opts == NULL) {
        open_opts = &DEFAULT_OPTS;
    }

    snprintf(input, strlen(ts_file) + 6, "file:%s", ts_file);

    av_dict_set_int(&opts, "probesize", (open_opts->probe_size == 0 ? DEFAULT_OPTS.probe_size : open_opts->probe_size), 0);
    av_dict_set_int(&opts, "analyzeduration", (open_opts->analyze_duration == 0 ? DEFAULT_OPTS.analyze_duration : open_opts->analyze_duration), 0);
    av_dict_set_int(&opts, "skip_initial_bytes", open_opts->skip_initial_bytes, 0);

    fprintf(stderr, "open_file: probesize = %s, analyze_duration = %s, skip_initial_bytes = %s\n",
        safe_av_dict_get(opts, "probesize"), safe_av_dict_get(opts, "analyzeduration"), safe_av_dict_get(opts, "skip_initial_bytes")
    );

    ret = avformat_open_input(avf_context, input, NULL, &opts);

    free(input);
    av_dict_free(&opts);

    return ret;
}

int open_file(const char *ts_file, AVFormatContext **avf_context) {
    return open_file_with_opts(ts_file, avf_context, NULL);
}

AVCodecContext *open_decoder_for_stream(AVStream *stream) {
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    AVCodecContext *context = avcodec_alloc_context3(codec);

    avcodec_parameters_to_context(context, stream->codecpar);
    avcodec_open2(context, codec, NULL);

    return context;
}

void print_av_error(FILE *fp, const char *prefix, int ret) {
    char err[1024];

    av_strerror(ret, err, sizeof(err));

    fprintf(fp, "%s: %s\n", prefix, err);
}

#define ALLOC_FRAMES 10240

struct video_stream_frame_index *build_index_stream(AVFormatContext *avf_context, AVStream *stream, AVCodecContext *codec, int *frames) {
    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    int ret;

    struct video_stream_frame_index *indices = calloc(sizeof(*indices), ALLOC_FRAMES);
    int num_indices = 0;
    int num_buf = ALLOC_FRAMES;

    while ((ret = av_read_frame(avf_context, packet)) == 0) {
        if (packet->stream_index != stream->index) {
            goto free_packet;
        }
        if (packet->flags & AV_PKT_FLAG_CORRUPT) {
            goto free_packet;
        }

        ret = avcodec_send_packet(codec, packet);
        if (ret == 0) {
            ret = avcodec_receive_frame(codec, frame);
            if (ret == 0) {
                if (num_buf >= num_indices) {
                    num_buf += ALLOC_FRAMES;
                    indices = realloc(indices, sizeof(*indices) * num_buf);
                }

                indices[num_indices].pts = frame->pts;
                indices[num_indices].pos = packet->pos;

                num_indices++;

                av_frame_unref(frame);
            } else if (ret != AVERROR(EAGAIN)) {
                av_packet_free(&packet);
                av_frame_free(&frame);

                free(indices);

                return NULL;
            }
        }

free_packet:
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    av_frame_free(&frame);

    *frames = num_indices;
    return indices;
}

static int compare_stream_frame_index(const void *a, const void *b) {
    const struct video_stream_frame_index *ia = a, *ib = b;

    return ia->pts - ib->pts;
}

struct video_stream_frame_index *find_index(struct video_stream_frame_index *indices, int num, unsigned long pts) {
    struct video_stream_frame_index key = { .pts = pts };

    return bsearch(&key, indices, num, sizeof(*indices), compare_stream_frame_index);
}

struct video_stream_frame_index *nearest_earlier_index(struct video_stream_frame_index *indices, int num, unsigned long pts) {
    // should do binary search, but...
    int i;

    if (indices[0].pts > pts) {
        return indices;
    }

    for (i = 1; i < num; i++) {
        if (indices[i].pts > pts) {
            break;
        }
    }

    return indices + i - 1;
}

int seek_frame(AVFormatContext *avf_context, AVStream *stream, unsigned long pts, struct video_stream_frame_index *indices, int frames_in_indices) {
    int ret;

    if (indices) {
        struct video_stream_frame_index *index = find_index(indices, frames_in_indices, pts);
        if (!index) {
            index = nearest_earlier_index(indices, frames_in_indices, pts);
            if (!index) {
                fprintf(stderr, "Failed to find the index for %ld\n", pts);
                return -1;
            }
            fprintf(stderr, "Use non-exact match for %ld\n", pts);
        }

        fprintf(stderr, "Found the index: pts = %ld, pos = %ld\n", index->pts, index->pos);
        if ((ret = av_seek_frame(avf_context, stream->index, index->pos, AVSEEK_FLAG_BYTE)) < 0) {
            fprintf(stderr, "av_seek_frame() = %d\n", ret);
            return ret;
        }
    } else {
        if ((ret = av_seek_frame(avf_context, stream->index, pts, AVSEEK_FLAG_BACKWARD)) < 0) {
            fprintf(stderr, "av_seek_frame() = %d\n", ret);
            return ret;
        }
    }

    return 0;
}
