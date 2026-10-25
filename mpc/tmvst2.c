/* Turing LCC - versão MPC (VST2 instrumento, Gen1 armhf). O núcleo está em core/tm.c.
 * - Página de controle em http://<ip-da-mpc>:8100 (8101... para outras instâncias)
 * - Som próprio na trilha de plugin; MIDI opcional por uma porta ALSA própria ("Turing LCC"),
 *   porque a MPC ignora o MIDI que sai de plugins (a porta aparece como entrada MIDI para outras trilhas).
 * C puro, sem mexer em sinais do processo da MPC. ABI VST2 escrita à mão. */
#define _GNU_SOURCE
#include "../core/tm.h"
#include <alsa/asoundlib.h>   /* só tipos e macros: a biblioteca é aberta com dlopen, se existir */
#include <arpa/inet.h>
#include <dlfcn.h>
#include <ifaddrs.h>
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

#define PLUG_UID 0x546D4C63 /* 'TmLc' */
#define FIRST_PORT 8100

typedef struct AEffect AEffect;
typedef intptr_t (*audioMasterCallback)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect {
    int32_t magic;
    intptr_t (*dispatcher)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
    void (*process)(AEffect *, float **, float **, int32_t);
    void (*setParameter)(AEffect *, int32_t, float);
    float (*getParameter)(AEffect *, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    void (*processReplacing)(AEffect *, float **, float **, int32_t);
    void (*processDoubleReplacing)(AEffect *, double **, double **, int32_t);
    char future[56];
};
typedef struct { int32_t type, byteSize, deltaFrames, flags; char data[16]; } VstEvent;
typedef struct { int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset; unsigned char midiData[4]; char detune, noteOffVelocity, reserved1, reserved2; } VstMidiEvent;
typedef struct { int32_t numEvents; intptr_t reserved; VstEvent *events[2]; } VstEvents;
typedef struct {
    double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
    int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
} VstTimeInfo;
enum {
    effOpen = 0, effClose = 1, effSetProgram = 2, effGetProgram = 3, effGetProgramName = 5, effGetParamLabel = 6, effGetParamDisplay = 7,
    effGetParamName = 8, effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12, effGetChunk = 23, effSetChunk = 24,
    effProcessEvents = 25, effCanBeAutomated = 26, effGetPlugCategory = 35, effGetEffectName = 45, effGetVendorString = 47,
    effGetProductString = 48, effGetVendorVersion = 49, effCanDo = 51, effGetVstVersion = 58,
};
enum { audioMasterGetTime = 7, kVstTransportPlaying = 1 << 1, kVstPpqPosValid = 1 << 9, kVstTempoValid = 1 << 10 };
enum { effFlagsCanReplacing = 1 << 4, effFlagsProgramChunks = 1 << 5, effFlagsIsSynth = 1 << 8 };

/* ---- ALSA por dlopen ---- */
typedef struct {
    void *lib; snd_seq_t *seq; int port, tried;
    int (*open)(snd_seq_t **, const char *, int, int);
    int (*name)(snd_seq_t *, const char *);
    int (*mkport)(snd_seq_t *, const char *, unsigned int, unsigned int);
    int (*out)(snd_seq_t *, snd_seq_event_t *);
    int (*close)(snd_seq_t *);
} Alsa;

typedef struct {
    AEffect fx;
    audioMasterCallback master;
    TmCore *c;
    TmMidi in[512]; int nin;
    TmMidi out[512];
    pthread_t th; int thOn, srv, port; int run;
    char dir[512];
    char *chunk;
    Alsa a;
} W;

static void alsa_open(W *w) {
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
static void alsa_send(W *w, const uint8_t *m) {
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
static void serve(W *w, int fd) {
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
    W *w = a;
    while (__atomic_load_n(&w->run, __ATOMIC_ACQUIRE)) {
        struct pollfd p = { w->srv, POLLIN, 0 };
        if (poll(&p, 1, 300) <= 0) continue;
        int c = accept(w->srv, NULL, NULL);
        if (c < 0) continue;
        serve(w, c); close(c);
    }
    return NULL;
}
static void http_start(W *w) {
    if (w->thOn) return;
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
static void http_stop(W *w) {
    if (!w->thOn) return;
    __atomic_store_n(&w->run, 0, __ATOMIC_RELEASE); pthread_join(w->th, NULL); w->thOn = 0;
    close(w->srv); w->srv = -1;
}

/* ---- áudio ---- */
static void run(AEffect *e, float **out, int32_t n, int add) {
    W *w = e->object;
    TmHost h = { 0, 0, 0, 120 };
    VstTimeInfo *ti = w->master ? (VstTimeInfo *) w->master(e, audioMasterGetTime, 0, kVstPpqPosValid | kVstTempoValid, 0, 0) : NULL;
    if (ti && (ti->flags & kVstPpqPosValid)) {
        h.hasHost = 1; h.ppq = ti->ppqPos; h.playing = (ti->flags & kVstTransportPlaying) != 0;
        if ((ti->flags & kVstTempoValid) && ti->tempo > 0) h.bpm = ti->tempo;
    }
    if (!add) { memset(out[0], 0, sizeof(float) * (size_t) n); memset(out[1], 0, sizeof(float) * (size_t) n); }
    tm_midi_in(w->c, w->in, w->nin); w->nin = 0;
    int no = 0;
    tm_process(w->c, n, &h, out[0], out[1], w->out, &no, 512);
    if (no && tm_midi_out_enabled(w->c)) {
        if (!w->a.tried) alsa_open(w);
        for (int i = 0; i < no; i++) alsa_send(w, w->out[i].m);
    }
}
static void processReplacing(AEffect *e, float **in, float **out, int32_t n) { (void) in; run(e, out, n, 0); }
static void process(AEffect *e, float **in, float **out, int32_t n) { (void) in; run(e, out, n, 1); }
static float getParameter(AEffect *e, int32_t i) { return tm_param_get(((W *) e->object)->c, i); }
static void setParameter(AEffect *e, int32_t i, float v) { tm_param_set(((W *) e->object)->c, i, v); }

static void copy_str(void *d, const char *s, size_t max) { strncpy(d, s, max - 1); ((char *) d)[max - 1] = 0; }
static void w_free(W *w) {
    http_stop(w);
    if (w->a.seq) w->a.close(w->a.seq);
    if (w->a.lib) dlclose(w->a.lib);
    tm_free(w->c); free(w->chunk); free(w);
}
static intptr_t dispatcher(AEffect *e, int32_t op, int32_t idx, intptr_t v, void *p, float o) {
    W *w = e->object;
    switch (op) {
    case effOpen: http_start(w); return 1;
    case effClose: w_free(w); return 1;
    case effGetPlugCategory: return 2;
    case effGetEffectName: case effGetProductString: copy_str(p, "Turing LCC", 32); return 1;
    case effGetVendorString: copy_str(p, "Alex Sheeny", 32); return 1;
    case effGetVendorVersion: return 1000;
    case effGetVstVersion: return 2400;
    case effCanBeAutomated: return idx > 0 && idx < tm_param_count();
    case effGetParamName: if (idx >= 0 && idx < tm_param_count()) copy_str(p, tm_param_name(idx), 32); return 1;
    case effGetParamLabel: copy_str(p, "", 8); return 1;
    case effGetParamDisplay: { char t[64] = ""; tm_param_display(w->c, idx, t, sizeof t); copy_str(p, t, 24); return 1; }
    case effSetSampleRate: if (o > 1000) tm_set_rate(w->c, o); return 1;
    case effSetBlockSize: case effMainsChanged: return 1;
    case effProcessEvents: {
        VstEvents *ev = p;
        for (int i = 0; i < ev->numEvents && w->nin < 512; i++) {
            if (!ev->events[i] || ev->events[i]->type != 1) continue;
            VstMidiEvent *m = (VstMidiEvent *) ev->events[i];
            TmMidi t; t.frame = m->deltaFrames; memcpy(t.m, m->midiData, 3);
            w->in[w->nin++] = t;
        }
        return 1;
    }
    case effCanDo: return p && (!strcmp(p, "receiveVstEvents") || !strcmp(p, "receiveVstMidiEvent") || !strcmp(p, "receiveVstTimeInfo")) ? 1 : -1;
    case effGetChunk: { size_t n = 0; char *s = tm_state_save(w->c, &n); if (!s) return 0; free(w->chunk); w->chunk = s; *(void **) p = w->chunk; return (intptr_t) n; }
    case effSetChunk: if (p && v > 0) tm_state_load(w->c, p, (size_t) v); return 1;
    case effSetProgram: case effGetProgram: return 0;
    case effGetProgramName: copy_str(p, "Turing LCC", 24); return 1;
    default: return 0;
    }
}

__attribute__((visibility("default"))) AEffect *VSTPluginMain(audioMasterCallback master) {
    W *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->c = tm_new(44100, "/media/az01-internal");
    if (!w->c) { free(w); return NULL; }
    w->master = master; w->srv = -1;
    Dl_info di;
    if (dladdr((void *) &VSTPluginMain, &di) && di.dli_fname) {
        snprintf(w->dir, sizeof w->dir, "%s", di.dli_fname);
        char *s = strrchr(w->dir, '/'); if (s) *s = 0; else snprintf(w->dir, sizeof w->dir, ".");
    } else snprintf(w->dir, sizeof w->dir, ".");
    AEffect *e = &w->fx;
    e->magic = 0x56737450;
    e->dispatcher = dispatcher; e->process = process; e->processReplacing = processReplacing;
    e->setParameter = setParameter; e->getParameter = getParameter;
    e->numParams = tm_param_count(); e->numPrograms = 0; e->numInputs = 0; e->numOutputs = 2;
    e->flags = effFlagsCanReplacing | effFlagsIsSynth | effFlagsProgramChunks;
    e->uniqueID = PLUG_UID; e->version = 1000; e->object = w;
    return e;
}
