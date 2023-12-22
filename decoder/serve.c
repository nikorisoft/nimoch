#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <jansson.h>
#include "lib/framecache.h"
#include "lib/helper.h"
#include "lib/scene_detect.h"

/* Protocol */
#define NICM_SERVE_COMMAND_QUIT  0
#define NICM_SERVE_COMMAND_INFO  1
/* Image: [0]: Frame PTS / [1]: Decode options / [2]: Image options
 *   Decode options: 0 .. return only exact frame / 1 .. return the nearest frame
 *   Image options:  0 .. original size / 1 .. half size / 2 .. resized original size / 3 .. resized half size | 0 .. PNG / 4 .. JPEG
 */
#define NICM_SERVE_COMMAND_IMAGE 2

#define NICM_SERVE_COMMAND_SCENE_DETECT 256
/* Image: [0]: Base Frame PTS / [1]: Detect options / [2]: Max frames (default: 100, max: 2000) / [3]: Cutoff score
 *   Detect options: 0 .. detect forward / 1 .. detect backward
 */

#define SCENE_DETECT_MAX_FRAMES 2000
#define SCENE_DETECT_DEFAULT_FRAMES 100

struct nicm_serve_command {
    long command;
    long args[7];
};
struct nicm_serve_response {
    long code;
    long size;
};

static int serve_stream(AVFormatContext *avf_context, AVStream *stream, AVCodecContext *codec, FILE *pipe, struct file_open_options *opts);

int do_serve(const char *ts_file, const int stream, struct file_open_options *opts) {
    AVFormatContext *avf_context = NULL;
    int ret;

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
    AVCodecContext *avcc = open_decoder_for_stream(avs);
    if (!avcc) {
        fprintf(stderr, "Stream error: Failed to open the decoder for the stream");
        avformat_close_input(&avf_context);
        return 15;
    }

    ret = serve_stream(avf_context, avs, avcc, stdout, opts);

    avcodec_close(avcc);
    avcodec_free_context(&avcc);
    avformat_close_input(&avf_context);

    return ret;
}

static char *handle_info_command(AVStream *stream, long first_pts);

static int send_response(FILE *output, long code, size_t size, void *data) {
    struct nicm_serve_response response;

    response.code = code;
    response.size = size;
    if (fwrite(&response, sizeof(struct nicm_serve_response), 1, output) != 1) {
        return 1;
    }
    if (size > 0) {
        if (fwrite(data, 1, size, output) != size) {
            return 1;
        }
    }
    fflush(output);

    return 0;
}

static int send_response_json(FILE *output, long code, json_t *object) {
    char *json_str;
    int ret;

    json_str = json_dumps(object, 0);
    ret = send_response(output, code, strlen(json_str), json_str);
    free(json_str);

    return ret;
}

static struct frame *load_frame(struct framecache *cache, AVFormatContext *avf_context, AVStream *stream, AVCodecContext *codec, long pts, struct video_stream_frame_index *indices, int frames_in_indices);
static int cache_next_frame(struct framecache *cache, AVFormatContext *avf_context, AVStream *stream, AVCodecContext *codec, long min_pts, long max_pts);

#define DEFAULT_ARRAY_SIZE 120
#define SEEK_THRESHOLD 30

struct encode_configs {
    int width;
    int height;
    const AVCodec *encoder;
    AVCodecContext *encoder_context;
    struct SwsContext* sws_context;
    enum AVPixelFormat fmt;
};

