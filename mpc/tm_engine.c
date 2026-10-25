/* Turing LCC - versão MPC com tela própria.
 * O plugin VST2 é o wrapper do projeto mpc-vst-plugins (mpc/mpcvst/vst2_wrap.c, licença MIT), que dá a tela
 * nativa da MPC (skin em "Plugin Skins/TUI.json") e cuida dos Q-Links; este arquivo liga esse wrapper ao núcleo
 * (core/tm.c) e mantém o resto da versão anterior:
 * - página de controle em http://<ip-da-mpc>:8100 (samples, memórias, escala personalizada etc.);
 * - MIDI por uma porta ALSA própria ("Turing LCC"), porque a MPC ignora o MIDI que sai de plugins. */
#define _GNU_SOURCE
#include "../core/tm.h"
#include "mpcvst/engine.h"
#include <alsa/asoundlib.h>   /* só tipos e macros: a biblioteca é aberta com dlopen, se existir */
#include <arpa/inet.h>
#include <dlfcn.h>
#include <ifaddrs.h>
#include <math.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define FIRST_PORT 8100
#define SR 44100.0
#define NKV 32

typedef struct {
    void *lib; snd_seq_t *seq; int port, tried;
    int (*open)(snd_seq_t **, const char *, int, int);
    int (*name)(snd_seq_t *, const char *);
    int (*mkport)(snd_seq_t *, const char *, unsigned int, unsigned int);
    int (*out)(snd_seq_t *, snd_seq_event_t *);
    int (*close)(snd_seq_t *);
} Alsa;

typedef struct {
    TmCore *c;
    TmMidi in[256]; int nin;
    TmMidi out[512];
    float L[256], R[256];
    double ppq, bpm; int playing, hasHost;
    pthread_t th; int thOn, srv, port, run;
    char dir[512];
    int last[NKV], lastOk, scan;   /* o que a tela já sabe (mpc_engine_changed) */
    Alsa a;
} E;

/* ---- ALSA ---- */
static void alsa_open(E *w) {
    Alsa *a = &w->a;
    if (a->tried) return;
    a->tried = 1;
    a->lib = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!a->lib) return;
    a->open = dlsym(a->lib, "snd_seq_open"); a->name = dlsym(a->lib, "snd_seq_set_client_name");
    a->mkport = dlsym(a->lib, "snd_seq_create_simple_port"); a->out = dlsym(a->lib, "snd_seq_event_output_direct");
    a->close = dlsym(a->lib, "snd_seq_close");
    if (!a->open || !a->name || !a->mkport || !a->out || !a->close || a->open(&a->seq, "default", SND_SEQ_OPEN_OUTPUT, SND_SEQ_NONBLOCK) < 0) {
        a->seq = NULL; return;
    }
    a->name(a->seq, "Turing LCC");
    a->port = a->mkport(a->seq, "Turing LCC", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (a->port < 0) { a->close(a->seq); a->seq = NULL; }
}
static void alsa_send(E *w, const uint8_t *m) {
    Alsa *a = &w->a;
    if (!a->seq) return;
    snd_seq_event_t ev; snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, a->port); snd_seq_ev_set_subs(&ev); snd_seq_ev_set_direct(&ev);
    int st = m[0] & 0xF0, ch = m[0] & 15;
    if (st == 0x90 && m[2]) snd_seq_ev_set_noteon(&ev, ch, m[1], m[2]);
    else if (st == 0x80 || st == 0x90) snd_seq_ev_set_noteoff(&ev, ch, m[1], 0);
    else return;
    a->out(a->seq, &ev);
}

