/* Wreck (L'Cronx): a self-sampling instrument for the Akai Force / MPC OS.
 *
 * A source feeds a chain of five blocks (drive, echo, filter, shift, smear; their order is yours) and a limiter. The result is
 * recorded all the time into a 20 second circular buffer. MIDI notes are voices that play that buffer back (speed, pitch,
 * direction, envelope); what they play can be fed back into the chain's input. Behind the voices sits an output bus (bouncing-ball
 * delay, lo-fi, stutter) and an arp with polymetric lanes that plays the voices itself (MPC ignores MIDI a VST sends out).
 * Contract: wrapper/engine.h (44100 Hz, int16 stereo, 1..128 frames per render with SAMPLE_ACCURATE, song_pos with HAS_SONG_POS).
 * No allocation, I/O or waiting in render(); files (WAV, user presets) are handled by a worker thread. */
#define _GNU_SOURCE
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "engine.h"
#include "wparams.h"
#include "wreck_view.h"

#define SR 44100
#define SRF 44100.0f
#define BUFN (SR * 20)
#define MAXV 16
#define PI_F 3.14159265f
#define TAU 6.28318531f
#define MAXWAV (SR * 30)
#define MAXUSER 4096

/* the Force's glibc is older than powf@GLIBC_2.27: powf is our own (exp2f and log2f carry old symbol versions) */
static inline float wr_powf(float a, float b) { return a <= 0.0f ? 0.0f : exp2f(b * log2f(a)); }
#define powf(a, b) wr_powf((a), (b))

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

/* ---------------------------------------------------------------------------------------------- parameter maps (also used for the text) */
static float map_rate(float v) { return exp2f((v - 0.5f) * 8.0f); }          /* 1/16 .. 16 x */
static float map_speed(float v) { return exp2f((v - 0.5f) * 4.0f); }         /* 1/4 .. 4 x */
static float map_ms(float v) { return powf(2000.0f, v); }                    /* 1 .. 2000 ms */
static float map_hz(float v) { return 20.0f * powf(900.0f, v); }             /* 20 Hz .. 18 kHz */
static float map_time(float v) { return 10.0f * powf(150.0f, v); }           /* 10 ms .. 1.5 s */
static float map_lofi(float v) { return 1500.0f * powf(SRF / 1500.0f, v); }  /* 1.5 kHz .. 44.1 kHz */
static float lvl(float v) { return v < 0.005f ? 0.0f : exp2f((v - 0.75f) * 8.0f); }   /* 0.75 = 0 dB, 1 = +12 dB */

static const char *const NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/* ---------------------------------------------------------------------------------------------- random */
static inline uint32_t rnd32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static inline float rndf(uint32_t *s) { return (float)(rnd32(s) >> 8) * (1.0f / 8388608.0f) - 1.0f; }   /* -1 .. 1 */
static inline float rnd01(uint32_t *s) { return (float)(rnd32(s) >> 8) * (1.0f / 16777216.0f); }

static inline float fsoft(float x) {   /* tanh-like, cheap */
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* ---------------------------------------------------------------------------------------------- state */
typedef struct { float *l, *r; int n; } wav_t;

typedef struct {
    int on, key, arp, stage;            /* stage 0 attack, 1 sustain, 2 release */
    uint32_t age;
    float vel, env, atk_inc, rel_dec, fin, pan;
    double pos;                         /* absolute read position in the buffer */
    double speed;
    int dir;
    int off_at;                         /* arp: samples until its note-off (-1 none) */
} voice_t;

typedef struct {
    char key[160], name[64];
} ent_t;

typedef struct {
    float v[NP];                        /* natural-unit values */
    char data_dir[600];
    uint32_t rng;
    /* worker */
    pthread_t th; pthread_mutex_t mu; pthread_cond_t cv; int quit, started;
    int req_scan, req_wav, req_save, req_user;
    float save_v[NP]; char save_wav[64];
    char *pend_text;                    /* a user preset's text, handed to render */
    char wavnames[64][64]; int nwav;
    char usernames[MAXUSER][24]; int nuser;
    wav_t wavs[2]; volatile int wav_act; volatile int wav_busy;
    int wav_loaded_idx;
    volatile int rev;                   /* display_rev */
    char logp[700];
    /* buffer */
    float *bl, *br; int wpos; float pk_acc; int pk_col;
    wview_data_t view; overlay_t ov;
    /* source */
    double ph[8]; float sx[8]; float gate_env; double wavpos; int heldcount, last_note;
    float src_ping_env, src_ping_idx, src_ping_fc, src_ping_fm; double ping_ph, ping_phm; float ping_t, ping_pan;
    float ck_l, ck_r; float cl_x, cl_y, cl_x1, cl_y1, cl_t; int cl_n;
    float att[3], attn[3], attmax[3], attdc[3]; int att_sys;
    float lp_n[2];
    float hg_t, hg_a[4], hg_f[4], hg_p[4], hg_d[2];
    float sf_m, sf_max; float sp_max, sf_ph;
    /* chain */
    float lp_b1[2][2], hp_b1[2]; float echo_l[2 * SR], echo_r[2 * SR]; int echo_w; float echo_dl[2], echo_lp[2];
    float svf_low[2], svf_band[2], env_f;
    float sh_l[8192], sh_r[8192]; int sh_w; float sh_p, sh_fb[2];
    float rv_c[2][4][2048]; float rv_ap[2][2][1024]; int rv_w; float rv_lp[2][4]; float rv_len[4], rv_size_s;
    float lim_g, lim_meter;
    float tap_l[7], tap_r[7]; float prev_vl, prev_vr;
    /* voices */
    voice_t vc[MAXV]; uint32_t vage; float bend, modw;
    /* arp */
    int held[16], nheld, phys_down; int played[16], nplayed;
    double beat, bpm, steplen; long last_k; int arp_idx, arp_dir; int transport;
    int arp_was_on;
    /* output bus */
    float ball_l[6 * SR], ball_r[6 * SR]; int ball_w;
    float lofi_acc, lofi_hl, lofi_hr, lofi_ll, lofi_lr;
    float st_l[2 * SR], st_r[2 * SR]; int st_w; float fz_l[SR], fz_r[SR]; int fz_n, fz_i, st_active;
    float dc_xl, dc_yl, dc_xr, dc_yr;
    /* smoothed macro state */
    float chaos_r[3], chaos_t[3];
    /* effective, smoothed block parameters */
    float e_cut, e_dl, e_drv;
} wr_t;

/* ---------------------------------------------------------------------------------------------- display text */
static void fmt_value(wr_t *s, int i, float v, char *buf, int n) {
    const wp_t *p = &WP[i];
    switch (p->fmt) {
    case F_PCT: snprintf(buf, n, "%d %%", (int)lroundf(v * 100)); break;
    case F_BIPCT: snprintf(buf, n, "%+d %%", (int)lroundf(v * 100)); break;
    case F_RATE: snprintf(buf, n, "%.2f X", map_rate(v)); break;
    case F_SPEEDX: snprintf(buf, n, "%.2f X", map_speed(v)); break;
    case F_ST: snprintf(buf, n, "%+d ST", (int)lroundf(v)); break;
    case F_CENTS: snprintf(buf, n, "%+d CT", (int)lroundf(v)); break;
    case F_MS: { float m = map_ms(v); if (m >= 1000) snprintf(buf, n, "%.2f S", m / 1000); else snprintf(buf, n, "%d MS", (int)lroundf(m)); break; }
    case F_HZ: { float h = map_hz(v); if (h >= 1000) snprintf(buf, n, "%.1f K", h / 1000); else snprintf(buf, n, "%d HZ", (int)lroundf(h)); break; }
    case F_TIME: { float m = map_time(v); if (m >= 1000) snprintf(buf, n, "%.2f S", m / 1000); else snprintf(buf, n, "%d MS", (int)lroundf(m)); break; }
    case F_DB: { float g = lvl(v); if (g <= 0) snprintf(buf, n, "-INF"); else snprintf(buf, n, "%+.1f DB", 20 * log10f(g)); break; }
    case F_LOFI: snprintf(buf, n, "%.2f K", map_lofi(v) / 1000); break;
    case F_STEPS: snprintf(buf, n, "%d", (int)lroundf(v)); break;
    case F_CHIP: {                                  /* the chain strip: the level of each stage, OFF for a block that is off */
        int c = i - P_chip0;
        if (c == 0) snprintf(buf, n, "%d", (int)lroundf(s->v[P_src_level] * 100));
        else if (c >= 1 && c <= 5) {
            if (s->v[P_b1_on + (c - 1)] < 0.5f) snprintf(buf, n, "OFF");
            else {
                static const int lv[5] = {P_b1_level, P_b2_mix, P_b3_level, P_b4_level, P_b5_level};
                snprintf(buf, n, "%d", (int)lroundf(s->v[lv[c - 1]] * 100));
            }
        } else snprintf(buf, n, "%d", (int)lroundf(s->v[P_level] * 100));
        break;
    }
    case F_INT: snprintf(buf, n, "%d", (int)lroundf(v)); break;
    case F_WINFO: if (s->nwav > 0) snprintf(buf, n, "%d FILE%s", s->nwav, s->nwav == 1 ? "" : "S"); else snprintf(buf, n, "NO FILES"); break;
    case F_NAME:
        if (i == P_wav) {
            int k = (int)lroundf(v);
            if (k >= 0 && k < s->nwav) snprintf(buf, n, "%s", s->wavnames[k]); else snprintf(buf, n, "NO FILE");
        } else {                                    /* the preset page */
            int k = (int)lroundf(s->v[P_p_sel]);
            if (s->v[P_p_bank] < 0.5f) snprintf(buf, n, "%02d %s", k + 1, k >= 0 && k < 22 ? FACT[k].name : "-");
            else if (k >= 0 && k < s->nuser) snprintf(buf, n, "%s", s->usernames[k]);
            else snprintf(buf, n, "NO USER");
        }
        break;
    default: snprintf(buf, n, "%g", v); break;
    }
}

/* ---------------------------------------------------------------------------------------------- log */
static void wlog(wr_t *s, const char *fmt, ...) {
    if (!s->logp[0]) return;
    FILE *f = fopen(s->logp, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(f, "[engine] ");
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    va_end(ap);
    fclose(f);
}

/* ---------------------------------------------------------------------------------------------- WAV */
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int wav_read(const char *path, wav_t *w) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 44 || len > 200L * 1024 * 1024) { fclose(f); return -1; }
    uint8_t *b = malloc((size_t)len);
    if (!b || fread(b, 1, (size_t)len, f) != (size_t)len) { free(b); fclose(f); return -1; }
    fclose(f);
    if (memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) { free(b); return -1; }
    long p = 12;
    int fmt = 0, ch = 0, bits = 0, rate = 0;
    const uint8_t *data = NULL;
    long dlen = 0;
    while (p + 8 <= len) {
        uint32_t cl = rd32(b + p + 4);
        if (!memcmp(b + p, "fmt ", 4) && p + 8 + 16 <= len) {
            fmt = rd16(b + p + 8); ch = rd16(b + p + 10); rate = (int)rd32(b + p + 12); bits = rd16(b + p + 22);
            if (fmt == 0xFFFE && cl >= 26) fmt = rd16(b + p + 8 + 24);
        } else if (!memcmp(b + p, "data", 4)) {
            data = b + p + 8;
            dlen = (long)cl;
            if (p + 8 + dlen > len) dlen = len - p - 8;
            break;
        }
        p += 8 + (long)cl + (cl & 1);
    }
    if (!data || ch < 1 || ch > 8 || rate < 1000 || !((fmt == 1 && (bits == 16 || bits == 24 || bits == 32 || bits == 8)) || (fmt == 3 && bits == 32))) { free(b); return -1; }
    int bps = bits / 8;
    long frames = dlen / (bps * ch);
    if (frames < 16) { free(b); return -1; }
    double ratio = (double)rate / SR;
    long outn = (long)((double)frames / ratio);
    if (outn > MAXWAV) outn = MAXWAV;
    w->l = malloc((size_t)outn * sizeof(float));
    w->r = malloc((size_t)outn * sizeof(float));
    if (!w->l || !w->r) { free(w->l); free(w->r); w->l = w->r = NULL; free(b); return -1; }
    for (long i = 0; i < outn; i++) {
        double sp = (double)i * ratio;
        long i0 = (long)sp;
        float fr = (float)(sp - (double)i0);
        long i1 = i0 + 1 < frames ? i0 + 1 : i0;
        float a[2] = {0, 0};
        for (int side = 0; side < 2; side++) {
            float s0, s1;
            for (int k = 0; k < 2; k++) {
                long fi = k ? i1 : i0;
                const uint8_t *q = data + (fi * ch + (side && ch > 1 ? 1 : 0)) * bps;
                float v;
                if (fmt == 3) { uint32_t u = rd32(q); memcpy(&v, &u, 4); }
                else if (bits == 16) v = (int16_t)rd16(q) / 32768.0f;
                else if (bits == 24) v = (int32_t)((uint32_t)q[0] << 8 | (uint32_t)q[1] << 16 | (uint32_t)q[2] << 24) / 2147483648.0f;
                else if (bits == 32) v = (int32_t)rd32(q) / 2147483648.0f;
                else v = ((int)q[0] - 128) / 128.0f;
                if (k) s1 = v; else s0 = v;
            }
            a[side] = s0 + (s1 - s0) * fr;
        }
        w->l[i] = a[0]; w->r[i] = a[1];
    }
    w->n = (int)outn;
    free(b);
    return 0;
}