static int serve_stream(AVFormatContext *avf_context, AVStream *stream, AVCodecContext *codec, FILE *pipe, struct file_open_options *opts) {
    struct nicm_serve_command cmd;
    struct framecache cache;
    long first_pts;
    long delta;

    struct encode_configs encode_configs[8];
    const AVCodec *png_codec, *jpeg_codec;

    struct video_stream_frame_index *indices = NULL;
    int frames_in_indices = 0;

    // Initialization

    // Calculate delta in time base. delta is 1 / fps [s]
    delta = stream->r_frame_rate.den * stream->time_base.den / stream->r_frame_rate.num / stream->time_base.num;
    fprintf(stderr, "delta: %ld (%ld | %ld)\n", delta, sizeof(int), sizeof(long));

    init_framecache(&cache, DEFAULT_ARRAY_SIZE, delta, SEEK_THRESHOLD,
        codec->codec_id == AV_CODEC_ID_MPEG2VIDEO ? 40 : codec->codec_id == AV_CODEC_ID_H264 ? 40 : 30
    );

    if (cache_next_frame(&cache, avf_context, stream, codec, AV_NOPTS_VALUE, AV_NOPTS_VALUE) == 0) {
        first_pts = cache.pts_range_start;
    }
    cache_next_frame(&cache, avf_context, stream, codec, AV_NOPTS_VALUE, AV_NOPTS_VALUE);

    if (cache.pts_range_end - cache.pts_range_start != delta) {
        fprintf(stderr, "*The interval between the first two frames is not delta (expecting %ld, but got %ld)\n",
                delta, cache.pts_range_end - cache.pts_range_start);
    }

    // Transform Initialization
    encode_configs[0].width = encode_configs[4].width = stream->codecpar->width;
    encode_configs[0].height = encode_configs[4].height = stream->codecpar->height;
    encode_configs[1].width = encode_configs[5].width = encode_configs[0].width / 2;
    encode_configs[1].height = encode_configs[5].height = encode_configs[0].height / 2;
    encode_configs[2].width = encode_configs[6].width = stream->codecpar->width
        * stream->codecpar->sample_aspect_ratio.num / stream->codecpar->sample_aspect_ratio.den;
    encode_configs[2].height = encode_configs[6].height = stream->codecpar->height;
    encode_configs[3].width = encode_configs[7].width = encode_configs[2].width / 2;
    encode_configs[3].height = encode_configs[7].height = encode_configs[2].height / 2;

    png_codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
    jpeg_codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);

    int i;
    for (i = 0; i < 4; i++) {
        encode_configs[i].encoder = png_codec;
        encode_configs[i].fmt = AV_PIX_FMT_RGB24;
        encode_configs[i + 4].encoder = jpeg_codec;
        encode_configs[i + 4].fmt = AV_PIX_FMT_YUVJ420P;
    }
    for (i = 0; i < 8; i++) {
        struct encode_configs *c = encode_configs + i;

        c->encoder_context = avcodec_alloc_context3(c->encoder);
        c->encoder_context->time_base = stream->time_base;
        c->encoder_context->pix_fmt = c->fmt;
        c->encoder_context->width = c->width;
        c->encoder_context->height = c->height;

        if (avcodec_open2(c->encoder_context, c->encoder, NULL) != 0) {
            fprintf(stderr, "Failed to open encoder.");
            return 1;
        }

        c->sws_context = sws_getContext(stream->codecpar->width, stream->codecpar->height, stream->codecpar->format,
            encode_configs[i].width, encode_configs[i].height, c->fmt, SWS_BILINEAR, NULL, NULL, NULL);
    }

    if (opts->seek_by_byte) {
        // Create index
        fprintf(stderr, "Seek-by-byte option is set. Creating indices...\n");
        indices = build_index_stream(avf_context, stream, codec, &frames_in_indices);
        if (!indices) {
            fprintf(stderr, "Failed to create indices.\n");
            return 1;
        }
        fprintf(stderr, "Indices created. Frames = %d\n", frames_in_indices);
    }

    // Receive Loop
    while (fread(&cmd, sizeof(struct nicm_serve_command), 1, stdin) == 1) {
        fprintf(stderr, "[Command] command = %ld (%ld, %ld, %ld)\n", cmd.command, cmd.args[0], cmd.args[1], cmd.args[2]);
        if (cmd.command == NICM_SERVE_COMMAND_QUIT) {
            fprintf(stderr, "[Quit] Quitting the server...");
            send_response(pipe, 0, 0, NULL);

            break;
        } else if (cmd.command == NICM_SERVE_COMMAND_INFO) {
            char *result = handle_info_command(stream, first_pts);
            if (result) {
                send_response(pipe, 0, strlen(result), result);
                free(result);
            } else {
                send_response(pipe, 500, 0, NULL);
            }
        } else if (cmd.command == NICM_SERVE_COMMAND_IMAGE) {
            if (cmd.args[2] < 0 || cmd.args[2] >= 8) {
                send_response(pipe, 400, 0, NULL);
            } else {
                struct frame *frame = load_frame(&cache, avf_context, stream, codec, cmd.args[0], indices, frames_in_indices);
                if (frame == NULL) {
                    fprintf(stderr, "[Image command] No frame for %ld\n", cmd.args[0]);
                    send_response(pipe, 404, 0, NULL);
                } else {
                    int imageOpt = cmd.args[2];
                    if (!frame->encoded[imageOpt]) {
                        AVFrame *new_frame = av_frame_alloc();
                        int ret;
                        struct encode_configs *c = encode_configs + imageOpt;

                        av_image_alloc(new_frame->data, new_frame->linesize, c->width, c->height, c->fmt, 16);
                        sws_scale(c->sws_context, (const uint8_t * const *)frame->avf->data, frame->avf->linesize, 0, frame->avf->height, new_frame->data, new_frame->linesize);
                        new_frame->width = c->width;
                        new_frame->height = c->height;
                        new_frame->format = c->fmt;

                        if ((ret = avcodec_send_frame(c->encoder_context, new_frame)) != 0) {
                            fprintf(stderr, "avcodec_send_frame failed: %d\n", ret);
                            av_freep(&new_frame->data[0]);
                            av_frame_free(&new_frame);
                            send_response(pipe, 500, 0, NULL);
                            continue;
                        }

                        AVPacket *packet = av_packet_alloc();
                        if ((ret = avcodec_receive_packet(c->encoder_context, packet)) != 0) {
                            fprintf(stderr, "avcodec_receive_packet failed: %d\n", ret);
                            av_freep(&new_frame->data[0]);
                            av_frame_free(&new_frame);
                            send_response(pipe, 500, 0, NULL);
                            continue;
                        }

                        frame->encoded[imageOpt] = packet;

                        av_freep(&new_frame->data[0]);
                        av_frame_free(&new_frame);
                    }
                    send_response(pipe, 0, frame->encoded[imageOpt]->size, frame->encoded[imageOpt]->data);
                }
            }
        } else if (cmd.command == NICM_SERVE_COMMAND_SCENE_DETECT) {
            int backward = (cmd.args[1] & 1) == 1;
            int max_frame = SCENE_DETECT_DEFAULT_FRAMES;
            int cut_off = MAX_SCENE_CHANGE_SCORE;
            long pts = cmd.args[0];
            int f;
            struct frame *frame = load_frame(&cache, avf_context, stream, codec, pts, indices, frames_in_indices);
            struct scene_detect_context sd;

            if (frame == NULL) {
                fprintf(stderr, "[Scene command] No frame for %ld\n", pts);
                send_response(pipe, 404, 0, NULL);
            }

            if (cmd.args[2] > 0) {
                if (cmd.args[2] > SCENE_DETECT_MAX_FRAMES) {
                    max_frame = SCENE_DETECT_MAX_FRAMES;
                } else {
                    max_frame = (int)cmd.args[2];
                }
            }

            if (cmd.args[3] > 0 && cmd.args[3] < MAX_SCENE_CHANGE_SCORE) {
                cut_off = cmd.args[3];
            }

            json_t *result = json_object();
            json_t *array = json_array();
            init_scene_detect_context(&sd, frame);

            for (f = 1; f <= max_frame; f++) {
                struct frame *new_frame = load_frame(&cache, avf_context, stream, codec,
                    backward ? pts - f * cache.delta : pts + f * cache.delta,
                    indices, frames_in_indices
                );

                if (new_frame == NULL) {
                    break;
                }

                int score = score_scene_change(&sd, new_frame);

                json_array_append_new(array, json_integer(score));

                if (score > cut_off) {
                    break;
                }
            }

            json_object_set_new(result, "scores", array);

            send_response_json(pipe, 0, result);
            json_decref(result);
        }
    }

    destroy_framecache(&cache);

    for (i = 0; i < 8; i++) {
        struct encode_configs *c = encode_configs + i;

        sws_freeContext(c->sws_context);
        avcodec_close(c->encoder_context);
        avcodec_free_context(&c->encoder_context);
    }

    if (indices) {
        free(indices);
    }

    return 0;
}

