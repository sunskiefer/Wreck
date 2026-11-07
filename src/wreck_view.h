/* Wreck's buffer strip: the waveform of the 20 s buffer with a moving playhead (the write head) and a thin line per sounding
 * voice, drawn by the plugin on a spare hardware plane of the Force's display, over a box the skin leaves empty.
 * The DRM plane code, the finding of MPC's page in its picture (any screen rotation) and the buffers come from L'Cronx's
 * Pseudo X/Y pad (itself built on VADER 0.5.0's overlay, MIT); the drawing is Wreck's own. No touch: it only shows. */
#pragma once
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>

#define OVL_TILES_W 1280
#define OVL_TILES_H 628
#define VW_COLS 1232
#define VW_ROWS 84
#define VW_VOICES 16

typedef struct {
    volatile float peak[VW_COLS];      /* 0..1 per column: the loudest sample in that 1/1232 of the 20 s buffer */
    volatile float wpos;               /* 0..1: the write head (the playhead) */
    volatile float vpos[VW_VOICES];    /* 0..1: each sounding voice's read head, -1 = silent */
    volatile int ver;                  /* counts every visible change (the thread redraws when it moves) */
} wview_data_t;

typedef struct { uint32_t handle, pitch, fb_id; uint64_t size; uint8_t *map; } ovl_buf_t;

typedef struct {
    char log_path[700], dir[600];
    int started, stop, live, waited_logged;
    pthread_t thread;
    int fd, crtc_index;
    uint32_t crtc_id, plane_id, format, disp_w, disp_h;
    uint64_t zpos_max;
    int has_zpos;
    ovl_buf_t buf[2];
    int back, fb_slot;
    int box_found, box_logged, box_x, box_y, on_screen, fmt_unknown, fb_logged, fb_err_logged, no_vblank;
    double ms_avg;
    int pad_x, pad_y, bw, bh;          /* the strip's box in page pixels (data/layout.txt: "view x y w h") */
    int have_page, rot, lw, lh, pw, ph, bufd, pl_x, pl_y;
    unsigned fb_w, fb_h, geom;
    wview_data_t *data;
} overlay_t;

void view_start(overlay_t *o, const char *data_dir, wview_data_t *data);
void view_stop(overlay_t *o);
/* offline: one frame of the strip into out (bw * bh * 4, premultiplied ARGB little-endian) */
void view_render(const wview_data_t *d, int bw, int bh, uint8_t *out);