/* ---- página ---- */
static void my_ip(char *out, size_t n) {
    struct ifaddrs *ifa = NULL; snprintf(out, n, "ip-da-mpc");
    if (getifaddrs(&ifa)) return;
    for (struct ifaddrs *a = ifa; a; a = a->ifa_next)
        if (a->ifa_addr && a->ifa_addr->sa_family == AF_INET && !(a->ifa_flags & IFF_LOOPBACK) && (a->ifa_flags & IFF_UP)) {
            inet_ntop(AF_INET, &((struct sockaddr_in *) a->ifa_addr)->sin_addr, out, (socklen_t) n);
            if (!strncmp(a->ifa_name, "wlan", 4)) break;
        }
    freeifaddrs(ifa);
}
static void reply(int fd, int code, const char *type, const char *body, size_t n) {
    char h[320];
    const char *cs = code == 200 ? "200 OK" : code == 400 ? "400 Bad Request" : code == 404 ? "404 Not Found" : code == 413 ? "413 Too Large" : "500 Erro";
    int hl = snprintf(h, sizeof h, "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n", cs, type, n);
    if (send(fd, h, (size_t) hl, MSG_NOSIGNAL) < 0) return;
    size_t off = 0;
    while (off < n) { ssize_t s = send(fd, body + off, n - off, MSG_NOSIGNAL); if (s <= 0) break; off += (size_t) s; }
}
static void serve(E *w, int fd) {
    struct timeval tv = { 3, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    size_t cap = 1 << 16, len = 0; char *b = malloc(cap + 1);
    if (!b) return;
    char *hend = NULL;
    while (!hend && len < cap) { ssize_t r = recv(fd, b + len, cap - len, 0); if (r <= 0) break; len += (size_t) r; b[len] = 0; hend = strstr(b, "\r\n\r\n"); }
    if (!hend) { free(b); return; }
    size_t hl = (size_t) (hend - b) + 4; long cl = 0;
    char *c = strcasestr(b, "Content-Length:"); if (c && c < hend) cl = atol(c + 15);
    if (cl < 0 || cl > (long) (cap - hl)) { reply(fd, 413, "text/plain", "grande demais", 13); free(b); return; }
    while (len < hl + (size_t) cl) { ssize_t r = recv(fd, b + len, cap - len, 0); if (r <= 0) break; len += (size_t) r; }
    b[len] = 0;
    char m[8] = { 0 }, u[1200] = { 0 };
    sscanf(b, "%7s %1199s", m, u);
    char *q = strchr(u, '?'); const char *qs = "";
    if (q) { *q = 0; qs = q + 1; }
    if (!strcmp(m, "OPTIONS")) reply(fd, 200, "text/plain", "", 0);
    else if (!strcmp(m, "GET") && (!strcmp(u, "/") || !strcmp(u, "/index.html"))) {
        char pth[700]; snprintf(pth, sizeof pth, "%s/web/tm.html", w->dir);
        FILE *f = fopen(pth, "rb");
        if (!f) reply(fd, 404, "text/plain", "sem web/tm.html", 15);
        else {
            fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
            char *h = n > 0 ? malloc((size_t) n) : NULL;
            if (h && fread(h, 1, (size_t) n, f) == (size_t) n) reply(fd, 200, "text/html; charset=utf-8", h, (size_t) n);
            else reply(fd, 500, "text/plain", "erro", 4);
            free(h); fclose(f);
        }
    } else {
        TmResp r; tm_http(w->c, m, u, qs, b + hl, &r);
        reply(fd, r.status, r.type, r.body, r.len); free(r.body);
    }
    free(b);
}
static void *http_thread(void *a) {
    E *w = a;
    while (__atomic_load_n(&w->run, __ATOMIC_ACQUIRE)) {
        struct pollfd p = { w->srv, POLLIN, 0 };
        if (poll(&p, 1, 300) <= 0) continue;
        int c = accept4(w->srv, NULL, NULL, SOCK_CLOEXEC);
        if (c < 0) continue;
        serve(w, c); close(c);
    }
    return NULL;
}
static void http_start(E *w) {
    for (int port = FIRST_PORT; port < FIRST_PORT + 10; port++) {
        int s = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (s < 0) return;
        int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        struct sockaddr_in ad; memset(&ad, 0, sizeof ad);
        ad.sin_family = AF_INET; ad.sin_port = htons((uint16_t) port); ad.sin_addr.s_addr = INADDR_ANY;
        if (bind(s, (struct sockaddr *) &ad, sizeof ad) == 0 && listen(s, 8) == 0) {
            w->srv = s; w->port = port; __atomic_store_n(&w->run, 1, __ATOMIC_RELEASE);
            if (pthread_create(&w->th, NULL, http_thread, w) == 0) w->thOn = 1; else { close(s); w->srv = -1; w->port = 0; }
            char ip[48], t[64]; my_ip(ip, sizeof ip); snprintf(t, sizeof t, "%s:%d", ip, w->port); tm_set_page_info(w->c, t);
            return;
        }
        close(s);
    }
}
static void http_stop(E *w) {
    if (!w->thOn) return;
    __atomic_store_n(&w->run, 0, __ATOMIC_RELEASE); pthread_join(w->th, NULL); w->thOn = 0;
    close(w->srv); w->srv = -1;
}

/* ---- motor para o wrapper ---- */
static void *create(const char *data_dir) {
    (void) data_dir;
    E *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->c = tm_new(SR, "/media/az01-internal");
    if (!w->c) { free(w); return NULL; }
    w->srv = -1; w->ppq = -1; w->bpm = 120;
    Dl_info di;
    if (dladdr((void *) &mpc_engine, &di) && di.dli_fname) {
        snprintf(w->dir, sizeof w->dir, "%s", di.dli_fname);
        char *s = strrchr(w->dir, '/'); if (s) *s = 0; else snprintf(w->dir, sizeof w->dir, ".");
    } else snprintf(w->dir, sizeof w->dir, ".");
    if (!getenv("TM_SEM_PAGINA")) http_start(w);
    return w;
}
static void destroy(void *i) {
    E *w = i;
    if (!w) return;
    http_stop(w);
    if (w->a.seq) w->a.close(w->a.seq);
    if (w->a.lib) dlclose(w->a.lib);
    tm_free(w->c); free(w);
}
static void midi(void *i, const uint8_t *m, int len) {
    E *w = i;
    if (len < 2 || w->nin >= 256) return;
    TmMidi t = { 0, { m[0], m[1], len > 2 ? m[2] : 0 } };
    w->in[w->nin++] = t;
}
static void song_pos(void *i, double ppq, double bpm, int playing, int frames) {
    E *w = i; (void) frames;
    w->hasHost = ppq >= 0; w->ppq = ppq; w->playing = playing; if (bpm > 0) w->bpm = bpm;
}
static int kv_index(const char *k) { for (int j = 0; TM_KV_NUM[j] && j < NKV; j++) if (!strcmp(TM_KV_NUM[j], k)) return j; return -1; }
static void set_param(void *i, const char *k, const char *v) {
    E *w = i;
    tm_kv_set(w->c, k, v);
    int j = kv_index(k);
    if (j >= 0 && w->lastOk) { int vals[NKV]; tm_kv_values(w->c, vals); w->last[j] = vals[j]; }   /* o host já sabe deste */
}
static int get_param(void *i, const char *k, char *b, int n) { return tm_kv_get(((E *) i)->c, k, b, n); }
static void render(void *i, int16_t *o, int frames) {
    E *w = i;
    if (frames > 256) frames = 256;
    memset(w->L, 0, sizeof(float) * (size_t) frames); memset(w->R, 0, sizeof(float) * (size_t) frames);
    TmHost h = { w->hasHost, w->playing, w->ppq, w->bpm };
    tm_midi_in(w->c, w->in, w->nin); w->nin = 0;
    int no = 0;
    tm_process(w->c, frames, &h, w->L, w->R, w->out, &no, 512);
    if (w->hasHost) w->ppq += frames * w->bpm / 60.0 / SR;   /* o próximo pedaço do mesmo bloco do host */
    if (no && tm_midi_out_enabled(w->c)) {
        if (!w->a.tried) alsa_open(w);
        for (int k = 0; k < no; k++) alsa_send(w, w->out[k].m);
    }
    for (int k = 0; k < frames; k++) {
        float l = w->L[k], r = w->R[k];
        l = l > 1 ? 1 : l < -1 ? -1 : l; r = r > 1 ? 1 : r < -1 ? -1 : r;
        o[2 * k] = (int16_t) lrintf(l * 32767.0f); o[2 * k + 1] = (int16_t) lrintf(r * 32767.0f);
    }
}

/* valores que mudam sem o host saber (página no navegador, memórias, "tônica pelos pads", notas da escala) */
__attribute__((visibility("hidden"))) int mpc_engine_changed(void *i, char *key, int len) {
    E *w = i; int vals[NKV];
    tm_kv_values(w->c, vals);
    if (!w->lastOk) { memcpy(w->last, vals, sizeof vals); w->lastOk = 1; return 0; }
    for (int j = 0; TM_KV_NUM[j] && j < NKV; j++)
        if (vals[j] != w->last[j]) { w->last[j] = vals[j]; snprintf(key, (size_t) len, "%s", TM_KV_NUM[j]); return 1; }
    return 0;
}

static const mpc_engine_t ENGINE = { create, destroy, midi, set_param, get_param, render, NULL, song_pos };
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