static int has_wav_ext(const char *n) {
    size_t l = strlen(n);
    return l > 4 && !strcasecmp(n + l - 4, ".wav");
}

static int cmp_names(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }

/* ---------------------------------------------------------------------------------------------- params in / out */
static void apply_defaults(wr_t *s, int keep_presets) {
    for (int i = 0; i < NP; i++) {
        if (keep_presets && (i == P_p_bank || i == P_p_sel)) continue;
        if (WP[i].fmt == F_TRIG) continue;
        s->v[i] = WP[i].def;
    }
    /* the arp lane patterns are slots whose default is in the table already */
}

static int key_index(const char *k) {
    for (int i = 0; i < NP; i++) if (!strcmp(WP[i].key, k)) return i;
    return -1;
}

static void set_idx(wr_t *s, int i, float v) {
    if (i < 0 || i >= NP) return;
    v = clampf(v, WP[i].lo, WP[i].hi);
    if (WP[i].fmt == F_OPT || WP[i].fmt == F_INT || WP[i].fmt == F_ST || WP[i].fmt == F_CENTS || WP[i].fmt == F_STEPS || WP[i].fmt == F_NAME || WP[i].fmt == F_SLOT) v = floorf(v + 0.5f);
    s->v[i] = v;
}

/* a user preset's text: "key=value" lines */
static void apply_text(wr_t *s, const char *t) {
    apply_defaults(s, 1);
    char line[256];
    const char *p = t;
    while (*p) {
        size_t n = strcspn(p, "\n");
        if (n < sizeof line) {
            memcpy(line, p, n);
            line[n] = 0;
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = 0;
                int k = key_index(line);
                if (k >= 0 && k != P_p_bank && k != P_p_sel) set_idx(s, k, (float)atof(eq + 1));
                else if (!strcmp(line, "wavname")) {
                    for (int w = 0; w < s->nwav; w++) if (!strcmp(s->wavnames[w], eq + 1)) { s->v[P_wav] = (float)w; break; }
                }
            }
        }
        p += n;
        if (*p == '\n') p++;
    }
    __atomic_add_fetch(&s->rev, 1, __ATOMIC_RELEASE);
}

static void load_factory(wr_t *s, int idx) {
    if (idx < 0 || idx >= 22) return;
    apply_defaults(s, 1);
    for (int i = 0; i < FACT[idx].n; i++) set_idx(s, FACT[idx].v[i].p, FACT[idx].v[i].v);
    __atomic_add_fetch(&s->rev, 1, __ATOMIC_RELEASE);
}

/* ---------------------------------------------------------------------------------------------- worker thread */
static void scan_all(wr_t *s) {
    char dir[700];
    snprintf(dir, sizeof dir, "%s/wav", s->data_dir);
    DIR *d = opendir(dir);
    int n = 0;
    char (*tmp)[64] = calloc(64, 64);
    if (!tmp) return;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && n < 64) {
            if (has_wav_ext(e->d_name) && strlen(e->d_name) < 63) { snprintf(tmp[n], 64, "%s", e->d_name); n++; }
        }
        closedir(d);
    }
    qsort(tmp, (size_t)n, 64, cmp_names);
    memcpy(s->wavnames, tmp, sizeof(char) * 64 * (size_t)n);
    __atomic_store_n(&s->nwav, n, __ATOMIC_RELEASE);
    snprintf(dir, sizeof dir, "%s/user", s->data_dir);
    d = opendir(dir);
    int m = 0;
    char (*ut)[24] = calloc(MAXUSER, 24);
    if (!ut) { free(tmp); return; }
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && m < MAXUSER) {
            size_t l = strlen(e->d_name);
            if (l > 6 && l < 23 && !strcmp(e->d_name + l - 6, ".wreck")) { snprintf(ut[m], 24, "%.*s", (int)(l - 6), e->d_name); m++; }
        }
        closedir(d);
    }
    qsort(ut, (size_t)m, 24, cmp_names);
    memcpy(s->usernames, ut, 24 * (size_t)m);
    __atomic_store_n(&s->nuser, m, __ATOMIC_RELEASE);
    __atomic_add_fetch(&s->rev, 1, __ATOMIC_RELEASE);
    free(tmp); free(ut);
    wlog(s, "scan: %d WAV files, %d user presets", n, m);
}

static void worker_load_wav(wr_t *s, int idx) {
    if (idx < 0 || idx >= s->nwav) return;
    char path[800];
    snprintf(path, sizeof path, "%s/wav/%s", s->data_dir, s->wavnames[idx]);
    wav_t w;
    memset(&w, 0, sizeof w);
    if (wav_read(path, &w)) { wlog(s, "WAV %s: not readable (16/24/32-bit PCM or 32-bit float)", s->wavnames[idx]); return; }
    int act = s->wav_act, dst = 1 - act;
    s->wavs[dst] = w;                                   /* the slot the audio side is not reading */
    __atomic_store_n(&s->wav_act, dst, __ATOMIC_RELEASE);
    usleep(60000);                                      /* the audio side finishes the block that may still read the old one */
    wav_t old = s->wavs[act];
    s->wavs[act].l = s->wavs[act].r = NULL; s->wavs[act].n = 0;
    free(old.l); free(old.r);
    s->wav_loaded_idx = idx;
    wlog(s, "WAV %s: %d samples (%.1f s)", s->wavnames[idx], w.n, (double)w.n / SR);
    __atomic_add_fetch(&s->rev, 1, __ATOMIC_RELEASE);
}