static char *handle_info_command(AVStream *stream, long first_pts) {
    json_t *root = json_object();
    char *json_str;

    json_object_set_new(root, "stream", json_integer(stream->index));

    json_t *time_base = json_object();
    json_object_set_new(time_base, "num", json_integer(stream->time_base.num));
    json_object_set_new(time_base, "den", json_integer(stream->time_base.den));

    json_object_set_new(root, "timebase", time_base);

    json_t *frame_rate = json_object();
    json_object_set_new(frame_rate, "num", json_integer(stream->r_frame_rate.num));
    json_object_set_new(frame_rate, "den", json_integer(stream->r_frame_rate.den));

    json_object_set_new(root, "fps", frame_rate);

    json_object_set_new(root, "start_time", json_integer(stream->start_time));
    json_object_set_new(root, "first_pts", json_integer(first_pts));

    json_object_set_new(root, "width", json_integer(stream->codecpar->width));
    json_object_set_new(root, "height", json_integer(stream->codecpar->height));

    json_t *aspect_ratio = json_object();
    json_object_set_new(aspect_ratio, "num", json_integer(stream->codecpar->sample_aspect_ratio.num));
    json_object_set_new(aspect_ratio, "den", json_integer(stream->codecpar->sample_aspect_ratio.den));

    json_object_set_new(root, "aspect_ratio", aspect_ratio);
    json_object_set_new(root, "duration", json_integer(stream->duration));

    json_str = json_dumps(root, 0);
    json_decref(root);

    return json_str;
}

