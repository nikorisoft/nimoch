#pragma once

enum NICM_STREAM_TYPE {
    STREAM_TYPE_NONE,
    STREAM_TYPE_VIDEO,
    STREAM_TYPE_AUDIO
};

struct file_open_options {
    long analyze_duration;
    long probe_size;
    long skip_initial_bytes;

    int seek_by_byte;
};