static void worker_save(wr_t *s) {
    char dir[700], path[800];
    snprintf(dir, sizeof dir, "%s/user", s->data_dir);
    mkdir(dir, 0755);
    int mx = 0;
    for (int i = 0; i < s->nuser; i++) {
        int k = atoi(s->usernames[i] + (s->usernames[i][0] == 'U' ? 1 : 0));
        if (k > mx) mx = k;
    }
    snprintf(path, sizeof path, "%s/U%04d.wreck", dir, mx + 1);
    FILE *f = fopen(path, "w");
    if (!f) { wlog(s, "save: cannot write %s", path); return; }
    fprintf(f, "wreck=1\n");
    for (int i = 0; i < NP; i++) {
        if (WP[i].fmt == F_TRIG || i == P_p_bank || i == P_p_sel || i == P_wav || i == P_wav_prev || i == P_wav_next || (i >= P_wl_1 && i <= P_wl_8) || i == P_wav_info) continue;
        fprintf(f, "%s=%g\n", WP[i].key, s->save_v[i]);
    }
    if (s->save_wav[0]) fprintf(f, "wavname=%s\n", s->save_wav);      /* the WAV by its name, never the audio */
    fclose(f);
    wlog(s, "saved %s", path);
    scan_all(s);
}

static void worker_user(wr_t *s, int idx) {
    if (idx < 0 || idx >= s->nuser) return;
    char path[800];
    snprintf(path, sizeof path, "%s/user/%s.wreck", s->data_dir, s->usernames[idx]);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char *t = calloc(1, 16384);
    if (t) {
        size_t n = fread(t, 1, 16383, f);
        t[n] = 0;
        char *old = __atomic_exchange_n(&s->pend_text, t, __ATOMIC_ACQ_REL);
        free(old);
    }
    fclose(f);
}

static void *worker(void *p) {
    wr_t *s = p;
    pthread_mutex_lock(&s->mu);
    while (!s->quit) {
        if (!(s->req_scan || s->req_wav >= 0 || s->req_save || s->req_user >= 0)) { pthread_cond_wait(&s->cv, &s->mu); continue; }
        int scan = s->req_scan, wv = s->req_wav, sv = s->req_save, us = s->req_user;
        s->req_scan = 0; s->req_wav = -1; s->req_save = 0; s->req_user = -1;
        pthread_mutex_unlock(&s->mu);
        if (scan) scan_all(s);
        if (wv >= 0) worker_load_wav(s, wv);
        if (sv) worker_save(s);
        if (us >= 0) worker_user(s, us);
        pthread_mutex_lock(&s->mu);
    }
    pthread_mutex_unlock(&s->mu);
    return NULL;
}
static void post(wr_t *s, int scan, int wav, int save, int user) {
    pthread_mutex_lock(&s->mu);
    if (scan) s->req_scan = 1;
    if (wav >= 0) s->req_wav = wav;
    if (save) s->req_save = 1;
    if (user >= 0) s->req_user = user;
    pthread_cond_signal(&s->cv);
    pthread_mutex_unlock(&s->mu);
}

/* ---------------------------------------------------------------------------------------------- create / destroy */
static void *w_create(const char *data_dir) {
    wr_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->bl = calloc(BUFN, sizeof(float));
    s->br = calloc(BUFN, sizeof(float));
    if (!s->bl || !s->br) { free(s->bl); free(s->br); free(s); return NULL; }
    s->rng = 0x9e3779b9u ^ (uint32_t)time(NULL);
    apply_defaults(s, 0);
    s->bpm = 120; s->lim_g = 1; s->att_sys = -1; s->req_wav = -1; s->req_user = -1; s->last_note = 60; s->last_k = -1;
    s->att[0] = 0.1f; s->att[1] = 0.0f; s->att[2] = 0.0f;
    s->hg_a[0] = 1; s->hg_a[1] = 1; s->hg_a[2] = 1; s->hg_a[3] = 1;
    s->cl_x = 0.1f; s->cl_y = 0.1f; s->cl_x1 = 0.1f; s->cl_y1 = 0.1f;
    for (int i = 0; i < VW_VOICES; i++) s->view.vpos[i] = -1;
    for (int i = 0; i < 4; i++) s->rv_len[i] = 0;
    s->wav_loaded_idx = -1;
    if (data_dir && data_dir[0]) {
        snprintf(s->data_dir, sizeof s->data_dir, "%s", data_dir);
        snprintf(s->logp, sizeof s->logp, "%s/wreck.log", data_dir);
        pthread_mutex_init(&s->mu, NULL);
        pthread_cond_init(&s->cv, NULL);
        if (!pthread_create(&s->th, NULL, worker, s)) s->started = 1;
        scan_all(s);
        view_start(&s->ov, data_dir, &s->view);
    }
    return s;
}
static void w_destroy(void *p) {
    wr_t *s = p;
    if (!s) return;
    view_stop(&s->ov);
    if (s->started) {
        pthread_mutex_lock(&s->mu);
        s->quit = 1;
        pthread_cond_signal(&s->cv);
        pthread_mutex_unlock(&s->mu);
        pthread_join(s->th, NULL);
    }
    free(s->wavs[0].l); free(s->wavs[0].r); free(s->wavs[1].l); free(s->wavs[1].r);
    free(s->pend_text);
    free(s->bl); free(s->br);
    free(s);
}

/* ---------------------------------------------------------------------------------------------- voices */
static void voice_off(wr_t *s, int key, int arp) {
    for (int i = 0; i < MAXV; i++) {
        voice_t *v = &s->vc[i];
        if (v->on && v->key == key && v->arp == arp && v->stage < 2) {
            v->stage = 2;
            v->rel_dec = v->env / fmaxf(1.0f, map_ms(s->v[P_v_rel]) * SRF / 1000.0f);
        }
    }
}

static void voice_on(wr_t *s, int key, float vel, int arp, int gate_samples) {
    int idx = -1;
    int nv = (int)lroundf(s->v[P_v_count]);
    if (nv < 1) nv = 1;
    if (nv > MAXV) nv = MAXV;
    for (int i = 0; i < nv && idx < 0; i++) if (!s->vc[i].on) idx = i;
    if (idx < 0) {
        uint32_t best = 0xffffffffu;
        for (int i = 0; i < nv; i++) if (s->vc[i].stage == 2 && s->vc[i].age < best) { best = s->vc[i].age; idx = i; }
    }
    if (idx < 0) {
        uint32_t best = 0xffffffffu;
        for (int i = 0; i < nv; i++) if (s->vc[i].age < best) { best = s->vc[i].age; idx = i; }
    }
    voice_t *v = &s->vc[idx];
    memset(v, 0, sizeof *v);
    int mode = (int)s->v[P_v_mode];
    float lag;
    if (mode == 2) lag = 0.05f * SRF + clampf(((float)key - 24.0f) / 72.0f, 0, 1) * 19.0f * SRF;          /* LOCK: the key picks the place */
    else if (mode == 3) lag = 0.08f * SRF;                                                                 /* PLAY: the live edge */
    else lag = 0.05f * SRF + powf(1.0f - vel, 1.5f) * 19.0f * SRF;                                         /* velocity picks fresh or older */
    v->on = 1; v->key = key; v->arp = arp; v->vel = vel; v->age = ++s->vage; v->stage = 0; v->env = 0;
    v->atk_inc = 1.0f / fmaxf(1.0f, map_ms(s->v[P_v_att]) * SRF / 1000.0f);
    v->fin = 1.0f;
    v->pan = ((float)((key * 37) % 100) / 100.0f - 0.5f) * 0.6f;
    int dir = (int)s->v[P_v_dir];
    v->dir = dir == 1 ? -1 : 1;
    if (dir == 3) v->dir = (rnd32(&s->rng) & 1) ? 1 : -1;
    double pos = (double)s->wpos - (double)lag;
    while (pos < 0) pos += BUFN;
    v->pos = pos;
    v->off_at = arp ? gate_samples : -1;
}

static void all_off(wr_t *s) {
    for (int i = 0; i < MAXV; i++) if (s->vc[i].on && s->vc[i].stage < 2) {
        s->vc[i].stage = 2;
        s->vc[i].rel_dec = s->vc[i].env / fmaxf(1.0f, map_ms(s->v[P_v_rel]) * SRF / 1000.0f);
    }
}

/* ---------------------------------------------------------------------------------------------- arp */
static void arp_add(wr_t *s, int note) {
    for (int i = 0; i < s->nheld; i++) if (s->held[i] == note) return;
    if (s->nheld < 16) s->held[s->nheld++] = note;
}
static void arp_del(wr_t *s, int note) {
    for (int i = 0; i < s->nheld; i++) if (s->held[i] == note) { memmove(&s->held[i], &s->held[i + 1], (size_t)(s->nheld - i - 1) * sizeof(int)); s->nheld--; return; }
}
static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

static const double STEPLEN[] = {1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125};

static void arp_step(wr_t *s, long k) {
    int nl = (int)s->v[P_a_nlen], vl = (int)s->v[P_a_vlen], gl = (int)s->v[P_a_glen], ol = (int)s->v[P_a_olen];
    if (nl < 1) nl = 1; if (vl < 1) vl = 1; if (gl < 1) gl = 1; if (ol < 1) ol = 1;
    long kk = k < 0 ? 0 : k;
    int note_on = s->v[P_a_n_1 + (int)(kk % nl)] > 0.5f;
    int vel_hi = s->v[P_a_v_1 + (int)(kk % vl)] > 0.5f;
    int gate_hi = s->v[P_a_g_1 + (int)(kk % gl)] > 0.5f;
    int oct_up = s->v[P_a_o_1 + (int)(kk % ol)] > 0.5f;
    if (!note_on || s->nheld == 0) return;
    int order = (int)s->v[P_a_order];
    int base[16], nb = s->nheld;
    memcpy(base, s->held, sizeof(int) * (size_t)nb);
    if (order != 4) qsort(base, (size_t)nb, sizeof(int), cmp_int);
    int range = (int)s->v[P_a_range] + 1;
    int seq[64], ns = 0;
    for (int o = 0; o < range; o++) for (int i = 0; i < nb && ns < 64; i++) seq[ns++] = base[i] + 12 * o;
    int pick;
    if (order == 1) pick = ns - 1 - (s->arp_idx % ns);
    else if (order == 2) { int per = ns > 1 ? 2 * ns - 2 : 1; int q = s->arp_idx % per; pick = q < ns ? q : per - q; }
    else if (order == 3) pick = (int)(rnd32(&s->rng) % (uint32_t)ns);
    else pick = s->arp_idx % ns;
    s->arp_idx++;
    int note = seq[pick] + (oct_up ? 12 : 0);
    if (note > 127) note -= 12;
    double step_samples = s->steplen * 60.0 / s->bpm * SR;
    int gate = (int)(step_samples * (gate_hi ? 0.9 : 0.35));
    if (gate < 64) gate = 64;
    voice_on(s, note, vel_hi ? 1.0f : 0.55f, 1, gate);
}

