#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <libavutil/avutil.h>

#include "nicm.h"

enum NICM_SUBCOMMAND {
    CMD_NONE = 0,
    CMD_DETECT,
    CMD_INDEX,
    CMD_SERVE,
    CMD_DECODE,
    CMD_CHECK
};

extern int do_detect(const char *ts_file, const char *output_file, struct file_open_options *opts);
extern int do_index(const char *ts_file, const char *output_file, int stream, struct file_open_options *opts);
extern int do_serve(const char *ts_file, int stream, struct file_open_options *opts);
extern int do_decode(const char *ts_file, const int stream, const enum NICM_STREAM_TYPE stream_type, const char *output_file, unsigned long *points, const char *info_file, struct file_open_options *opts);
extern int do_check(const char *ts_file, const char *output_file);

static void usage(const char *argv0, enum NICM_SUBCOMMAND cmd) {
    fprintf(stderr, "nicm: nicd media tool (aka ntt4)\n\n");
    switch (cmd) {
        case CMD_DETECT:
            fprintf(stderr, "Usage: %s detect [options...] (Movie file)\n\n", argv0);

            fprintf(stderr, "Options:\n");
            fprintf(stderr, "    -o JSON: Specify output file\n");
            fprintf(stderr, "    -a DURATION: Set the duration (sec) for the first analysis\n");

            break;

        case CMD_INDEX:
            fprintf(stderr, "Usage: %s detect [options...] (Movie file)\n\n", argv0);

            fprintf(stderr, "Options:\n");
            fprintf(stderr, "    -o JSON: Specify output file\n");
            fprintf(stderr, "    -s STREAM: Video stream\n");
            fprintf(stderr, "    -l DURATION: Set the duration (sec) for the first analysis\n");

            break;

        case CMD_SERVE:
            fprintf(stderr, "Usage: %s serve [options...] (Movie file)\n\n", argv0);

            fprintf(stderr, "Options:\n");
            fprintf(stderr, "    -s STREAM: Video stream\n");
            fprintf(stderr, "    -l DURATION: Set the duration (sec) for the first analysis\n");
            fprintf(stderr, "    -b: Seek a frame by byte\n");

            break;

        case CMD_DECODE:
            fprintf(stderr, "Usage: %s decode (-v|-a|-s STREAM) [options...] (Movie file) (PTS...)\n\n", argv0);

            fprintf(stderr, "Options:\n");
            fprintf(stderr, "    -s STREAM: Stream to decode\n");
            fprintf(stderr, "    -v: Decode video stream\n");
            fprintf(stderr, "    -a: Decode audio stream\n");
            fprintf(stderr, "    -o FILE: Specify output file\n");
            fprintf(stderr, "    -g SEGMENT: Specify information file (audio only)\n");
            fprintf(stderr, "    -l DURATION: Set the duration (sec) for the first analysis\n");
            fprintf(stderr, "    -b: Seek a frame by byte\n");

            break;

        case CMD_CHECK:
            fprintf(stderr, "Usage: %s check [options...] [(Movie file)]\n\n", argv0);

            fprintf(stderr, "Options:\n");
            fprintf(stderr, "    -o FILE: Output filename\n");

            break;

        default:
            fprintf(stderr, "Usage: %s (command)\n\n", argv0);

            fprintf(stderr, "Commands:\n");
            fprintf(stderr, "    detect (TS file)\n");
            fprintf(stderr, "    index (TS file)\n");
            fprintf(stderr, "    serve (TS file)\n");
            fprintf(stderr, "    decode (-v|-a|-s STREAM) (Movie file) (PTS...)\n");
            break;
    }
}