static int cache_next_frame(struct framecache *cache, AVFormatContext *avf_context, AVStream *stream, AVCodecContext *codec,
                            long pts_min, long pts_max) {
    AVPacket packet = {};
    int ret;
    AVFrame *frame = av_frame_alloc();

    while ((ret = av_read_frame(avf_context, &packet)) == 0) {
        if (packet.stream_index != stream->index) {
            goto free_packet;
        }
        if (packet.flags & AV_PKT_FLAG_CORRUPT) {
            fprintf(stderr, "Stream #%d, dts %ld corrupted.", packet.stream_index, packet.dts);
            goto free_packet;
        }
        ret = avcodec_send_packet(codec, &packet);
        if (ret == 0) {
            ret = avcodec_receive_frame(codec, frame);
            if (ret == 0) {
                if ((pts_min == AV_NOPTS_VALUE || frame->pts >= pts_min) &&
                    (pts_max == AV_NOPTS_VALUE || frame->pts <= pts_max)) {
                    fprintf(stderr, "[cache_next_frame] PTS %ld received at %ld. Going to add to cache\n", frame->pts, packet.pos);
                    add_framecache(cache, frame);
                } else {
                    fprintf(stderr, "[cache_next_frame] PTS %ld received. Discard.\n", frame->pts);
                    av_frame_free(&frame);
                }

                av_packet_unref(&packet);
                return 0;
            } else if (ret != AVERROR(EAGAIN)) {
                fprintf(stderr, "avcodec_receive_frame() => %d\n", ret);
                return 1;
            }
        }

free_packet:
        av_packet_unref(&packet);
    }
    fprintf(stderr, "av_read_frame() => %d\n", ret);

    av_frame_free(&frame);

    return 1;
}

static struct frame *load_frame(struct framecache *cache, AVFormatContext *avf_context, AVStream *stream, AVCodecContext *codec, long pts, struct video_stream_frame_index *indices, int frames_in_indices) {
    int ret = find_in_framecache(cache, pts);
    if (ret >= 0) {
        return cache->frames + ret;
    }

    long pts_min = AV_NOPTS_VALUE;
    if (ret == -1) {
        pts_min = pts - cache->delta * cache->seek_amount;

        cache->pts_last = AV_NOPTS_VALUE;

        ret = seek_frame(avf_context, stream, pts_min, indices, frames_in_indices);
        if (ret != 0) {
            fprintf(stderr, "seek_frame returned error\n");
            return NULL;
        }
        avcodec_flush_buffers(codec);
    }

    while ((ret = cache_next_frame(cache, avf_context, stream, codec, AV_NOPTS_VALUE, AV_NOPTS_VALUE)) == 0) {
        if (cache->pts_last == pts) {
            ret = find_in_framecache(cache, pts);
            if (ret >= 0) {
                return cache->frames + ret;
            } else {
                fprintf(stderr, "???: cache->pts_last == pts (%ld) but find returned error %d\n", pts, ret);
                return NULL;
            }
        } else if (cache->pts_last > pts) {
            ret = find_nearest_frame(cache, pts);
            if (ret >= 0) {
                fprintf(stderr, "Returning the nearest frame %ld instead of %ld\n", cache->frames[ret].pts, pts);
                return cache->frames + ret;
            } else {
                fprintf(stderr, "Try to find the nearest frame, but nope\n");
                return NULL;
            }
        }
    }
    return NULL;
}