static void arp_clear(wr_t *s) {
    s->nheld = 0; s->phys_down = 0;
    for (int i = 0; i < MAXV; i++) if (s->vc[i].on && s->vc[i].arp && s->vc[i].stage < 2) {
        s->vc[i].stage = 2;
        s->vc[i].rel_dec = s->vc[i].env / fmaxf(1.0f, map_ms(s->v[P_v_rel]) * SRF / 1000.0f);
    }
}

/* ---------------------------------------------------------------------------------------------- MIDI */
static void w_midi(void *p, const uint8_t *m, int len) {
    wr_t *s = p;
    if (len < 2) return;
    int st = m[0] & 0xf0, d1 = m[1] & 127, d2 = len > 2 ? m[2] & 127 : 0;
    int arp = s->v[P_a_on] > 0.5f;
    if (st == 0x90 && d2 > 0) {
        s->last_note = d1;
        s->heldcount++;
        if (arp) {
            if (s->v[P_a_latch] > 0.5f && s->phys_down == 0) { s->nheld = 0; }
            s->phys_down++;
            arp_add(s, d1);
        } else voice_on(s, d1, d2 / 127.0f, 0, 0);
    } else if (st == 0x80 || st == 0x90) {
        if (s->heldcount > 0) s->heldcount--;
        if (arp) {
            if (s->phys_down > 0) s->phys_down--;
            if (s->v[P_a_latch] < 0.5f) arp_del(s, d1);
        } else voice_off(s, d1, 0);
    } else if (st == 0xE0) {
        int b = d1 | (d2 << 7);
        s->bend = ((float)b - 8192.0f) / 8192.0f * 2.0f;
    } else if (st == 0xB0) {
        if (d1 == 1) s->modw = d2 / 127.0f;
        else if (d1 == 120 || d1 == 123) { all_off(s); arp_clear(s); s->heldcount = 0; }
    }
}

/* ---------------------------------------------------------------------------------------------- params API */
static void w_set(void *p, const char *key, const char *val) {
    wr_t *s = p;
    if (!strcmp(key, "state")) {
        const char *w = strstr(val, "wav=");
        if (w) {
            char nm[64];
            size_t n = strcspn(w + 4, "|");
            if (n < sizeof nm) {
                memcpy(nm, w + 4, n); nm[n] = 0;
                for (int i = 0; i < s->nwav; i++) if (!strcmp(s->wavnames[i], nm)) { s->v[P_wav] = (float)i; post(s, 0, i, 0, -1); break; }
            }
        }
        return;
    }
    int i = key_index(key);
    if (i < 0) return;
    float x = (float)atof(val);
    if (i >= P_wl_1 && i <= P_wl_8) {                   /* a tap on a file row: choose it, load it, make WAV the source */
        if (x < 0.5f) return;
        int k = ((int)lroundf(s->v[P_wav]) / 8) * 8 + (i - P_wl_1);
        if (k < s->nwav) {
            s->v[P_wav] = (float)k; s->v[P_src] = 2.0f;
            post(s, 0, k, 0, -1);
            __atomic_add_fetch(&s->rev, 1, __ATOMIC_RELEASE);
        }
        return;
    }
    if (WP[i].fmt == F_TRIG) {
        if (x < 0.5f) return;
        if (i == P_wav_scan) { post(s, 1, -1, 0, -1); return; }
        if (i == P_wav_use) { s->v[P_src] = 2.0f; if (s->nwav > 0 && s->wav_loaded_idx != (int)s->v[P_wav]) post(s, 0, (int)s->v[P_wav], 0, -1); __atomic_add_fetch(&s->rev, 1, __ATOMIC_RELEASE); return; }
        if (i == P_p_load) {
            if (s->v[P_p_bank] < 0.5f) {
                load_factory(s, (int)s->v[P_p_sel]);
                post(s, 0, (int)s->v[P_wav], 0, -1);
            } else post(s, 0, -1, 0, (int)s->v[P_p_sel]);
        } else if (i == P_p_save) {
            memcpy(s->save_v, s->v, sizeof s->v);
            int w = (int)s->v[P_wav];
            snprintf(s->save_wav, sizeof s->save_wav, "%s", (w >= 0 && w < s->nwav && s->v[P_src] > 1.5f && s->v[P_src] < 2.5f) ? s->wavnames[w] : "");
            post(s, 0, -1, 1, -1);
        } else if (i == P_p_init) {
            apply_defaults(s, 1);
            __atomic_add_fetch(&s->rev, 1, __ATOMIC_RELEASE);
        }
        return;
    }
    if (i >= P_mon_t0 && i <= P_mon_t6) {            /* the skin's MON buttons: one of seven, or none */
        int t = i - P_mon_t0 + 1;
        if (x > 0.5f) s->v[P_mon] = (float)t; else if ((int)lroundf(s->v[P_mon]) == t) s->v[P_mon] = 0;
        __atomic_add_fetch(&s->rev, 1, __ATOMIC_RELEASE);
        return;
    }
    float old = s->v[i];
    set_idx(s, i, x);
    if (i == P_wav && s->nwav > 0 && s->v[i] >= (float)s->nwav) s->v[i] = (float)(s->nwav - 1);   /* the stepper stops at the last file */
    if (i == P_src_level || i == P_b1_level || i == P_b2_mix || i == P_b3_level || i == P_b4_level || i == P_b5_level || i == P_level ||
        (i >= P_b1_on && i <= P_b5_on)) __atomic_add_fetch(&s->rev, 1, __ATOMIC_RELEASE);   /* the chain strip's numbers */
    if (i == P_wav && s->v[i] != old) post(s, 0, (int)s->v[i], 0, -1);
    if (i == P_a_on && s->v[i] < 0.5f && old >= 0.5f) arp_clear(s);
    if (i == P_a_latch && s->v[i] < 0.5f && old >= 0.5f && s->phys_down == 0) { s->nheld = 0; }
    if (i == P_o_stut_on && s->v[i] > 0.5f && old < 0.5f) s->st_active = -1;   /* capture on the next block */
}

static int w_get(void *p, const char *key, char *buf, int len) {
    wr_t *s = p;
    if (!strcmp(key, "display_rev")) return snprintf(buf, (size_t)len, "%d", s->rev);
    if (!strcmp(key, "state")) {
        int w = (int)s->v[P_wav];
        return snprintf(buf, (size_t)len, "W1|wav=%s|", (w >= 0 && w < s->nwav) ? s->wavnames[w] : "");
    }
    if (!strncmp(key, "wl_", 3) && key[3] >= '1' && key[3] <= '8') {         /* SAMPLES page: the eight rows of the page that holds the chosen file */
        int cur = (int)lroundf(s->v[P_wav]), n = __atomic_load_n(&s->nwav, __ATOMIC_ACQUIRE);
        int k = (cur / 8) * 8 + (key[3] - '1');
        if (key[4] == 0) {
            if (k < n) {
                char t[64]; snprintf(t, sizeof t, "%s", s->wavnames[k]);
                size_t tl = strlen(t);
                if (tl > 4 && !strcasecmp(t + tl - 4, ".wav")) t[tl - 4] = 0;
                return snprintf(buf, (size_t)len, "%.31s", t);
            }
            return snprintf(buf, (size_t)len, " ");
        }
        if (!strcmp(key + 4, "_on")) return snprintf(buf, (size_t)len, "%d", k < n && k == cur);
        return 0;
    }
    size_t kl = strlen(key);
    int disp = kl > 8 && !strcmp(key + kl - 8, "_display");
    char k2[64];
    if (disp) { snprintf(k2, sizeof k2, "%.*s", (int)(kl - 8), key); key = k2; }
    int i = key_index(key);
    if (i < 0) return 0;
    if (i >= P_mon_t0 && i <= P_mon_t6 && !disp) return snprintf(buf, (size_t)len, "%d", (int)lroundf(s->v[P_mon]) == i - P_mon_t0 + 1);
    if (disp) {
        char t[64];
        fmt_value(s, i, s->v[i], t, sizeof t);
        return snprintf(buf, (size_t)len, "%s", t);
    }
    return snprintf(buf, (size_t)len, "%g", (double)s->v[i]);
}

/* ---------------------------------------------------------------------------------------------- song position */
static void w_songpos(void *p, double ppq, double bpm, int playing, int frames) {
    wr_t *s = p;
    (void)frames;
    if (bpm > 20 && bpm < 400) s->bpm = bpm;
    s->transport = playing;
    if (playing && ppq >= 0) s->beat = ppq;
}

/* ---------------------------------------------------------------------------------------------- sources */
static const float ATT_TS[15] = {78, 70, 60, 60, 600, 200, 150, 120, 110, 100, 100, 90, 80, 70, 60};

