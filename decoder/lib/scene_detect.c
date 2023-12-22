#include <math.h>
#include "scene_detect.h"

static double calc_frame_sum(AVFrame *avf) {
    int w, h;
    unsigned char *y = avf->data[0];
    double sum = 0;

    for (h = 0; h < avf->height; h++) {
        unsigned char *data = y + avf->linesize[0] * h;
        for (w = 0; w < avf->width; w++) {
            sum += data[w];
        }
    }

    return sum;
}

void init_scene_detect_context(struct scene_detect_context *context, struct frame *frame) {
    context->last_y_sum = calc_frame_sum(frame->avf);
}

int score_scene_change(struct scene_detect_context *context, struct frame *frame) {
    double sum = context->last_y_sum;
    double new_sum = calc_frame_sum(frame->avf);
    int score = (int)(fabs(new_sum - sum) / sum * (double)MAX_SCENE_CHANGE_SCORE);

    context->last_y_sum = new_sum;

    return score;
}
