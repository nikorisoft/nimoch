#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <jansson.h>

#define TS_PACKET_SIZE 188
#define TS_SYNC_BYTE   0x47
#define TS_PID_COUNT   0x2000

static int perform_check(FILE *input, json_t *result);

int do_check(const char *ts_file, const char *output_file) {
    FILE *input, *output;

    if (ts_file == NULL) {
        input = stdin;
    } else {
        input = fopen(ts_file, "rb");
        if (!input) {
            fprintf(stderr, "Cannot open %s for input.", ts_file);
            return 1;
        }
    }
    if (output_file == NULL) {
        output = stdout;
    } else {
        output = fopen(output_file, "w");
        if (!output) {
            fprintf(stderr, "Cannot open %s for input.", ts_file);

            fclose(input);

            return 1;
        }
    }

    json_t *result = json_object();

    perform_check(input, result);

    char *string = json_dumps(result, 0);
    fprintf(output, "%s", string);

    free(string);
    json_decref(result);

    return 0;
}

struct pid_info {
    int total;
    int dropped;
    int scrambled;

    int next_counter;
};

static int perform_check(FILE *input, json_t *result) {
    uint8_t packet[TS_PACKET_SIZE];
    struct pid_info pid_info[TS_PID_COUNT] = {};

    int error_packet = 0;

    while (fread(packet, 1, TS_PACKET_SIZE, input) == TS_PACKET_SIZE) {
        if (packet[0] != TS_SYNC_BYTE) {
            error_packet++;
            continue;
        }
        int pid = (((int)packet[1] & 0x1f) << 8) | packet[2];

        if (pid < 0 || pid > 0x1fff) {
            fprintf(stderr, "Error: error in the logic.\n");
            continue;
        }

        pid_info[pid].total++;

        if (packet[3] & 0x10) { // has payload
            int continuity_counter = (packet[3] & 0x0f);
            if (pid_info[pid].next_counter != 0 && 
                (pid_info[pid].next_counter & 0x0f) != continuity_counter) {
                pid_info[pid].dropped++;
            }

            pid_info[pid].next_counter = continuity_counter + 1;
        }

        if (packet[3] & 0xc0) {
            pid_info[pid].scrambled++;
        }
    }

    int i;
    for (i = 0; i < TS_PID_COUNT; i++) {
        const struct pid_info *pinfo = pid_info + i;
        char num[16];

        if (pinfo->total > 0) {
            json_t *info = json_object();
            json_object_set_new(info, "total", json_integer(pinfo->total));
            json_object_set_new(info, "dropped", json_integer(pinfo->dropped));
            json_object_set_new(info, "scrambled", json_integer(pinfo->scrambled));

            snprintf(num, sizeof(num), "%x", i);
            json_object_set_new(result, num, info);
        }
    }

    return 0;
}