static void att_deriv(int sys, const float *a, float *d) {
    float x = a[0], y = a[1], z = a[2];
    switch (sys) {
    case 0: d[0] = 10 * (y - x); d[1] = x * (28 - z) - y; d[2] = x * y - 2.6667f * z; break;                     /* Lorenz */
    case 1: d[0] = -y - z; d[1] = x + 0.2f * y; d[2] = 0.2f + z * (x - 5.7f); break;                              /* Rossler */
    case 2: d[0] = 35 * (y - x); d[1] = -7 * x - x * z + 28 * y; d[2] = x * y - 3 * z; break;                     /* Chen */
    case 3: d[0] = 36 * (y - x); d[1] = -x * z + 20 * y; d[2] = x * y - 3 * z; break;                             /* Lu */
    case 4: d[0] = sinf(y) - 0.208186f * x; d[1] = sinf(z) - 0.208186f * y; d[2] = sinf(x) - 0.208186f * z; break; /* Thomas */
    case 5: d[0] = -1.89f * x - 4 * y - 4 * z - y * y; d[1] = -1.89f * y - 4 * z - 4 * x - z * z; d[2] = -1.89f * z - 4 * x - 4 * y - x * x; break; /* Halvorsen */
    case 6: d[0] = (z - 0.7f) * x - 3.5f * y; d[1] = 3.5f * x + (z - 0.7f) * y;
            d[2] = 0.6f + 0.95f * z - z * z * z / 3 - (x * x + y * y) * (1 + 0.25f * z) + 0.1f * z * x * x * x; break; /* Aizawa */
    case 7: d[0] = y - 3 * x + 2.7f * y * z; d[1] = 1.7f * y - x * z + z; d[2] = 2 * x * y - 9 * z; break;       /* Dadras */
    case 8: d[0] = y * z; d[1] = x - y; d[2] = 1 - x * y; break;                                                  /* Sprott B */
    case 9: d[0] = y * z; d[1] = x - y; d[2] = 1 - x * x; break;                                                  /* Sprott C */
    case 10: d[0] = -y; d[1] = x + z; d[2] = x * z + 3 * y * y; break;                                            /* Sprott D */
    case 11: d[0] = y * z; d[1] = x * x - y; d[2] = 1 - 4 * x; break;                                             /* Sprott E */
    case 12: d[0] = y + z; d[1] = -x + 0.5f * y; d[2] = x * x - z; break;                                         /* Sprott F */
    case 13: d[0] = 0.4f * x + z; d[1] = x * z - y; d[2] = -x + y; break;                                         /* Sprott G */
    default: d[0] = -y + z * z; d[1] = x + 0.5f * y; d[2] = x - z; break;                                         /* Sprott H */
    }
}

static void att_reset(wr_t *s, int sys) {
    s->att[0] = 0.1f + 0.01f * (float)sys; s->att[1] = 0.05f; s->att[2] = 0.02f;
    if (sys == 1) { s->att[0] = 1; s->att[1] = 1; }
    for (int i = 0; i < 3; i++) { s->attmax[i] = 1; s->attdc[i] = 0; }
    s->att_sys = sys;
}

static void source(wr_t *s, float rate, float *outl, float *outr) {
    int src = (int)s->v[P_src];
    float l = 0, r = 0;
    switch (src) {
    case 0: {                                                      /* NOISE: white through a low-pass that follows RATE */
        float fc = clampf(800.0f * rate, 40.0f, 18000.0f);
        float a = 1.0f - expf(-TAU * fc / SRF);
        s->lp_n[0] += a * (rndf(&s->rng) - s->lp_n[0]);
        s->lp_n[1] += a * (rndf(&s->rng) - s->lp_n[1]);
        float g = 1.0f + 2.5f * (1.0f - a);
        l = s->lp_n[0] * g; r = s->lp_n[1] * g;
        break;
    }
    case 1: {                                                      /* CLICKS: random impulses, 8 per second at RATE 1 */
        float prob = 8.0f * rate / SRF;
        if (rnd01(&s->rng) < prob) { s->ck_l = rndf(&s->rng) * 1.2f; s->ck_r = rndf(&s->rng) * 1.2f; }
        l = s->ck_l; r = s->ck_r;
        s->ck_l *= 0.55f; s->ck_r *= 0.55f;
        break;
    }
    case 2: {                                                      /* WAV: the loaded file, looped, RATE = speed */
        const wav_t *w = &s->wavs[__atomic_load_n(&s->wav_act, __ATOMIC_ACQUIRE)];
        if (w->l && w->n > 8) {
            s->wavpos += rate;
            if (s->wavpos >= w->n) s->wavpos -= w->n;
            if (s->wavpos < 0) s->wavpos += w->n;
            int i0 = (int)s->wavpos, i1 = i0 + 1 >= w->n ? 0 : i0 + 1;
            float fr = (float)(s->wavpos - i0);
            l = w->l[i0] + (w->l[i1] - w->l[i0]) * fr;
            r = w->r[i0] + (w->r[i1] - w->r[i0]) * fr;
        }
        break;
    }
    case 3: {                                                      /* FM PING: bell-like pings, random pitch from a pentatonic set */
        s->ping_t -= 1.0f;
        if (s->ping_t <= 0) {
            static const int pent[5] = {0, 2, 4, 7, 9};
            int oct = (int)(rnd32(&s->rng) % 4), deg = pent[rnd32(&s->rng) % 5];
            s->src_ping_fc = 110.0f * exp2f((float)(oct * 12 + deg) / 12.0f);
            s->src_ping_fm = s->src_ping_fc * (1.4f + 2.1f * rnd01(&s->rng));
            s->src_ping_env = 1; s->src_ping_idx = 2 + 4 * rnd01(&s->rng);
            s->ping_pan = rndf(&s->rng) * 0.7f;
            s->ping_t = SRF / (1.5f * rate) * (0.6f + 0.8f * rnd01(&s->rng));
        }
        s->ping_ph += s->src_ping_fc / SRF; s->ping_phm += s->src_ping_fm / SRF;
        s->ping_ph -= floor(s->ping_ph); s->ping_phm -= floor(s->ping_phm);
        float e = s->src_ping_env;
        float o = sinf(TAU * (float)s->ping_ph + s->src_ping_idx * e * sinf(TAU * (float)s->ping_phm)) * e;
        s->src_ping_env *= 0.99985f;
        l = o * (1 - s->ping_pan) * 0.7f; r = o * (1 + s->ping_pan) * 0.7f;
        break;
    }
    case 4: {                                                      /* HARMONOGRAPH: two damped pendulums per axis, re-struck when they die */
        float f0 = 110.0f * rate;
        if (s->hg_t <= 0) {
            static const float rat[7] = {1, 1.5f, 2, 2.01f, 3, 1.25f, 1.3333f};
            for (int i = 0; i < 4; i++) { s->hg_f[i] = f0 * rat[rnd32(&s->rng) % 7] * (i & 1 ? 1.002f : 1.0f); s->hg_p[i] = rnd01(&s->rng); }
            s->hg_a[0] = 1; s->hg_d[0] = 1.0f - (0.8f + 0.5f * rnd01(&s->rng)) / SRF; s->hg_t = 3.0f * SRF;
        }
        s->hg_t -= 1.0f;
        s->hg_a[0] *= s->hg_d[0];
        for (int i = 0; i < 4; i++) s->hg_p[i] += s->hg_f[i] / SRF * (rate > 0 ? 1.0f : 0.0f);
        for (int i = 0; i < 4; i++) s->hg_p[i] -= floorf(s->hg_p[i]);
        float a = s->hg_a[0];
        l = 0.5f * (sinf(TAU * s->hg_p[0]) + sinf(TAU * s->hg_p[1])) * a;
        r = 0.5f * (sinf(TAU * s->hg_p[2]) + sinf(TAU * s->hg_p[3])) * a;
        if (a < 0.02f) s->hg_t = 0;
        break;
    }
    case 5: {                                                      /* CLIFFORD attractor: one map step per tick, interpolated */
        s->cl_t += 330.0f * rate / SRF;
        while (s->cl_t >= 1.0f) {
            s->cl_t -= 1.0f;
            s->cl_x1 = s->cl_x; s->cl_y1 = s->cl_y;
            float x = sinf(-1.4f * s->cl_y1) + 1.0f * cosf(-1.4f * s->cl_x1);
            float y = sinf(1.6f * s->cl_x1) + 0.7f * cosf(1.6f * s->cl_y1);
            s->cl_x = x; s->cl_y = y;
        }
        l = (s->cl_x1 + (s->cl_x - s->cl_x1) * s->cl_t) * 0.45f;
        r = (s->cl_y1 + (s->cl_y - s->cl_y1) * s->cl_t) * 0.45f;
        break;
    }
    case 6: {                                                      /* SUPERFORMULA: the shape's radius walks round while m drifts */
        s->ph[0] += 110.0 * rate / SR;
        s->ph[0] -= floor(s->ph[0]);
        s->ph[1] += 0.07 / SR; s->ph[1] -= floor(s->ph[1]);
        float m = 3.0f + 6.0f * (0.5f + 0.5f * sinf(TAU * (float)s->ph[1]));
        float phi = TAU * (float)s->ph[0];
        float c = fabsf(cosf(m * phi * 0.25f)), sn = fabsf(sinf(m * phi * 0.25f));
        float rr = powf(powf(c, 5.0f) + powf(sn, 5.0f) + 1e-6f, -1.0f / 2.0f);
        if (rr > s->sf_max) s->sf_max = rr; else s->sf_max *= 0.99999f;
        float n = rr / (s->sf_max + 1e-6f);
        l = n * cosf(phi); r = n * sinf(phi);
        break;
    }
    case 7: {                                                      /* SPIROGRAPH: a hypotrochoid */
        s->ph[0] += 110.0 * rate / SR;
        s->ph[0] -= floor(s->ph[0]);
        s->ph[1] += 0.05 / SR; s->ph[1] -= floor(s->ph[1]);
        float rr = 3.0f + 2.0f * sinf(TAU * (float)s->ph[1]);
        float t = TAU * (float)s->ph[0];
        float k = (5.0f - rr) / rr;
        float x = (5.0f - rr) * cosf(t) + 4.0f * cosf(k * t);
        float y = (5.0f - rr) * sinf(t) - 4.0f * sinf(k * t);
        float sc = 1.0f / (5.0f - rr + 4.0f + 0.001f);
        l = x * sc; r = y * sc;
        break;
    }
    case 8: {                                                      /* MOIRE: two gratings that drift apart and beat */
        float f = 110.0f * rate;
        s->ph[0] += f / SR; s->ph[1] += f * (1.0 + 0.012 + 0.01 * sin(s->ph[3] * TAU)) / SR;
        s->ph[2] += f * 2.0 / SR; s->ph[3] += 0.11 / SR;
        for (int i = 0; i < 4; i++) s->ph[i] -= floor(s->ph[i]);
        float a = sinf(TAU * (float)s->ph[0]), b = sinf(TAU * (float)s->ph[1]), c = sinf(TAU * (float)s->ph[2]);
        l = 0.5f * (a + b) * 0.9f; r = (0.5f * (a * b) + 0.25f * c) * 1.4f;
        break;
    }
    default: {                                                     /* ATTRACTORS: 15 systems, integrated at audio rate, normalised */
        int sys = (int)s->v[P_att] - 1;
        if (sys < 0) sys = 0;
        if (sys > 14) sys = 14;
        if (sys != s->att_sys) att_reset(s, sys);
        float dt = ATT_TS[sys] * rate / SRF;
        int sub = dt > 0.02f ? (int)(dt / 0.02f) + 1 : 1;
        float h = dt / (float)sub;
        for (int q = 0; q < sub; q++) {
            float k1[3], k2[3], tmp[3];
            att_deriv(sys, s->att, k1);
            for (int i = 0; i < 3; i++) tmp[i] = s->att[i] + 0.5f * h * k1[i];
            att_deriv(sys, tmp, k2);
            for (int i = 0; i < 3; i++) s->att[i] += h * k2[i];
        }
        int bad = 0;
        for (int i = 0; i < 3; i++) if (!(fabsf(s->att[i]) < 1e4f)) bad = 1;
        if (bad) att_reset(s, sys);
        float o[3];
        for (int i = 0; i < 3; i++) {
            s->attdc[i] += 0.0002f * (s->att[i] - s->attdc[i]);
            float v = s->att[i] - s->attdc[i];
            if (fabsf(v) > s->attmax[i]) s->attmax[i] = fabsf(v); else s->attmax[i] = fmaxf(0.001f, s->attmax[i] * 0.999995f);
            o[i] = v / s->attmax[i];
        }
        l = o[0] * 0.8f; r = o[1] * 0.8f;
        break;
    }
    }
    *outl = l; *outr = r;
}

