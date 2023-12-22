#pragma once
#include "framecache.h"

struct scene_detect_context {
    double last_y_sum;
};

void init_scene_detect_context(struct scene_detect_context *context, struct frame *frame);
int score_scene_change(struct scene_detect_context *context, struct frame *frame);

#define MAX_SCENE_CHANGE_SCORE 10000
