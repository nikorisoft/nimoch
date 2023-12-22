#include "nicm.h"
#include "lib/helper.h"
#include <libswresample/swresample.h>
#include <jansson.h>

int decode_stream_video(AVFormatContext *format, AVStream *stream, AVCodecContext *codec, FILE *output, const long *points, struct file_open_options *opts);
int decode_stream_audio(AVFormatContext *format, AVStream *stream, AVCodecContext *codec, FILE *output, const long *points, json_t *data_info, struct file_open_options *opts);

int do_decode(const char *ts_file, const int stream, const enum NICM_STREAM_TYPE stream_type, const char *output_file, const long *points, const char *info_file, struct file_open_options *opts) {
    AVFormatContext *avf_context = NULL;
    int ret;
    FILE *fp_output;
    AVStream *avs = NULL;
    enum AVMediaType type;

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

    if (stream >= 0) {
        if ((unsigned int)stream < avf_context->nb_streams) {
            avs = avf_context->streams[stream];
            if (avs->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                type = AVMEDIA_TYPE_VIDEO;
            } else if (avs->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                type = AVMEDIA_TYPE_AUDIO;
            } else {
                fprintf(stderr, "Error: Stream %d found but is not either video or audio (%d).\n", ret, avs->codecpar->codec_type);
                avformat_close_input(&avf_context);
                return 12;
            }
        } else {
            fprintf(stderr, "Error: Stream index %d is out of bound.\n", ret);
            avformat_close_input(&avf_context);
            return 13;
        }
    } else if (stream_type == STREAM_TYPE_VIDEO || stream_type == STREAM_TYPE_AUDIO) { // Find a specified stream
        if (stream_type == STREAM_TYPE_VIDEO) {
            type = AVMEDIA_TYPE_VIDEO;
        } else {
            type = AVMEDIA_TYPE_AUDIO;
        }

        // Choose the first one
        unsigned int i;

        for (i = 0; i < avf_context->nb_streams; i++) {
            if (!avs &&
                avf_context->streams[i]->codecpar->codec_type == type &&
                avf_context->streams[i]->start_time != AV_NOPTS_VALUE) {
                avs = avf_context->streams[i];
            }
        }
        if (!avs) {
            fprintf(stderr, "Error: No suitable stream found.\n");
            avformat_close_input(&avf_context);
            return 14;
        }
    } else {
        fprintf(stderr, "Error: Invalid option. Should not happen\n");
        avformat_close_input(&avf_context);
        return 15;
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

    FILE *fp_info = stderr;
    if (info_file) {
        fp_info = fopen(info_file, "w");
        if (!fp_info) {
            fprintf(stderr, "Error: cannot open the information output file \"%s\"\n", info_file);
            avformat_close_input(&avf_context);
            return 20;
        }
    }

    fprintf(stderr, "Decoding stream #%d (type = %d)\n", avs->index, type);

    AVCodecContext *avcc = open_decoder_for_stream(avs);
    if (!avcc) {
        fprintf(stderr, "Stream error: Failed to open the decoder for the stream");
        avformat_close_input(&avf_context);
        return 15;
    }

    if (type == AVMEDIA_TYPE_VIDEO) {
        ret = decode_stream_video(avf_context, avs, avcc, fp_output, points, opts);
    } else {
        json_t *data_info = json_array();

        ret = decode_stream_audio(avf_context, avs, avcc, fp_output, points, data_info, opts);

        char *str = json_dumps(data_info, 0);
        fprintf(fp_info, "%s", str);
        free(str);

        json_decref(data_info);
    }

    fclose(fp_output);

    avcodec_close(avcc);
    avcodec_free_context(&avcc);
    avformat_close_input(&avf_context);

    return ret;
}

static int decode_common(AVFormatContext *format, AVStream *stream, AVCodecContext *codec, AVFrame *frame) {
    AVPacket *packet = av_packet_alloc();
    int ret;

    while ((ret = av_read_frame(format, packet)) == 0) {
        if (packet->stream_index != stream->index || (packet->flags & AV_PKT_FLAG_CORRUPT)) {
            av_packet_unref(packet);
            continue;
        }

        ret = avcodec_send_packet(codec, packet);
        av_packet_unref(packet);

        if (ret == 0) {
            ret = avcodec_receive_frame(codec, frame);
            if (ret == 0) {
                break;
            } else if (ret != AVERROR(EAGAIN)) {
                fprintf(stderr, "avcodec_receive_frame() = %d\n", ret);
                break;
            }
        }
    }

    av_packet_free(&packet);

    return ret;
}


int decode_stream_video(AVFormatContext *format, AVStream *stream, AVCodecContext *codec, FILE *output, const long *points, struct file_open_options *opts) {
    int ret;
    AVFrame *frame = av_frame_alloc();
    int frames = 0;
    int index = 0;
    const int DELTA = stream->time_base.den * 2 / stream->time_base.num;
    // CFR
    const AVRational frame_rate = stream->r_frame_rate;
    const int time_per_frame = frame_rate.den * stream->time_base.den / frame_rate.num / stream->time_base.num;
    struct video_stream_frame_index *indices = NULL;
    int frames_in_indices = 0;

    if (stream->codecpar->format != AV_PIX_FMT_YUV420P) {
        fprintf(stderr, "Error: Pixel format unknown: %d\n", stream->codecpar->format);

        ret = -EINVAL;
        goto fin;
    }

    if (opts->seek_by_byte) {
        indices = build_index_stream(format, stream, codec, &frames_in_indices);
    }

    fprintf(output, "YUV4MPEG2 W%d H%d F%d:%d It A%d:%d C420\n",
        stream->codecpar->width, stream->codecpar->height, frame_rate.num, frame_rate.den,
        stream->codecpar->sample_aspect_ratio.num, stream->codecpar->sample_aspect_ratio.den);

    do {
        long start, end;
        if (!points) {
            start = AV_NOPTS_VALUE;
            end = AV_NOPTS_VALUE;
        } else {
            start = points[index];
            end = points[index + 1];
        }

        if (start != AV_NOPTS_VALUE && (start - DELTA) > stream->start_time) {
            ret = seek_frame(format, stream, start - DELTA, indices, frames_in_indices);
            if (ret != 0) {
                fprintf(stderr, "seek_frame returned error\n");
                goto fin;
            }

            avcodec_flush_buffers(codec);
        }

        long pts = start;

        while (end == AV_NOPTS_VALUE || pts < end) {
            ret = decode_common(format, stream, codec, frame);
            if (ret != 0) {
                break;
            }
            if (pts == AV_NOPTS_VALUE) {
                pts = frame->pts;
            }
            while (pts >= frame->pts && pts < frame->pts + frame->duration) {
                fprintf(output, "FRAME\n");
                int y;
                for (y = 0; y < frame->height; y++) {
                    fwrite(frame->data[0] + y * frame->linesize[0], 1, frame->width, output);
                }
                for (y = 0; y < frame->height / 2; y++) {
                    fwrite(frame->data[1] + y * frame->linesize[1], 1, frame->width / 2, output);
                }
                for (y = 0; y < frame->height / 2; y++) {
                    fwrite(frame->data[2] + y * frame->linesize[2], 1, frame->width / 2, output);
                }

                frames++;
                pts += time_per_frame;
            }
            av_frame_unref(frame);
        }

        index += 2;
    } while (points && points[index] != AV_NOPTS_VALUE);

fin:
    av_frame_free(&frame);
    free(indices);

    fprintf(stderr, "Processed %d frames\n", frames);

    return 0;
}

static inline unsigned long pts_to_sample(unsigned long pts, AVRational *time_base, int sample_rate) {
    return pts * time_base->num * sample_rate / time_base->den;
}

static inline unsigned long sample_to_pts(long samples, AVRational *time_base, int sample_rate) {
    return samples * time_base->den / sample_rate / time_base->num;
}

static void append_segment_info(json_t *data_info, unsigned long start, unsigned long end, int channels, const AVChannelLayout *layout, int sample_rate, const char *format, int num_frames) {
    json_t *segment = json_object();
    char layout_str[32];

    snprintf(layout_str, sizeof(layout_str), "%lx", layout->u.mask);
    json_object_set_new(segment, "start", json_integer(start));
    json_object_set_new(segment, "end", json_integer(end));
    json_object_set_new(segment, "layout", json_string(layout_str));
    json_object_set_new(segment, "channels", json_integer(channels));
    json_object_set_new(segment, "format", json_string(format));
    json_object_set_new(segment, "sampleRate", json_integer(sample_rate));
    json_object_set_new(segment, "frames", json_integer(num_frames));

    json_array_append_new(data_info, segment);
}

int decode_stream_audio(AVFormatContext *format, AVStream *stream, AVCodecContext *codec, FILE *output, const long *points, json_t *data_info, struct file_open_options *opts) {
    int ret;
    AVFrame *frame = av_frame_alloc();
    int frames = 0, last_frames = 0;
    unsigned long samples = 0;
    int index = 0;
    const int DELTA = stream->time_base.den * 1 / stream->time_base.num;

    struct SwrContext *swr_context;

    AVChannelLayout output_channel_layout = {};
    int output_channels;
    int output_sample_rate = 48000;
    int output_sample_byte = 2;
    enum AVSampleFormat output_format = AV_SAMPLE_FMT_S16;

    unsigned long prev_samples_start = 0;

    struct video_stream_frame_index *indices = NULL;
    int frames_in_indices = 0;

    output_channels = stream->codecpar->ch_layout.nb_channels;

    av_channel_layout_copy(&output_channel_layout, &stream->codecpar->ch_layout);

    if ((ret = swr_alloc_set_opts2(&swr_context,
            &output_channel_layout, output_format, output_sample_rate,
            &stream->codecpar->ch_layout, stream->codecpar->format, stream->codecpar->sample_rate,
            0, NULL) != 0)) {
        fprintf(stderr, "swr_alloc_set_opts2() = %d\n", ret);
        goto fin;
    }

    if ((ret = swr_init(swr_context)) != 0) {
        fprintf(stderr, "swr_init() = %d\n", ret);
        goto fin;
    }

    if (opts->seek_by_byte) {
        indices = build_index_stream(format, stream, codec, &frames_in_indices);
    }

    unsigned long output_in_pts = 0;

    do {
        long start, end;
        unsigned long samples_to_write = 0;

        if (!points) {
            start = AV_NOPTS_VALUE;
            end = AV_NOPTS_VALUE;
        } else {
            start = points[index];
            end = points[index + 1];
            // The number of samples to be written by the end of this range (from the beginning of this decoding)
            samples_to_write = pts_to_sample((end - start) + output_in_pts, &stream->time_base, output_sample_rate);
        }
        fprintf(stderr, "Starting the range #%d (%ld -> %ld)\n", index / 2, start, end);

        if (start != AV_NOPTS_VALUE && (start - DELTA) > stream->start_time) {
            if ((ret = seek_frame(format, stream, start - DELTA, indices, frames_in_indices)) < 0) {
                fprintf(stderr, "av_seek_frame() = %d\n", ret);
                goto fin;
            }
            avcodec_flush_buffers(codec);
        }

        int first_decode = 1;
        long final_pts = AV_NOPTS_VALUE;
        long decoded_pts = AV_NOPTS_VALUE;

        unsigned char last_sample[8 * output_sample_byte];
        memset(last_sample, 0, sizeof(last_sample));

        while ((ret = decode_common(format, stream, codec, frame)) == 0) {
            // Fix up duration
            long duration = frame->duration;
            long samples_duration = sample_to_pts(frame->nb_samples, &stream->time_base, frame->sample_rate);

            if (duration > samples_duration) {
                fprintf(stderr, "*FIXUP: duration %ld -> %ld (# of samples: %d)\n", duration, samples_duration, frame->nb_samples);
                duration = samples_duration;
            }

            // Skip frames out of the range
            int sample_start, sample_end, ch;

            if (start != AV_NOPTS_VALUE && frame->pts + duration <= start) {
                av_frame_unref(frame);
                continue;
            }
            if (end != AV_NOPTS_VALUE && frame->pts >= end) {
                av_frame_unref(frame);
                break;
            }

            // Some decoder returns a duplicate frame
            if (final_pts != AV_NOPTS_VALUE && final_pts == frame->pts) {
                fprintf(stderr, "*DUP %ld - %ld (%d samples)\n", frame->pts, frame->pts + duration, frame->nb_samples);
                av_frame_unref(frame);
                continue;
            }
            if (decoded_pts != AV_NOPTS_VALUE && decoded_pts != frame->pts) {
                fprintf(stderr, "Gap in the original data detected: (Last %ld - %ld, received %ld)\n", final_pts, decoded_pts, frame->pts);
            }
            if (final_pts > frame->pts || decoded_pts > frame->pts) {
                fprintf(stderr, "ERROR: Audio packets do not appear in order. Cannot process this stream.\n");
                fprintf(stderr, "    Stream %d: Final PTS = %ld, Decoded PTS = %ld, Received PTS = %ld\n", stream->index, final_pts, decoded_pts, frame->pts);
                ret = 1;

                goto fin;
            }
            final_pts = frame->pts;

            sample_start = 0;
            sample_end = frame->nb_samples;

            if (start != AV_NOPTS_VALUE && frame->pts < start) { // partial
                sample_start = pts_to_sample(start - frame->pts, &stream->time_base, frame->sample_rate);
            }
            if (end != AV_NOPTS_VALUE && frame->pts + duration > end) {
                sample_end = pts_to_sample(end - frame->pts, &stream->time_base, frame->sample_rate);
                if (sample_end == 0) { // Less than 1 frame
                    av_frame_unref(frame);
                    continue;
                }
                fprintf(stderr, "PTS: %ld -> %ld | end = %ld\n", frame->pts, duration, end);
                fprintf(stderr, "  sample_end = %d / num_samples = %ld\n", sample_end, samples_to_write - samples);
            }

            if (frame->ch_layout.nb_channels < 1) {
                fprintf(stderr, "Invalid channel detected: %d\n", frame->ch_layout.nb_channels);
                goto fin;
            }

            if (frame->ch_layout.nb_channels != output_channels || av_channel_layout_compare(&output_channel_layout, &frame->ch_layout)) {
                fprintf(stderr, "Channel change detected: %d <%lx> -> %d <%lx>\n", output_channels, output_channel_layout.u.mask, frame->ch_layout.nb_channels, frame->ch_layout.u.mask);
                if (prev_samples_start < samples) {
                    append_segment_info(data_info, prev_samples_start, samples, output_channels, &output_channel_layout, output_sample_rate, "S16", frames - last_frames);
                    prev_samples_start = samples;
                }
                last_frames = frames;

                av_channel_layout_copy(&output_channel_layout, &frame->ch_layout);
                output_channels = frame->ch_layout.nb_channels;

                swr_alloc_set_opts2(&swr_context,
                    &output_channel_layout, output_format, output_sample_rate,
                    &frame->ch_layout, frame->format, frame->sample_rate,
                    0, NULL);

                if ((ret = swr_init(swr_context)) != 0) {
                    fprintf(stderr, "swr_init() = %d\n", ret);
                    goto fin;
                }
            }
            if (sample_end < sample_start) {
                fprintf(stderr, "No enough sample data is in the frame. (Start: %d, End: %d)\n", sample_start, sample_end);
                fprintf(stderr, "    Frame duration: %ld, Samples: %d (equivalent to %ld)\n", frame->duration, frame->nb_samples, samples_duration);
                ret = 1;

                goto fin;
            }

            int output_samples = av_rescale_rnd(sample_end - sample_start, frame->sample_rate, output_sample_rate, AV_ROUND_UP);
            uint8_t *output_data;
            const uint8_t *input[8] = {};

            for (ch = 0; ch < frame->ch_layout.nb_channels; ch++) {
                input[ch] = (uint8_t *)((float *)(frame->data[ch]) + sample_start);
            }

            if ((ret = av_samples_alloc(&output_data, NULL, output_channels, output_samples, output_format, 1)) < 0) {
                fprintf(stderr, "Original buffer: %d / PTS : %ld\n", frame->nb_samples, frame->pts);
                fprintf(stderr, "Failed to allocate output sample (%d samples) (%d)\n", output_samples, ret);

                av_frame_unref(frame);
                goto fin;
            }

            ret = swr_convert(swr_context, &output_data, output_samples, input, sample_end - sample_start);

            if (first_decode){
                if (start != AV_NOPTS_VALUE && start < frame->pts) {
                    int i, gap_samples;

                    gap_samples = pts_to_sample(frame->pts - start, &stream->time_base, output_sample_rate);
                    fprintf(stderr, "*Need to fill in the gap (Start: %ld, First Frame PTS: %ld) for %d samples\n", start, frame->pts, gap_samples);

                    for (i = 0; i < gap_samples; i++) {
                        if (fwrite(output_data, output_channels * output_sample_byte, 1, output) != 1) {
                            fprintf(stderr, "Failed to write the gap data\n");
                        }
                    }
                    samples += gap_samples;
                }
                first_decode = 0;
            }

            if (ret > 0) {
                if (fwrite(output_data, output_channels * output_sample_byte, ret, output) != (size_t)ret) {
                    fprintf(stderr, "Failed to write the output data\n");
                    goto fin;
                } else {
                    samples += ret;

                    memcpy(last_sample, (output_data + output_channels * output_sample_byte * (ret - 1)), output_channels * output_sample_byte);
                }
            }

            decoded_pts = frame->pts + duration;

            av_freep(&output_data);
            av_frame_unref(frame);
            frames++;

            if (ret < 0) {
                fprintf(stderr, "Failed: swr_convert() = %d\n", ret);
                goto fin;
            }
        }
        fprintf(stderr, "Finished the range #%d (%d frames, %ld/%ld samples) (Last PTS: %ld)\n", index / 2, frames, samples, samples_to_write, final_pts);
        index += 2;

        if (samples < samples_to_write) {
            unsigned long i;

            fprintf(stderr, "Filling in the gap (%ld frames)\n", samples_to_write - samples);
            for (i = 0; i < (samples_to_write - samples); i++) {
                if (fwrite(last_sample, output_channels * output_sample_byte, 1, output) != 1) {
                    fprintf(stderr, "Failed to write the gap data\n");
                    goto fin;
                }
            }

            samples += samples_to_write - samples;
        }

        if (start != AV_NOPTS_VALUE && end != AV_NOPTS_VALUE) {
            output_in_pts += (end - start);
        }
    } while (points && points[index] != AV_NOPTS_VALUE);

fin:
    swr_free(&swr_context);
    av_frame_free(&frame);

    fprintf(stderr, "Processed %d frames and wrote %ld samples\n", frames, samples);

    append_segment_info(data_info, prev_samples_start, samples, output_channels, &output_channel_layout, output_sample_rate, "S16", frames - last_frames);

    av_channel_layout_uninit(&output_channel_layout);

    free(indices);

    if (ret != 0) {
        if (ret == AVERROR_EOF) {
            ret = 0;
        } else {
            print_av_error(stderr, "Error during decoding", ret);
        }
    }
    return ret;
}