/* ---------------------------------------------------------------------------------------------- the render */
typedef struct {
    float drive, tone, bias, b1mix, b1lvl;
    float time, efb, etone, emix, spread;
    float cut, res, fenv, b3mix, b3lvl;
    float ratio, sfb, b4mix, b4lvl;
    float size, decay, damp, b5mix, b5lvl;
    float vspeed; float lofi; float grit, body;
    float srate;
} eff_t;

static void compute_eff(wr_t *s, eff_t *e) {
    const float *v = s->v;
    float chaos = clampf(v[P_m_chaos] + s->modw * 0.5f, 0, 1), space = v[P_m_space], grit = v[P_m_grit], body = v[P_m_body];
    /* chaos: slow random walks (updated per block, smoothed) */
    for (int i = 0; i < 3; i++) {
        s->chaos_t[i] += 1.0f;
        if (s->chaos_t[i] > 220.0f) { s->chaos_t[i] = 0; s->chaos_r[i] += 0.3f * rndf(&s->rng); s->chaos_r[i] = clampf(s->chaos_r[i], -1, 1); }
    }
    e->srate = map_rate(v[P_src_rate]) * exp2f(s->chaos_r[0] * chaos * 1.0f);
    e->vspeed = map_speed(v[P_v_speed]) * exp2f(s->chaos_r[1] * chaos * 0.5f);
    e->drive = clampf(v[P_b1_drive] + grit * 0.5f, 0, 1);
    e->tone = clampf(v[P_b1_tone] - body * 0.2f, 0, 1);
    e->bias = v[P_b1_bias];
    e->b1mix = clampf(v[P_b1_mix] + grit * 0.15f, 0, 1);
    e->b1lvl = lvl(v[P_b1_level]);
    e->time = v[P_b2_time]; e->efb = clampf(v[P_b2_fb] + space * 0.25f, 0, 0.97f); e->etone = v[P_b2_tone];
    e->emix = clampf(v[P_b2_mix] + space * 0.4f, 0, 1); e->spread = v[P_b2_spread];
    e->cut = clampf(v[P_b3_cut] - body * 0.2f + s->chaos_r[2] * chaos * 0.2f, 0, 1);
    e->res = v[P_b3_res]; e->fenv = v[P_b3_env]; e->b3mix = v[P_b3_mix]; e->b3lvl = lvl(v[P_b3_level]);
    float st = v[P_b4_pitch] + v[P_b4_fine] / 100.0f + s->chaos_r[1] * chaos * 2.0f;
    e->ratio = exp2f(st / 12.0f); e->sfb = v[P_b4_fb]; e->b4mix = v[P_b4_mix]; e->b4lvl = lvl(v[P_b4_level]);
    e->size = clampf(v[P_b5_size] + space * 0.15f, 0, 1); e->decay = v[P_b5_decay]; e->damp = v[P_b5_damp];
    e->b5mix = clampf(v[P_b5_mix] + space * 0.5f, 0, 1); e->b5lvl = lvl(v[P_b5_level]);
    e->lofi = map_lofi(v[P_o_lofi]) * (1.0f - grit * 0.6f);
    e->grit = grit; e->body = body;
}

static const int COMB_LEN[4] = {1116, 1188, 1277, 1356};
static const int AP_LEN[2] = {556, 441};