int main(int argc, char **argv) {
    struct file_open_options file_opts = {};

    if (argc < 2) {
        usage(argv[0], CMD_NONE);
        return 1;
    }

    // Ignore the subcommand for parsing
    optind = 2;

    if (!strcmp(argv[1], "detect")) {
        // Subcommand: detect
        int index, ret;
        const char *output_file = NULL;
        const char *ts_file = NULL;
        const struct option detect_opts[] = {
            {
                .name = "output",
                .has_arg = required_argument,
                .val = 'o'
            },
            {
                .name = "help",
                .has_arg = no_argument,
                .val = 'h'
            },
            {
                .name = "analysis-duration",
                .has_arg = required_argument,
                .val = 'l'
            }
        };

        while ((ret = getopt_long(argc, argv, "o:h?l:", detect_opts, &index)) > 0) {
            if (ret == 'o') {
                output_file = optarg;
            } else if (ret == 'h' || ret == '?') {
                usage(argv[0], CMD_DETECT);
                return 1;
            } else if (ret == 'l') {
                file_opts.analyze_duration = atol(optarg) * 1000 * 1000;
            }
        }
        if (optind >= argc) {
            fprintf(stderr, "Error: No TS file is specified.\n");
            usage(argv[0], CMD_DETECT);

            return 1;
        }
        ts_file = argv[optind];

        return do_detect(ts_file, output_file, &file_opts);
    } else if (!strcmp(argv[1], "index")) {
        // Subcommand: index
        int index, ret;
        const char *output_file = NULL;
        const char *ts_file = NULL;
        int stream = -1;

        const struct option index_opts[] = {
            {
                .name = "output",
                .has_arg = required_argument,
                .val = 'o'
            },
            {
                .name = "stream",
                .has_arg = required_argument,
                .val = 's'
            },
            {
                .name = "help",
                .has_arg = no_argument,
                .val = 'h'
            },
            {
                .name = "analysis-duration",
                .has_arg = required_argument,
                .val = 'l'
            }
        };

        while ((ret = getopt_long(argc, argv, "o:s:h?l:", index_opts, &index)) > 0) {
            if (ret == 'o') {
                output_file = optarg;
            } else if (ret == 's') {
                stream = atoi(optarg);
            } else if (ret == 'h' || ret == '?') {
                usage(argv[0], CMD_INDEX);
                return 1;
            } else if (ret == 'l') {
               file_opts.analyze_duration = atol(optarg) * 1000 * 1000;
            }
        }
        if (optind >= argc) {
            fprintf(stderr, "Error: No TS file is specified.\n");
            usage(argv[0], CMD_INDEX);

            return 1;
        }
        ts_file = argv[optind];

        return do_index(ts_file, output_file, stream, &file_opts);
    } else if (!strcmp(argv[1], "serve")) {
        // Subcommand: serve
        int index, ret;
        const char *ts_file = NULL;
        int stream = -1;

        const struct option serve_opts[] = {
            {
                .name = "stream",
                .has_arg = required_argument,
                .val = 's'
            },
            {
                .name = "help",
                .has_arg = no_argument,
                .val = 'h'
            },
            {
                .name = "analysis-duration",
                .has_arg = required_argument,
                .val = 'l'
            },
            {
                .name = "seek-by-byte",
                .has_arg = no_argument,
                .val = 'b'
            }
        };

        while ((ret = getopt_long(argc, argv, "s:h?l:b", serve_opts, &index)) > 0) {
            if (ret == 's') {
                stream = atoi(optarg);
            } else if (ret == 'h' || ret == '?') {
                usage(argv[0], CMD_SERVE);
                return 1;
            } else if (ret == 'l') {
                file_opts.analyze_duration = atol(optarg) * 1000 * 1000;
            } else if (ret == 'b') {
                file_opts.seek_by_byte = 1;
            }
        }
        if (optind >= argc) {
            fprintf(stderr, "Error: No TS file is specified.\n");
            usage(argv[0], CMD_SERVE);

            return 1;
        }
        ts_file = argv[optind];

        return do_serve(ts_file, stream, &file_opts);
    } else if (!strcmp(argv[1], "decode")) {
        // Subcommand: decode
        int index, ret;
        const char *ts_file = NULL;
        char *output_file = NULL;
        char *info_file = NULL;
        int stream = -1;
        int stream_type = STREAM_TYPE_NONE;
        unsigned long *points = NULL;

        const struct option decode_opts[] = {
            {
                .name = "stream",
                .has_arg = required_argument,
                .val = 's'
            },
            {
                .name = "video",
                .has_arg = no_argument,
                .val = 'v'
            },
            {
                .name = "audio",
                .has_arg = no_argument,
                .val = 'a'
            },
            {
                .name = "output",
                .has_arg = required_argument,
                .val = 'o'
            },
            {
                .name = "segment",
                .has_arg = required_argument,
                .val = 'g'
            },
            {
                .name = "help",
                .has_arg = no_argument,
                .val = 'h'
            },
            {
                .name = "analysis-duration",
                .has_arg = required_argument,
                .val = 'l'
            },
            {
                .name = "seek-by-byte",
                .has_arg = no_argument,
                .val = 'b'
            }
        };

        while ((ret = getopt_long(argc, argv, "s:o:avh?g:l:b", decode_opts, &index)) > 0) {
            if (ret == 's') {
                stream = atoi(optarg);
            } else if (ret == 'v') {
                stream_type = STREAM_TYPE_VIDEO;
            } else if (ret == 'a') {
                stream_type = STREAM_TYPE_AUDIO;
            } else if (ret == 'o') {
                output_file = optarg;
            } else if (ret == 'g') {
                info_file = optarg;
            } else if (ret == 'h' || ret == '?') {
                usage(argv[0], CMD_DECODE);
                return 1;
            } else if (ret == 'l') {
                file_opts.analyze_duration = atol(optarg) * 1000 * 1000;
            } else if (ret == 'b') {
                file_opts.seek_by_byte = 1;
            }
        }

        if (stream_type == STREAM_TYPE_NONE && stream == -1) {
            fprintf(stderr, "Error: Stream type or stream number should be specified.\n");
            usage(argv[0], CMD_DECODE);

            return 1;
        }
        if (optind >= argc) {
            fprintf(stderr, "Error: No TS file is specified.\n");
            usage(argv[0], CMD_DECODE);

            return 1;
        }
        ts_file = argv[optind];

        optind++;
        if (((argc - optind) & 1)) {
            fprintf(stderr, "Error: Odd number of cut points are specified.");
            usage(argv[0], CMD_DECODE);

            return 1;
        }
        if (argc - optind > 0) {
            points = calloc(sizeof(points[0]), argc - optind + 1);
            for (index = optind; index < argc; index++) {
                points[index - optind] = atol(argv[index]);
            }
            points[index - optind] = AV_NOPTS_VALUE;
        }

        ret = do_decode(ts_file, stream, stream_type, output_file, points, info_file, &file_opts);
        free(points);

        return ret;
    } else if (!strcmp(argv[1], "check")) {
        const char *ts_file = NULL, *output_file = NULL;
        int ret;
        int index;

        const struct option check_opts[] = {
            {
                .name = "help",
                .has_arg = no_argument,
                .val = 'h'
            },
            {
                .name = "output",
                .has_arg = required_argument,
                .val = 'o'
            }
        };

        while ((ret = getopt_long(argc, argv, "o:h?", check_opts, &index)) > 0) {
            if (ret == 'o') {
                output_file = optarg;
            } else if (ret == 'h' || ret == '?') {
                usage(argv[0], CMD_CHECK);
                return 1;
            }
        }
        if (optind < argc) {
            ts_file = argv[optind];
        }

        ret = do_check(ts_file, output_file);

        return ret;
    } else {
        fprintf(stderr, "Error: Unknown command '%s'\n", argv[1]);
        usage(argv[0], CMD_NONE);

        return 1;
    }

    return 0;
}