/* one chain block on a stereo sample. b = 0..4 */
static inline void block_run(wr_t *s, const eff_t *e, int b, float *pl, float *pr) {
    float l = *pl, r = *pr;
    switch (b) {
    case 0: {                                                        /* DRIVE */
        float g = 1.0f + e->drive * e->drive * 40.0f;
        float bs = e->bias * 0.6f, bo = fsoft(bs);
        float fc = clampf(map_hz(e->tone), 100.0f, 18000.0f);
        float a = 1.0f - expf(-TAU * fc / SRF);
        float yl = fsoft(l * g + bs) - bo, yr = fsoft(r * g + bs) - bo;
        s->lp_b1[0][0] += a * (yl - s->lp_b1[0][0]); s->lp_b1[1][0] += a * (yr - s->lp_b1[1][0]);
        float wl = s->lp_b1[0][0], wr = s->lp_b1[1][0];
        l = (l * (1 - e->b1mix) + wl * e->b1mix) * e->b1lvl;
        r = (r * (1 - e->b1mix) + wr * e->b1mix) * e->b1lvl;
        break;
    }
    case 1: {                                                        /* ECHO (stereo, the spread offsets the right and crosses the feedback) */
        float tl = map_time(e->time) * SRF / 1000.0f, trr = tl * (1.0f + e->spread * 0.5f);
        s->echo_dl[0] += 0.0008f * (tl - s->echo_dl[0]);
        s->echo_dl[1] += 0.0008f * (trr - s->echo_dl[1]);
        int N = 2 * SR;
        float dls[2];
        for (int c = 0; c < 2; c++) {
            float rp = (float)s->echo_w - clampf(s->echo_dl[c], 2.0f, (float)N - 4.0f);
            if (rp < 0) rp += (float)N;
            int i0 = (int)rp, i1 = i0 + 1 >= N ? 0 : i0 + 1;
            float fr = rp - (float)i0;
            float *buf = c ? s->echo_r : s->echo_l;
            dls[c] = buf[i0] + (buf[i1] - buf[i0]) * fr;
        }
        float fc = clampf(map_hz(0.3f + e->etone * 0.7f), 300.0f, 18000.0f);
        float a = 1.0f - expf(-TAU * fc / SRF);
        float fl = dls[0] * (1 - e->spread * 0.5f) + dls[1] * e->spread * 0.5f;
        float fr2 = dls[1] * (1 - e->spread * 0.5f) + dls[0] * e->spread * 0.5f;
        s->echo_lp[0] += a * (fl - s->echo_lp[0]); s->echo_lp[1] += a * (fr2 - s->echo_lp[1]);
        s->echo_l[s->echo_w] = fsoft(l + s->echo_lp[0] * e->efb);
        s->echo_r[s->echo_w] = fsoft(r + s->echo_lp[1] * e->efb);
        s->echo_w = s->echo_w + 1 >= N ? 0 : s->echo_w + 1;
        l = l * (1 - e->emix * 0.5f) + dls[0] * e->emix;
        r = r * (1 - e->emix * 0.5f) + dls[1] * e->emix;
        break;
    }
    case 2: {                                                        /* FILTER (state variable low-pass, envelope-follower modulation) */
        float m = fabsf(l) + fabsf(r);
        s->env_f += (m > s->env_f ? 0.02f : 0.0005f) * (m - s->env_f);
        float c = clampf(e->cut + e->fenv * clampf(s->env_f * 1.5f, 0, 1), 0, 1);
        float fc = clampf(map_hz(c), 20.0f, 14000.0f);
        float f = 2.0f * sinf(PI_F * fc / SRF) * 0.5f;
        float q = 1.7f - e->res * e->res * 1.68f;
        float in[2] = {l, r}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            float lo = s->svf_low[ch], bd = s->svf_band[ch];
            for (int it = 0; it < 2; it++) {
                lo += f * bd;
                float hi = in[ch] - lo - q * bd;
                bd += f * hi;
                bd = clampf(bd, -8.0f, 8.0f); lo = clampf(lo, -8.0f, 8.0f);
            }
            s->svf_low[ch] = lo; s->svf_band[ch] = bd;
            out[ch] = lo * (1.0f + e->res * 0.4f);
        }
        l = (l * (1 - e->b3mix) + out[0] * e->b3mix) * e->b3lvl;
        r = (r * (1 - e->b3mix) + out[1] * e->b3mix) * e->b3lvl;
        break;
    }
    case 3: {                                                        /* SHIFT (two crossfading read taps) */
        const int W = 2048, N = 8192;
        s->sh_l[s->sh_w] = fsoft(l + s->sh_fb[0] * e->sfb);
        s->sh_r[s->sh_w] = fsoft(r + s->sh_fb[1] * e->sfb);
        float outl = 0, outr = 0;
        for (int t = 0; t < 2; t++) {
            float ph = s->sh_p + 0.5f * (float)t;
            if (ph >= 1.0f) ph -= 1.0f;
            float d = ph * (float)W + 2.0f;
            float rp = (float)s->sh_w - d;
            if (rp < 0) rp += (float)N;
            int i0 = (int)rp, i1 = (i0 + 1) & (N - 1);
            float fr = rp - (float)i0;
            float g = sinf(PI_F * ph);
            outl += g * (s->sh_l[i0] + (s->sh_l[i1] - s->sh_l[i0]) * fr);
            outr += g * (s->sh_r[i0] + (s->sh_r[i1] - s->sh_r[i0]) * fr);
        }
        s->sh_p += (1.0f - e->ratio) / (float)W;
        if (s->sh_p >= 1.0f) s->sh_p -= 1.0f;
        if (s->sh_p < 0.0f) s->sh_p += 1.0f;
        s->sh_w = (s->sh_w + 1) & (N - 1);
        s->sh_fb[0] = outl; s->sh_fb[1] = outr;
        l = (l * (1 - e->b4mix) + outl * e->b4mix) * e->b4lvl;
        r = (r * (1 - e->b4mix) + outr * e->b4mix) * e->b4lvl;
        break;
    }
    default: {                                                       /* SMEAR (a small Schroeder reverb with a moving size) */
        float in = (l + r) * 0.5f * 0.3f;
        float fbk = 0.6f + e->decay * 0.385f;
        float damp = 0.1f + e->damp * 0.8f;
        float sz = 0.45f + e->size * 1.0f;
        float o[2] = {0, 0};
        for (int c = 0; c < 2; c++) {
            for (int k = 0; k < 4; k++) {
                float len = ((float)COMB_LEN[k] + (c ? 23.0f : 0.0f)) * sz;
                len = clampf(len, 8.0f, 2040.0f);
                float rp = (float)s->rv_w - len;
                if (rp < 0) rp += 2048.0f;
                int i0 = (int)rp & 2047, i1 = (i0 + 1) & 2047;
                float fr = rp - floorf(rp);
                float y = s->rv_c[c][k][i0] + (s->rv_c[c][k][i1] - s->rv_c[c][k][i0]) * fr;
                s->rv_lp[c][k] = y * (1 - damp) + s->rv_lp[c][k] * damp;
                s->rv_c[c][k][s->rv_w] = in + s->rv_lp[c][k] * fbk;
                o[c] += y;
            }
            for (int k = 0; k < 2; k++) {                            /* two all-passes, fixed lengths */
                int al = AP_LEN[k] + (c ? 11 : 0);
                int rp = ((s->rv_w - al) % 1024 + 1024) & 1023;
                float bufo = s->rv_ap[c][k][rp];
                float x = o[c];
                float y = -x + bufo;
                s->rv_ap[c][k][s->rv_w & 1023] = x + bufo * 0.5f;
                o[c] = y;
            }
        }
        s->rv_w = (s->rv_w + 1) & 2047;
        l = (l * (1 - e->b5mix * 0.5f) + o[0] * e->b5mix * 0.6f) * e->b5lvl;
        r = (r * (1 - e->b5mix * 0.5f) + o[1] * e->b5mix * 0.6f) * e->b5lvl;
        break;
    }
    }
    *pl = l; *pr = r;
}

static void order_of(const wr_t *s, int *ord) {
    int used[5] = {0}, n = 0;
    for (int i = 0; i < 5; i++) {
        int b = (int)s->v[P_slot1 + i];
        if (b >= 0 && b < 5 && !used[b]) { ord[n++] = b; used[b] = 1; }
    }
    for (int b = 0; b < 5; b++) if (!used[b]) ord[n++] = b;   /* a position set twice: the missing blocks go last */
}

static void w_render(void *p, int16_t *out, int frames) {
    wr_t *s = p;
#if defined(__arm__)
    { unsigned fp; __asm__ volatile("vmrs %0, fpscr" : "=r"(fp)); fp |= 1u << 24; __asm__ volatile("vmsr fpscr, %0" ::"r"(fp)); }   /* flush denormals to zero */
#endif
    char *pt = __atomic_exchange_n(&s->pend_text, NULL, __ATOMIC_ACQ_REL);
    if (pt) { apply_text(s, pt); free(pt); post(s, 0, (int)s->v[P_wav], 0, -1); }

    eff_t E;
    compute_eff(s, &E);
    int ord[5];
    order_of(s, ord);
    int on[5];
    for (int i = 0; i < 5; i++) on[i] = s->v[P_b1_on + i] > 0.5f;
    int solo[7], anysolo = 0;
    for (int i = 0; i < 7; i++) { solo[i] = s->v[P_solo0 + i] > 0.5f; anysolo |= solo[i]; }
    int mon = (int)s->v[P_mon];
    float trim = 0.6f;
    float fbamt = s->v[P_fb] * 0.9f, srclvl = lvl(s->v[P_src_level]);
    float direct = s->v[P_direct], master = lvl(s->v[P_level]);
    int tune = s->v[P_src_tune] > 0.5f, gate = s->v[P_src_gate] > 0.5f, prev = s->v[P_src_prev] > 0.5f;
    float tune_r = tune ? exp2f(((float)s->last_note - 60.0f) / 12.0f) : 1.0f;
    float bend_r = exp2f(s->bend / 12.0f);
    int mode = (int)s->v[P_v_mode];
    float pitch_k = s->v[P_v_pitch];
    int vdir = (int)s->v[P_v_dir];
    int arp_on = s->v[P_a_on] > 0.5f;
    s->steplen = STEPLEN[(int)s->v[P_a_rate]];
    double bps = s->bpm / 60.0;
    int ball_on = s->v[P_o_ball_on] > 0.5f, lofi_on = s->v[P_o_lofi_on] > 0.5f, stut_on = s->v[P_o_stut_on] > 0.5f;
    int sync = s->v[P_o_sync] > 0.5f;
    float omix = s->v[P_o_mix];
    /* output bus timing */
    static const float DIVF[6] = {0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f};      /* in beats */
    float ball_s = DIVF[(int)s->v[P_o_ball]] * (sync ? (float)(60.0 / s->bpm) : 0.5f) * SRF;
    float bounce = 0.35f + s->v[P_o_bounce] * 0.55f;
    {
        float cap = 5.5f * SRF * (1 - bounce);
        if (ball_s > cap) ball_s = cap;
    }
    int st_len = (int)(DIVF[(int)s->v[P_o_stut]] * (sync ? (float)(60.0 / s->bpm) : 0.5f) * SRF);
    if (st_len < 64) st_len = 64;
    if (st_len > SR - 1) st_len = SR - 1;
    float bal_tap_t[8], bal_tap_g[8];
    {
        float gap = ball_s, t = 0, g = 1;
        for (int k = 0; k < 8; k++) { t += gap; g *= 0.72f; gap *= bounce; bal_tap_t[k] = t; bal_tap_g[k] = g; }
    }

    for (int n = 0; n < frames; n++) {
        /* ---- arp clock */
        if (arp_on) {
            s->beat += bps / SRF;
            long k = (long)floor(s->beat / s->steplen + 1e-9);
            if (k != s->last_k) {
                s->last_k = k;
                arp_step(s, k);
            }
        } else {
            s->beat += bps / SRF;
            s->last_k = (long)floor(s->beat / s->steplen + 1e-9);
        }
        for (int i = 0; i < MAXV; i++) if (s->vc[i].on && s->vc[i].arp && s->vc[i].off_at >= 0) {
            if (--s->vc[i].off_at < 0 && s->vc[i].stage < 2) {
                s->vc[i].stage = 2;
                s->vc[i].rel_dec = s->vc[i].env / fmaxf(1.0f, map_ms(s->v[P_v_rel]) * SRF / 1000.0f);
            }
        }
        /* ---- source */
        float want = (s->heldcount > 0 || s->nheld > 0) ? 1.0f : 0.0f;
        s->gate_env += (want > s->gate_env ? 0.01f : 0.0003f) * (want - s->gate_env);
        float sl, sr;
        source(s, E.srate * tune_r, &sl, &sr);
        if (gate) { sl *= s->gate_env; sr *= s->gate_env; }
        sl *= srclvl; sr *= srclvl;
        s->tap_l[0] = sl; s->tap_r[0] = sr;
        /* ---- chain, in the order chosen */
        float cl = sl + s->prev_vl * fbamt, cr = sr + s->prev_vr * fbamt;
        for (int i = 0; i < 5; i++) {
            int b = ord[i];
            if (on[b]) block_run(s, &E, b, &cl, &cr);
            s->tap_l[b + 1] = cl; s->tap_r[b + 1] = cr;
        }
        /* ---- limiter (the end of the chain; what the buffer records) */
        {
            float pk = fmaxf(fabsf(cl), fabsf(cr));
            float tgt = pk > 0.9f ? 0.9f / pk : 1.0f;
            if (tgt < s->lim_g) s->lim_g += 0.3f * (tgt - s->lim_g); else s->lim_g += 0.0004f * (tgt - s->lim_g);
            cl *= s->lim_g; cr *= s->lim_g;
            cl = clampf(cl, -0.98f, 0.98f); cr = clampf(cr, -0.98f, 0.98f);
            s->lim_meter += 0.01f * (fabsf(cl) - s->lim_meter);
        }
        s->tap_l[6] = cl; s->tap_r[6] = cr;
        /* ---- record into the buffer */
        s->bl[s->wpos] = cl; s->br[s->wpos] = cr;
        {
            float a = fmaxf(fabsf(cl), fabsf(cr));
            if (a > s->pk_acc) s->pk_acc = a;
            int col = (int)((long)s->wpos * VW_COLS / BUFN);
            if (col != s->pk_col) { s->view.peak[s->pk_col] = s->pk_acc; s->pk_acc = a; s->pk_col = col; }
        }
        /* ---- voices */
        float vl = 0, vr = 0;
        for (int i = 0; i < MAXV; i++) {
            voice_t *v = &s->vc[i];
            if (!v->on) continue;
            float pit = pitch_k + ((mode == 1 || mode == 3) ? (float)v->key - 60.0f : 0.0f);
            double sp = (double)(exp2f(pit / 12.0f) * E.vspeed * bend_r) * (double)v->dir;
            double lag = (double)s->wpos - v->pos;
            if (lag < 0) lag += BUFN;
            float edge = 1.0f;
            int jump = 0;
            double dist_new = lag - 1.0, dist_old = (double)BUFN - 2000.0 - lag;
            if (sp > 0) { if (dist_new < 441) edge = (float)fmax(0.0, dist_new / 441.0); if (dist_new < 2) jump = 1; }
            else { if (dist_old < 441) edge = (float)fmax(0.0, dist_old / 441.0); if (dist_old < 2) jump = 1; }
            if (jump) {
                if (vdir == 2) v->dir = -v->dir;                                           /* PING: turn round */
                else if (vdir == 3) { v->dir = (rnd32(&s->rng) & 1) ? 1 : -1; lag = 0.05 * SR + (double)rnd01(&s->rng) * 19.0 * SR; v->pos = (double)s->wpos - lag; }
                else if (sp > 0) { lag = 19.0 * SR; v->pos = (double)s->wpos - lag; }      /* FWD: start again at the oldest */
                else { lag = 0.05 * SR; v->pos = (double)s->wpos - lag; }                  /* REV: start again at the newest */
                while (v->pos < 0) v->pos += BUFN;
                v->fin = 0.0f;
                edge = 0.0f;
            } else v->fin = fminf(1.0f, v->fin + 1.0f / 441.0f);
            double rp = v->pos;
            int i0 = (int)rp; float fr = (float)(rp - i0);
            if (i0 >= BUFN) i0 -= BUFN;
            int i1 = i0 + 1 >= BUFN ? 0 : i0 + 1;
            float xl = s->bl[i0] + (s->bl[i1] - s->bl[i0]) * fr, xr = s->br[i0] + (s->br[i1] - s->br[i0]) * fr;
            v->pos += sp;
            if (v->pos >= BUFN) v->pos -= BUFN;
            if (v->pos < 0) v->pos += BUFN;
            /* envelope */
            if (v->stage == 0) { v->env += v->atk_inc; if (v->env >= 1.0f) { v->env = 1.0f; v->stage = 1; } }
            else if (v->stage == 2) { v->env -= v->rel_dec; if (v->env <= 0.0f) { v->on = 0; s->view.vpos[i] = -1; continue; } }
            float g = v->env * (0.3f + 0.7f * v->vel) * edge * (v->fin < 1.0f && jump == 0 ? v->fin : 1.0f);
            float pl = 1.0f - fmaxf(0.0f, v->pan), pr = 1.0f + fminf(0.0f, v->pan);
            vl += xl * g * pl; vr += xr * g * pr;
        }
        vl *= 0.5f; vr *= 0.5f;
        s->prev_vl = fsoft(vl); s->prev_vr = fsoft(vr);
        /* ---- the bus: voices + direct + monitor / solo */
        float xl = vl, xr = vr;
        if (anysolo) {
            xl = 0; xr = 0;
            for (int i = 0; i < 7; i++) if (solo[i]) { xl += s->tap_l[i]; xr += s->tap_r[i]; }
        } else {
            xl += cl * direct; xr += cr * direct;
            if (mon >= 1 && mon <= 7) { xl += s->tap_l[mon - 1]; xr += s->tap_r[mon - 1]; }
            if (prev) { xl += s->tap_l[0]; xr += s->tap_r[0]; }
        }
        /* stutter */
        s->st_l[s->st_w] = xl; s->st_r[s->st_w] = xr;
        if (stut_on) {
            if (s->st_active <= 0) {                                     /* capture the last slice and loop it */
                s->st_active = 1; s->fz_n = st_len; s->fz_i = 0;
                for (int k = 0; k < st_len; k++) {
                    int q = (s->st_w - st_len + 1 + k + 2 * SR) % (2 * SR);
                    s->fz_l[k] = s->st_l[q]; s->fz_r[k] = s->st_r[q];
                }
            }
            float fade = 1.0f;
            if (s->fz_i < 32) fade = (float)s->fz_i / 32.0f; else if (s->fz_n - s->fz_i < 32) fade = (float)(s->fz_n - s->fz_i) / 32.0f;
            xl = s->fz_l[s->fz_i] * fade; xr = s->fz_r[s->fz_i] * fade;
            if (++s->fz_i >= s->fz_n) s->fz_i = 0;
        } else s->st_active = 0;
        s->st_w = s->st_w + 1 >= 2 * SR ? 0 : s->st_w + 1;
        /* lo-fi */
        if (lofi_on) {
            s->lofi_acc += E.lofi / SRF;
            if (s->lofi_acc >= 1.0f) { s->lofi_acc -= 1.0f; s->lofi_hl = xl; s->lofi_hr = xr; }
            float a = 1.0f - expf(-TAU * fminf(0.45f * E.lofi, 18000.0f) / SRF);
            s->lofi_ll += a * (s->lofi_hl - s->lofi_ll); s->lofi_lr += a * (s->lofi_hr - s->lofi_lr);
            float q = 1024.0f * (1.0f - E.grit * 0.7f);
            xl = floorf(s->lofi_ll * q + 0.5f) / q; xr = floorf(s->lofi_lr * q + 0.5f) / q;
        }
        /* bouncing-ball delay (taps on an input history, no feedback) */
        {
            s->ball_l[s->ball_w] = xl; s->ball_r[s->ball_w] = xr;
            if (ball_on) {
                float wl = 0, wr = 0;
                for (int k = 0; k < 8; k++) {
                    int q = (s->ball_w - (int)bal_tap_t[k] + 6 * SR) % (6 * SR);
                    if (k & 1) { wl += s->ball_r[q] * bal_tap_g[k]; wr += s->ball_l[q] * bal_tap_g[k]; }
                    else { wl += s->ball_l[q] * bal_tap_g[k]; wr += s->ball_r[q] * bal_tap_g[k]; }
                }
                xl += wl * omix * 1.2f; xr += wr * omix * 1.2f;
            }
            s->ball_w = s->ball_w + 1 >= 6 * SR ? 0 : s->ball_w + 1;
        }
        /* DC block, master, soft clip */
        float yl = xl - s->dc_xl + 0.9985f * s->dc_yl; s->dc_xl = xl; s->dc_yl = yl;
        float yr = xr - s->dc_xr + 0.9985f * s->dc_yr; s->dc_xr = xr; s->dc_yr = yr;
        yl *= master * trim; yr *= master * trim;
        yl = clampf(yl, -1.5f, 1.5f); yr = clampf(yr, -1.5f, 1.5f);
        yl = yl * (1.0f - yl * yl * (1.0f / 6.75f)); yr = yr * (1.0f - yr * yr * (1.0f / 6.75f));   /* soft clip: 1.5 maps to 1.0 */
        int a = (int)lroundf(yl * 32767.0f), b2 = (int)lroundf(yr * 32767.0f);
        out[2 * n] = (int16_t)(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
        out[2 * n + 1] = (int16_t)(b2 > 32767 ? 32767 : b2 < -32768 ? -32768 : b2);
        s->wpos = s->wpos + 1 >= BUFN ? 0 : s->wpos + 1;
    }
    /* the strip's data */
    s->view.wpos = (float)s->wpos / (float)BUFN;
    for (int i = 0; i < MAXV; i++) {
        voice_t *v = &s->vc[i];
        s->view.vpos[i] = v->on ? (float)(v->pos / BUFN) : -1.0f;
    }
    s->view.ver++;
}

static const mpc_engine_t ENGINE = {w_create, w_destroy, w_midi, w_set, w_get, w_render, NULL, w_songpos};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
