/* Turing LCC - núcleo. Ver tm.h. */
#define _GNU_SOURCE
#include "tm.h"
#include <dirent.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define NVO 12
#define NOPS 64
#define CHUNK 16

/* ---------------- LCC ---------------- */
static const char *TON_NOME[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
static const int CGORD[12] = { 0, 7, 2, 9, 4, 11, 6, 8, 3, 10, 5, 1 };                 /* ordem tonal lídia */
static const char *CGOL[12] = { "T", "V", "II", "VI", "III", "VII", "#IV", "#V", "bIII", "bVII", "IV", "bII" };
#define NESC 8
static const char *ESC_NOME[NESC] = { "Lídio", "Lídio aumentado", "Lídio diminuto", "Lídio b7",
                                      "Auxiliar aumentada", "Auxiliar diminuta", "Auxiliar diminuta blues", "Ordem tonal" };
static const char *ESC_CURTO[NESC] = { "Lidio", "Lid aum", "Lid dim", "Lid b7", "Aux aum", "Aux dim", "Aux blues", "Ordem" };
static const int ESC_IV[7][9] = {   /* intervalos sobre a tônica lídia, -1 = fim */
    { 0, 2, 4, 6, 7, 9, 11, -1 }, { 0, 2, 4, 6, 8, 9, 11, -1 }, { 0, 2, 3, 6, 7, 9, 11, -1 }, { 0, 2, 4, 6, 7, 9, 10, -1 },
    { 0, 2, 4, 6, 8, 10, -1 }, { 0, 2, 3, 5, 6, 8, 9, 11, -1 }, { 0, 1, 3, 4, 6, 7, 9, 10, -1 } };
static const double DIV_BEATS[5] = { 1.0, 0.5, 0.25, 1.0 / 3.0, 1.0 / 6.0 };
static const char *DIV_NOME[5] = { "1/4", "1/8", "1/16", "1/8T", "1/16T" };
static const char *NN[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static void nota_nome(int n, char *o, size_t k) { snprintf(o, k, "%s%d", NN[((n % 12) + 12) % 12], n / 12 - 2); }   /* C3 = 60 (MPC) */

/* ---------------- parâmetros ---------------- */
typedef struct {
    int ton, esc, ordK, passos, mud, div, ext, base, dens, dur, acento, swing, relogio, bpm, canal, midi, som, timbre, vol, raiz, inteira, seguir;
    float atk, rel;
} Par;
static void par_default(Par *p) {
    memset(p, 0, sizeof *p);
    p->ton = 5; p->esc = 0; p->ordK = 7; p->passos = 8; p->mud = 15; p->div = 2; p->ext = 2; p->base = 60; p->dens = 75;
    p->dur = 60; p->acento = 40; p->swing = 50; p->relogio = 0; p->bpm = 110; p->canal = 1; p->midi = 1; p->som = 1;
    p->timbre = 0; p->vol = 80; p->raiz = 60; p->inteira = 0; p->seguir = 0; p->atk = .004f; p->rel = .25f;
}
static int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static void par_fix(Par *p) {
    p->ton = clampi(p->ton, 0, 11); p->esc = clampi(p->esc, 0, NESC - 1); p->ordK = clampi(p->ordK, 1, 12); p->passos = clampi(p->passos, 2, 16);
    p->mud = clampi(p->mud, 0, 100); p->div = clampi(p->div, 0, 4); p->ext = clampi(p->ext, 1, 4); p->base = clampi(p->base, 12, 96);
    p->dens = clampi(p->dens, 0, 100); p->dur = clampi(p->dur, 5, 100); p->acento = clampi(p->acento, 0, 100); p->swing = clampi(p->swing, 50, 75);
    p->relogio = clampi(p->relogio, 0, 1); p->bpm = clampi(p->bpm, 30, 300); p->canal = clampi(p->canal, 1, 16); p->midi = !!p->midi;
    p->som = !!p->som; p->timbre = clampi(p->timbre, 0, 1); p->vol = clampi(p->vol, 0, 100); p->raiz = clampi(p->raiz, 0, 127);
    p->inteira = !!p->inteira; p->seguir = !!p->seguir;
    if (!(p->atk >= 0)) p->atk = 0;
    if (p->atk > 2) p->atk = 2;
    if (!(p->rel >= .01f)) p->rel = .01f;
    if (p->rel > 5) p->rel = 5;
}

/* ---------------- sample ---------------- */
typedef struct Sample { float *l, *r; long n; double rate; char path[512], name[128]; } Sample;
static void sample_free(Sample *s) { if (!s) return; if (s->r && s->r != s->l) free(s->r); free(s->l); free(s); }
static uint32_t rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24; }
static uint16_t rd16(const unsigned char *p) { return (uint16_t) (p[0] | p[1] << 8); }
static Sample *wav_load(const char *path, char *err, size_t errn) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errn, "não abriu o arquivo"); return NULL; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 44 || sz > 80L * 1024 * 1024) { fclose(f); snprintf(err, errn, sz < 44 ? "arquivo vazio" : "arquivo grande demais (máx. 80 MB)"); return NULL; }
    unsigned char *b = malloc((size_t) sz);
    if (!b) { fclose(f); snprintf(err, errn, "sem memória"); return NULL; }
    size_t got = fread(b, 1, (size_t) sz, f); fclose(f);
    if ((long) got != sz || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) { free(b); snprintf(err, errn, "não é um WAV"); return NULL; }
    int fmt = 0, ch = 0, bits = 0; uint32_t rate = 0; const unsigned char *data = NULL; uint32_t dlen = 0; long p = 12;
    while (p + 8 <= sz) {
        uint32_t len = rd32(b + p + 4); const unsigned char *c = b + p + 8;
        if (p + 8 + (long) len > sz) len = (uint32_t) (sz - p - 8);
        if (!memcmp(b + p, "fmt ", 4) && len >= 16) { fmt = rd16(c); ch = rd16(c + 2); rate = rd32(c + 4); bits = rd16(c + 14); if (fmt == 0xFFFE && len >= 26) fmt = rd16(c + 24); }
        else if (!memcmp(b + p, "data", 4)) { data = c; dlen = len; }
        p += 8 + len + (len & 1);
    }
    int bps = bits / 8;
    if (!data || ch < 1 || rate < 4000 || rate > 384000 || bps < 1 || !((fmt == 1 && bits >= 8 && bits <= 32) || (fmt == 3 && (bits == 32 || bits == 64)))) {
        free(b); snprintf(err, errn, "formato de WAV não suportado"); return NULL;
    }
    long n = (long) (dlen / (uint32_t) (bps * ch));
    if (n < 1) { free(b); snprintf(err, errn, "WAV sem áudio"); return NULL; }
    Sample *s = calloc(1, sizeof *s);
    float *L = malloc(sizeof(float) * (size_t) n), *R = ch > 1 ? malloc(sizeof(float) * (size_t) n) : NULL;
    if (!s || !L || (ch > 1 && !R)) { free(s); free(L); free(R); free(b); snprintf(err, errn, "sem memória"); return NULL; }
    for (long i = 0; i < n; i++)
        for (int k = 0; k < (ch > 1 ? 2 : 1); k++) {
            const unsigned char *q = data + ((size_t) i * ch + k) * bps; float v;
            if (fmt == 3) { if (bits == 32) { float x; memcpy(&x, q, 4); v = x; } else { double x; memcpy(&x, q, 8); v = (float) x; } }
            else if (bits == 8) v = (q[0] - 128) / 128.0f;
            else if (bits == 16) v = (int16_t) rd16(q) / 32768.0f;
            else if (bits == 24) { int32_t x = (int32_t) ((uint32_t) q[0] << 8 | (uint32_t) q[1] << 16 | (uint32_t) q[2] << 24); v = (float) (x / 2147483648.0); }
            else { int32_t x = (int32_t) rd32(q); v = (float) (x / 2147483648.0); }
            if (!isfinite(v)) v = 0;
            (k ? R : L)[i] = v;
        }
    free(b);
    s->l = L; s->r = R ? R : L; s->n = n; s->rate = rate;
    snprintf(s->path, sizeof s->path, "%s", path);
    const char *bn = strrchr(path, '/'); snprintf(s->name, sizeof s->name, "%s", bn ? bn + 1 : path);
    return s;
}
static int root_from_name(const char *name) {
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };
    int best = -1;
    for (const char *p = name; *p; p++) {
        char c = *p; if (c >= 'a' && c <= 'g') c = (char) (c - 32);
        if (c < 'A' || c > 'G') continue;
        if (p > name && ((p[-1] >= 'A' && p[-1] <= 'Z') || (p[-1] >= 'a' && p[-1] <= 'z'))) continue;
        const char *q = p + 1; int acc = 0;
        if (*q == '#' || *q == 's') { acc = 1; q++; } else if (*q == 'b') { acc = -1; q++; }
        int neg = 0; if (*q == '-') { neg = 1; q++; }
        if (*q < '0' || *q > '9' || (q[1] >= '0' && q[1] <= '9')) continue;
        int oct = *q - '0'; if (neg) oct = -oct;
        int m = 60 + (oct - 3) * 12 + base[c - 'A'] + acc;
        if (m >= 0 && m < 128) best = m;
    }
    return best;
}

/* ---------------- núcleo ---------------- */
typedef struct { int active, rel, src, note; double pos, step; float gain, env, atkInc, relInc, ph, lp, tine; uint32_t age; } Voice;
enum { OP_NOVO = 1, OP_LIMPAR, OP_BIT, OP_REG, OP_TOCAR, OP_PARAR };
typedef struct { int op, a, b; } Op;
typedef struct { int ok; uint16_t reg; int len; } Mem;

struct TmCore {
    pthread_mutex_t mx;
    Par pP, P;                 /* interface / cópia do áudio */
    int dirty;
    Op ops[NOPS]; int nops;
    Mem mem[8];
    char startDir[512], pageInfo[64];
    /* sample */
    Sample *cur; Sample *trash[8]; unsigned trashGen[8]; unsigned gen, audioGen;
    /* áudio */
    double sr;
    uint16_t reg; uint32_t rng; int pos; unsigned steps;
    int notes[64], nnotes;
    int intRun; double intPpq, lastEnd; int running;
    int melNote, melCh; long offRemain;      /* nota MIDI soando e quantos quadros faltam para soltar */
    Voice vo[NVO]; uint32_t clock; int voiceMel;
    TmMidi in[256]; int nin;
    /* para a interface */
    uint16_t aReg; int aPos, aNote, aRun, aHost, aTon, aLen, aBpm100; unsigned aSteps; int aGate;
};

static uint32_t xs(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x ? x : 0x9E3779B9u; }
static float frand(uint32_t *s) { return (xs(s) >> 8) * (1.0f / 16777216.0f); }

TmCore *tm_new(double sr, const char *startDir) {
    TmCore *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    pthread_mutexattr_t ma; pthread_mutexattr_init(&ma); pthread_mutexattr_setprotocol(&ma, PTHREAD_PRIO_INHERIT);
    pthread_mutex_init(&c->mx, &ma); pthread_mutexattr_destroy(&ma);
    par_default(&c->pP); c->P = c->pP;
    c->sr = sr > 1000 ? sr : 44100;
    c->rng = 0x1234567u ^ (uint32_t) (uintptr_t) c;
    for (int i = 0; i < 8; i++) xs(&c->rng);
    c->reg = (uint16_t) xs(&c->rng); c->aReg = c->reg;
    c->melNote = -1; c->lastEnd = -1; c->voiceMel = -1;
    snprintf(c->startDir, sizeof c->startDir, "%s", startDir && *startDir ? startDir : "/");
    snprintf(c->pageInfo, sizeof c->pageInfo, "-");
    c->dirty = 1;
    return c;
}
void tm_free(TmCore *c) {
    if (!c) return;
    sample_free(c->cur); for (int i = 0; i < 8; i++) sample_free(c->trash[i]);
    pthread_mutex_destroy(&c->mx); free(c);
}
void tm_set_rate(TmCore *c, double sr) { if (sr > 1000) c->sr = sr; }
void tm_set_page_info(TmCore *c, const char *t) { pthread_mutex_lock(&c->mx); snprintf(c->pageInfo, sizeof c->pageInfo, "%s", t); pthread_mutex_unlock(&c->mx); }

static void push_op(TmCore *c, int op, int a, int b) {
    pthread_mutex_lock(&c->mx);
    if (c->nops < NOPS) { c->ops[c->nops] = (Op) { op, a, b }; __atomic_store_n(&c->nops, c->nops + 1, __ATOMIC_RELEASE); }
    pthread_mutex_unlock(&c->mx);
}
static void set_par(TmCore *c, const Par *p) {
    pthread_mutex_lock(&c->mx); c->pP = *p; par_fix(&c->pP); __atomic_store_n(&c->dirty, 1, __ATOMIC_RELEASE); pthread_mutex_unlock(&c->mx);
}
static Par get_par(TmCore *c) { Par p; pthread_mutex_lock(&c->mx); p = c->pP; pthread_mutex_unlock(&c->mx); return p; }

/* troca de sample sem a thread de áudio usar memória liberada */
static void publish_sample(TmCore *c, Sample *s) {
    pthread_mutex_lock(&c->mx);
    unsigned ag = __atomic_load_n(&c->audioGen, __ATOMIC_ACQUIRE);
    for (int i = 0; i < 8; i++) if (c->trash[i] && (int) (ag - c->trashGen[i]) >= 0) { sample_free(c->trash[i]); c->trash[i] = NULL; }
    Sample *old = c->cur;
    __atomic_store_n(&c->cur, s, __ATOMIC_RELEASE);
    unsigned g = __atomic_add_fetch(&c->gen, 1, __ATOMIC_SEQ_CST);
    if (old) {
        int put = 0;
        for (int i = 0; i < 8 && !put; i++) if (!c->trash[i]) { c->trash[i] = old; c->trashGen[i] = g; put = 1; }
        if (!put) c->trash[0] = old, c->trashGen[0] = g;   /* lista cheia: o mais antigo vaza, nunca é liberado em uso */
    }
    pthread_mutex_unlock(&c->mx);
}
static int load_sample(TmCore *c, const char *path, int setRoot, char *err, size_t errn) {
    if (!path || !*path) { publish_sample(c, NULL); return 0; }
    Sample *s = wav_load(path, err, errn);
    if (!s) return -1;
    if (setRoot) { int r = root_from_name(s->name); if (r >= 0) { Par p = get_par(c); p.raiz = r; set_par(c, &p); } }
    publish_sample(c, s);
    return 0;
}

/* notas permitidas (ordem crescente) entre base e base + extensão */
static void build_notes(TmCore *c) {
    const Par *p = &c->P; int pcs[12] = { 0 };
    if (p->esc == 7) for (int i = 0; i < p->ordK; i++) pcs[CGORD[i]] = 1;
    else for (int i = 0; ESC_IV[p->esc][i] >= 0; i++) pcs[ESC_IV[p->esc][i]] = 1;
    c->nnotes = 0;
    for (int n = p->base; n <= p->base + 12 * p->ext && n < 128 && c->nnotes < 64; n++)
        if (pcs[((n - p->ton) % 12 + 12) % 12]) c->notes[c->nnotes++] = n;
    if (!c->nnotes) c->notes[c->nnotes++] = p->base;
}

static void pull(TmCore *c) {
    if (!__atomic_load_n(&c->dirty, __ATOMIC_ACQUIRE) && !__atomic_load_n(&c->nops, __ATOMIC_ACQUIRE)) return;
    if (pthread_mutex_trylock(&c->mx)) return;
    int rebuild = __atomic_load_n(&c->dirty, __ATOMIC_ACQUIRE);
    if (rebuild) { c->P = c->pP; __atomic_store_n(&c->dirty, 0, __ATOMIC_RELEASE); }
    for (int i = 0; i < c->nops; i++) {
        Op o = c->ops[i];
        if (o.op == OP_NOVO) c->reg = (uint16_t) xs(&c->rng);
        else if (o.op == OP_LIMPAR) c->reg = 0;
        else if (o.op == OP_BIT && o.a >= 0 && o.a < 16) c->reg ^= (uint16_t) (1u << o.a);
        else if (o.op == OP_REG) { c->reg = (uint16_t) o.a; if (o.b >= 2 && o.b <= 16) c->P.passos = c->pP.passos = o.b; }
        else if (o.op == OP_TOCAR) { c->intRun = 1; c->intPpq = 0; }
        else if (o.op == OP_PARAR) c->intRun = 0;
    }
    __atomic_store_n(&c->nops, 0, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&c->mx);
    if (rebuild) build_notes(c);
}

/* ---------------- vozes ---------------- */
static void voice_off(TmCore *c, int i) { if (i >= 0 && i < NVO && c->vo[i].active && !c->P.inteira) c->vo[i].rel = 1; }
static int voice_on(TmCore *c, int note, int vel) {
    int k = -1;
    for (int i = 0; i < NVO; i++) if (!c->vo[i].active) { k = i; break; }
    if (k < 0) { k = 0; for (int i = 1; i < NVO; i++) if (c->vo[i].age < c->vo[k].age) k = i; }
    Voice *v = &c->vo[k]; memset(v, 0, sizeof *v);
    Sample *sm = __atomic_load_n(&c->cur, __ATOMIC_ACQUIRE);
    v->active = 1; v->note = note; v->age = ++c->clock; v->tine = 1;
    v->src = sm ? 1 : 0;
    v->gain = (vel / 127.0f) * (sm ? .5f : .28f);
    v->step = sm ? pow(2.0, (note - c->P.raiz) / 12.0) * sm->rate / c->sr : 440.0 * pow(2.0, (note - 69) / 12.0) / c->sr;
    v->atkInc = c->P.atk > .0005f ? (float) (1.0 / (c->P.atk * c->sr)) : 1.0f;
    v->relInc = (float) (1.0 / (c->P.rel * c->sr));
    return k;
}
static void render(TmCore *c, float *L, float *R, int n) {
    Sample *sm = __atomic_load_n(&c->cur, __ATOMIC_ACQUIRE);
    const float vol = (c->P.vol / 100.0f) * (c->P.vol / 100.0f);
    const float TP = 6.2831853f;
    for (int i = 0; i < NVO; i++) {
        Voice *v = &c->vo[i];
        if (!v->active) continue;
        if (v->src && !sm) { v->active = 0; continue; }
        for (int j = 0; j < n; j++) {
            if (v->rel) { v->env -= v->relInc; if (v->env <= 0) { v->active = 0; break; } }
            else if (v->env < 1) { v->env += v->atkInc; if (v->env > 1) v->env = 1; }
            float a, b;
            if (v->src) {
                long ip = (long) v->pos;
                if (ip + 1 >= sm->n) { v->active = 0; break; }
                float fr = (float) (v->pos - ip);
                a = sm->l[ip] + (sm->l[ip + 1] - sm->l[ip]) * fr; b = sm->r[ip] + (sm->r[ip + 1] - sm->r[ip]) * fr;
                v->pos += v->step;
            } else if (c->P.timbre == 0) {   /* "lead": triângulo filtrado + seno */
                float ph = v->ph, tri = 4.f * fabsf(ph - floorf(ph + .5f)) - 1.f;
                v->lp += (tri - v->lp) * .3f;
                a = b = .8f * v->lp + .5f * sinf(TP * ph);
                v->ph += (float) v->step; if (v->ph >= 1) v->ph -= 1;
            } else {                          /* piano elétrico */
                a = b = sinf(TP * v->ph) + .22f * sinf(TP * 2 * v->ph) + .35f * v->tine * sinf(TP * 4 * v->ph);
                v->tine *= .99985f;
                v->ph += (float) v->step; if (v->ph >= 1) v->ph -= 1;
            }
            float g = v->gain * v->env;
            L[j] += a * g * vol; R[j] += b * g * vol;
        }
    }
}

/* ---------------- passo da Turing Machine ---------------- */
static void emit(TmMidi *out, int *nout, int maxout, int frame, int st, int d1, int d2) {
    if (!out || *nout >= maxout) return;
    TmMidi m; m.frame = frame; m.m[0] = (uint8_t) st; m.m[1] = (uint8_t) (d1 & 127); m.m[2] = (uint8_t) (d2 & 127);
    out[(*nout)++] = m;
}
static void mel_off(TmCore *c, int frame, TmMidi *out, int *nout, int maxout) {
    if (c->melNote >= 0) { if (c->P.midi) emit(out, nout, maxout, frame, 0x80 | c->melCh, c->melNote, 0); c->melNote = -1; }
    voice_off(c, c->voiceMel); c->voiceMel = -1;
    c->offRemain = -1;
}
static void tm_step(TmCore *c, int frame, double stepSec, TmMidi *out, int *nout, int maxout) {
    const Par *p = &c->P;
    int len = p->passos;
    int bit = (c->reg >> (len - 1)) & 1;
    if (frand(&c->rng) < p->mud / 100.0f) bit ^= 1;      /* mudança: 0% = loop fixo; 100% = loop invertido (dobra) */
    c->reg = (uint16_t) ((c->reg << 1) | bit);
    c->pos = (c->pos + 1) % len;
    c->steps++;
    int v = c->reg & 0xFF, g = (c->reg >> 8) & 0xFF;
    int gate = p->dens >= 100 || (p->dens > 0 && g * 100 < p->dens * 256);
    mel_off(c, frame, out, nout, maxout);
    __atomic_store_n(&c->aGate, gate, __ATOMIC_RELAXED);
    if (!gate) return;
    int note = c->notes[(v * c->nnotes) >> 8];
    int acc = ((c->reg >> 3) & 7);
    int vel = clampi(110 - p->acento / 2 + acc * p->acento * 6 / 70, 1, 127);   /* acento: 3 bits do registrador */
    c->melNote = note; c->melCh = p->canal - 1; __atomic_store_n(&c->aNote, note, __ATOMIC_RELAXED);
    if (p->midi) emit(out, nout, maxout, frame, 0x90 | c->melCh, note, vel);
    if (p->som) c->voiceMel = voice_on(c, note, vel);
    c->offRemain = frame + (long) (stepSec * p->dur / 100.0 * c->sr);
}
static void stop_all(TmCore *c, int frame, TmMidi *out, int *nout, int maxout) {
    mel_off(c, frame, out, nout, maxout);
    for (int i = 0; i < NVO; i++) voice_off(c, i);
}

void tm_midi_in(TmCore *c, const TmMidi *ev, int n) {
    for (int i = 0; i < n && c->nin < 256; i++) c->in[c->nin++] = ev[i];
}
int tm_midi_out_enabled(TmCore *c) { return c->P.midi; }
int tm_midi_out_channel(TmCore *c) { return c->P.canal - 1; }

void tm_process(TmCore *c, int n, const TmHost *h, float *L, float *R, TmMidi *out, int *nout, int maxout) {
    *nout = 0;
    unsigned g = __atomic_load_n(&c->gen, __ATOMIC_ACQUIRE);
    __atomic_store_n(&c->audioGen, g, __ATOMIC_RELEASE);
    pull(c);
    const Par *p = &c->P;
    /* notas que chegam: "tônica pelas notas" (ignora o canal de saída, que pode voltar como eco na MPC) */
    for (int i = 0; i < c->nin; i++) {
        const uint8_t *m = c->in[i].m;
        if ((m[0] & 0xF0) == 0x90 && m[2] && p->seguir && (m[0] & 15) != p->canal - 1) {
            pthread_mutex_lock(&c->mx); c->pP.ton = m[1] % 12; pthread_mutex_unlock(&c->mx);
            c->P.ton = m[1] % 12; build_notes(c);
        }
    }
    c->nin = 0;

    int run; double p0, bpm;
    if (p->relogio == 0) { run = h && h->hasHost && h->playing; p0 = h ? h->ppq : 0; bpm = h && h->bpm > 0 ? h->bpm : 120; }
    else { run = c->intRun; p0 = c->intPpq; bpm = p->bpm; }
    const double bps = bpm / 60.0 / c->sr, p1 = p0 + n * bps;
    if (!run && c->running) { stop_all(c, 0, out, nout, maxout); c->running = 0; }
    if (run) {
        if (!c->running || fabs(p0 - c->lastEnd) > 0.05) { if (c->running) mel_off(c, 0, out, nout, maxout); }
        c->running = 1;
    }
    /* passos dentro do bloco */
    int stepAt[96], ns = 0;
    const double sl = DIV_BEATS[p->div];
    const int straight = p->div <= 2;
    const double sh = straight ? fmin(sl * (p->swing - 50) / 50.0, sl * .9) : 0;
    if (run) {
        for (long k = (long) floor(p0 / sl) - 1; k <= (long) floor(p1 / sl) && ns < 96; k++) {
            if (k < 0) continue;
            double t = k * sl + ((k & 1) ? sh : 0);
            if (t >= p0 && t < p1) stepAt[ns++] = (int) ((t - p0) / bps);
        }
    }
    const double stepSec = sl * 60.0 / bpm;
    int si = 0;
    for (int i = 0; i < n; i += CHUNK) {
        int len = n - i < CHUNK ? n - i : CHUNK;
        if (c->offRemain >= 0 && c->offRemain < i + len) {
            int f = c->offRemain < i ? i : (int) c->offRemain;
            if (c->melNote >= 0 && p->midi) emit(out, nout, maxout, f, 0x80 | c->melCh, c->melNote, 0);
            c->melNote = -1; voice_off(c, c->voiceMel); c->voiceMel = -1; c->offRemain = -1;
        }
        while (si < ns && stepAt[si] < i + len) {
            int f = stepAt[si] < i ? i : stepAt[si];
            if (f > n - 1) f = n - 1;
            tm_step(c, f, stepSec, out, nout, maxout);
            si++;
        }
        render(c, L + i, R + i, len);   /* as caudas terminam mesmo se o som for desligado */
    }
    if (c->offRemain >= 0) c->offRemain -= n;
    if (run) { c->lastEnd = p1; if (p->relogio == 1) c->intPpq = p1; }
    /* estado para a interface */
    __atomic_store_n(&c->aReg, c->reg, __ATOMIC_RELAXED);
    __atomic_store_n(&c->aPos, c->pos, __ATOMIC_RELAXED); __atomic_store_n(&c->aRun, run, __ATOMIC_RELAXED);
    __atomic_store_n(&c->aHost, h && h->hasHost, __ATOMIC_RELAXED); __atomic_store_n(&c->aBpm100, (int) lround(bpm * 100), __ATOMIC_RELAXED);
    __atomic_store_n(&c->aTon, c->P.ton, __ATOMIC_RELAXED); __atomic_store_n(&c->aLen, c->P.passos, __ATOMIC_RELAXED);
    __atomic_store_n(&c->aSteps, c->steps, __ATOMIC_RELEASE);
}

/* ---------------- parâmetros do host ---------------- */
enum { Q_PAGINA, Q_MUD, Q_PASSOS, Q_DENS, Q_TON, Q_ESC, Q_EXT, Q_DIV, Q_VOL, Q_NOVO, NQ };
static const char *QNAME[NQ] = { "Pagina", "Mudanca", "Passos", "Densidade", "Tonica", "Escala", "Extensao", "Divisao", "Volume", "Novo" };
int tm_param_count(void) { return NQ; }
const char *tm_param_name(int i) { return i >= 0 && i < NQ ? QNAME[i] : ""; }
float tm_param_get(TmCore *c, int i) {
    Par p = get_par(c);
    switch (i) {
        case Q_MUD: return p.mud / 100.0f;
        case Q_PASSOS: return (p.passos - 2) / 14.0f;
        case Q_DENS: return p.dens / 100.0f;
        case Q_TON: return p.ton / 11.0f;
        case Q_ESC: return p.esc / (float) (NESC - 1);
        case Q_EXT: return (p.ext - 1) / 3.0f;
        case Q_DIV: return p.div / 4.0f;
        case Q_VOL: return p.vol / 100.0f;
        default: return 0;
    }
}
void tm_param_set(TmCore *c, int i, float v) {
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    if (i == Q_NOVO) { if (v > .5f) push_op(c, OP_NOVO, 0, 0); return; }
    if (i == Q_PAGINA) return;   /* só leitura (a MPC mexe no parâmetro 0 ao preparar o plugin) */
    Par p = get_par(c);
    switch (i) {
        case Q_MUD: p.mud = (int) lroundf(v * 100); break;
        case Q_PASSOS: p.passos = 2 + (int) lroundf(v * 14); break;
        case Q_DENS: p.dens = (int) lroundf(v * 100); break;
        case Q_TON: p.ton = (int) lroundf(v * 11); break;
        case Q_ESC: p.esc = (int) lroundf(v * (NESC - 1)); break;
        case Q_EXT: p.ext = 1 + (int) lroundf(v * 3); break;
        case Q_DIV: p.div = (int) lroundf(v * 4); break;
        case Q_VOL: p.vol = (int) lroundf(v * 100); break;
        default: return;
    }
    set_par(c, &p);
}
void tm_param_display(TmCore *c, int i, char *o, size_t n) {
    Par p = get_par(c);
    switch (i) {
        case Q_PAGINA: pthread_mutex_lock(&c->mx); snprintf(o, n, "%s", c->pageInfo); pthread_mutex_unlock(&c->mx); break;
        case Q_MUD: snprintf(o, n, p.mud == 0 ? "fixo" : p.mud == 100 ? "fixo x2" : "%d%%", p.mud); break;
        case Q_PASSOS: snprintf(o, n, "%d", p.passos); break;
        case Q_DENS: snprintf(o, n, "%d%%", p.dens); break;
        case Q_TON: snprintf(o, n, "%s", TON_NOME[p.ton]); break;
        case Q_ESC: if (p.esc == 7) snprintf(o, n, "Ordem 1-%d", p.ordK); else snprintf(o, n, "%s", ESC_CURTO[p.esc]); break;
        case Q_EXT: snprintf(o, n, "%d oit", p.ext); break;
        case Q_DIV: snprintf(o, n, "%s", DIV_NOME[p.div]); break;
        case Q_VOL: snprintf(o, n, "%d%%", p.vol); break;
        case Q_NOVO: snprintf(o, n, "toque"); break;
        default: if (n) o[0] = 0;
    }
}

/* ---------------- texto / JSON ---------------- */
typedef struct { char *b; size_t n, cap; int bad; } Buf;
static void bput(Buf *b, const char *s, size_t n) {
    if (b->bad) return;
    if (b->n + n + 1 > b->cap) { size_t c = b->cap ? b->cap * 2 : 2048; while (c < b->n + n + 1) c *= 2; char *nb = realloc(b->b, c); if (!nb) { b->bad = 1; return; } b->b = nb; b->cap = c; }
    memcpy(b->b + b->n, s, n); b->n += n; b->b[b->n] = 0;
}
static void bstr(Buf *b, const char *s) { bput(b, s, strlen(s)); }
static void bfmt(Buf *b, const char *f, ...) __attribute__((format(printf, 2, 3)));
static void bfmt(Buf *b, const char *f, ...) { char t[512]; va_list a; va_start(a, f); vsnprintf(t, sizeof t, f, a); va_end(a); bstr(b, t); }
static void bjson(Buf *b, const char *s) {
    bstr(b, "\"");
    for (; *s; s++) {
        unsigned char ch = (unsigned char) *s; char t[8];
        if (ch == '"' || ch == '\\') { t[0] = '\\'; t[1] = (char) ch; bput(b, t, 2); }
        else if (ch < 0x20) { snprintf(t, sizeof t, "\\u%04x", ch); bstr(b, t); }
        else bput(b, (const char *) &ch, 1);
    }
    bstr(b, "\"");
}

/* "chave valor" por linha -> parâmetros */
static void par_apply(Par *p, const char *body) {
    char line[200]; const char *s = body;
    while (*s) {
        const char *e = strchr(s, '\n'); size_t L = e ? (size_t) (e - s) : strlen(s);
        if (L >= sizeof line) L = sizeof line - 1;
        memcpy(line, s, L); line[L] = 0;
        char k[32]; double v;
        if (sscanf(line, "%31s %lf", k, &v) == 2) {
            int iv = (int) lround(v);
            if (!strcmp(k, "tonica")) p->ton = iv; else if (!strcmp(k, "escala")) p->esc = iv; else if (!strcmp(k, "ordem")) p->ordK = iv;
            else if (!strcmp(k, "passos")) p->passos = iv; else if (!strcmp(k, "mudanca")) p->mud = iv; else if (!strcmp(k, "divisao")) p->div = iv;
            else if (!strcmp(k, "extensao")) p->ext = iv; else if (!strcmp(k, "base")) p->base = iv; else if (!strcmp(k, "densidade")) p->dens = iv;
            else if (!strcmp(k, "duracao")) p->dur = iv; else if (!strcmp(k, "acento")) p->acento = iv; else if (!strcmp(k, "swing")) p->swing = iv;
            else if (!strcmp(k, "relogio")) p->relogio = iv; else if (!strcmp(k, "bpm")) p->bpm = iv; else if (!strcmp(k, "canal")) p->canal = iv;
            else if (!strcmp(k, "midi")) p->midi = iv; else if (!strcmp(k, "som")) p->som = iv; else if (!strcmp(k, "timbre")) p->timbre = iv;
            else if (!strcmp(k, "volume")) p->vol = iv; else if (!strcmp(k, "raiz")) p->raiz = iv; else if (!strcmp(k, "inteira")) p->inteira = iv;
            else if (!strcmp(k, "seguir")) p->seguir = iv; else if (!strcmp(k, "ataque")) p->atk = (float) v; else if (!strcmp(k, "soltura")) p->rel = (float) v;
        }
        s += L; if (*s == '\n') s++;
    }
    par_fix(p);
}
static void par_text(const Par *p, Buf *b) {
    bfmt(b, "tonica %d\nescala %d\nordem %d\npassos %d\nmudanca %d\ndivisao %d\nextensao %d\nbase %d\ndensidade %d\nduracao %d\nacento %d\nswing %d\n"
            "relogio %d\nbpm %d\ncanal %d\nmidi %d\nsom %d\ntimbre %d\nvolume %d\nraiz %d\ninteira %d\nseguir %d\nataque %.4f\nsoltura %.4f\n",
         p->ton, p->esc, p->ordK, p->passos, p->mud, p->div, p->ext, p->base, p->dens, p->dur, p->acento, p->swing,
         p->relogio, p->bpm, p->canal, p->midi, p->som, p->timbre, p->vol, p->raiz, p->inteira, p->seguir, p->atk, p->rel);
}

/* ---------------- pastas (escolher sample) ---------------- */
static int cmpstr(const void *a, const void *b) { return strcasecmp(*(char *const *) a, *(char *const *) b); }
static int is_wav(const char *n) { size_t l = strlen(n); return l > 4 && !strcasecmp(n + l - 4, ".wav"); }
static void url_decode(char *s) {
    char *o = s;
    for (; *s; s++) {
        if (*s == '+') *o++ = ' ';
        else if (*s == '%' && s[1] && s[2]) { char hx[3] = { s[1], s[2], 0 }; *o++ = (char) strtol(hx, NULL, 16); s += 2; }
        else *o++ = *s;
    }
    *o = 0;
}
static int qparam(const char *qs, const char *key, char *out, size_t n) {
    size_t kl = strlen(key);
    for (const char *p = qs; p && *p;) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            const char *v = p + kl + 1, *e = strchr(v, '&'); size_t L = e ? (size_t) (e - v) : strlen(v);
            if (L >= n) L = n - 1;
            memcpy(out, v, L); out[L] = 0; url_decode(out); return 1;
        }
        p = strchr(p, '&'); if (p) p++;
    }
    return 0;
}
static void list_dir(TmCore *c, const char *path, TmResp *r, Buf *b) {
    char real[PATH_MAX];
    if (!path[0] || path[0] != '/' || !realpath(path, real)) { if (!realpath(c->startDir, real)) snprintf(real, sizeof real, "/"); }
    DIR *d = opendir(real);
    if (!d) { r->status = 404; bstr(b, "pasta não encontrada"); return; }
    char **dirs = calloc(800, sizeof(char *)), **files = calloc(2000, sizeof(char *)); int nd = 0, nf = 0;
    struct dirent *de;
    while (dirs && files && (de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        char full[PATH_MAX + 260]; snprintf(full, sizeof full, "%s/%s", real, de->d_name);
        struct stat st; if (stat(full, &st)) continue;
        if (S_ISDIR(st.st_mode) && nd < 800) dirs[nd++] = strdup(de->d_name);
        else if (S_ISREG(st.st_mode) && is_wav(de->d_name) && nf < 2000) files[nf++] = strdup(de->d_name);
    }
    closedir(d);
    qsort(dirs, (size_t) nd, sizeof(char *), cmpstr); qsort(files, (size_t) nf, sizeof(char *), cmpstr);
    r->type = "application/json";
    bstr(b, "{\"dir\":"); bjson(b, real); bstr(b, ",\"pastas\":[");
    for (int i = 0; i < nd; i++) { if (i) bstr(b, ","); bjson(b, dirs[i] ? dirs[i] : ""); free(dirs[i]); }
    bstr(b, "],\"wavs\":[");
    for (int i = 0; i < nf; i++) { if (i) bstr(b, ","); bjson(b, files[i] ? files[i] : ""); free(files[i]); }
    bstr(b, "]}");
    free(dirs); free(files);
}

/* ---------------- roteador da página ---------------- */
static void cfg_json(TmCore *c, Buf *b) {
    Par p = get_par(c); char nm[160] = "", pth[512] = ""; Mem mem[8];
    pthread_mutex_lock(&c->mx);
    if (c->cur) { snprintf(nm, sizeof nm, "%s", c->cur->name); snprintf(pth, sizeof pth, "%s", c->cur->path); }
    memcpy(mem, c->mem, sizeof mem);
    pthread_mutex_unlock(&c->mx);
    bfmt(b, "{\"tonica\":%d,\"escala\":%d,\"ordem\":%d,\"passos\":%d,\"mudanca\":%d,\"divisao\":%d,\"extensao\":%d,\"base\":%d,\"densidade\":%d,"
            "\"duracao\":%d,\"acento\":%d,\"swing\":%d,\"relogio\":%d,\"bpm\":%d,\"canal\":%d,\"midi\":%d,\"som\":%d,\"timbre\":%d,\"volume\":%d,"
            "\"raiz\":%d,\"inteira\":%d,\"seguir\":%d,\"ataque\":%.4f,\"soltura\":%.4f,\"amostra\":",
         p.ton, p.esc, p.ordK, p.passos, p.mud, p.div, p.ext, p.base, p.dens, p.dur, p.acento, p.swing, p.relogio, p.bpm, p.canal,
         p.midi, p.som, p.timbre, p.vol, p.raiz, p.inteira, p.seguir, p.atk, p.rel);
    bjson(b, nm); bstr(b, ",\"caminho\":"); bjson(b, pth); bstr(b, ",\"memorias\":[");
    for (int i = 0; i < 8; i++) { if (i) bstr(b, ","); if (mem[i].ok) bfmt(b, "{\"reg\":%u,\"passos\":%d}", mem[i].reg, mem[i].len); else bstr(b, "null"); }
    bstr(b, "],\"inicio\":"); bjson(b, c->startDir); bstr(b, "}");
}

void tm_http(TmCore *c, const char *method, const char *path, const char *query, const char *body, TmResp *r) {
    Buf b = { 0 };
    r->status = 200; r->type = "text/plain; charset=utf-8";
    int get = !strcmp(method, "GET"), post = !strcmp(method, "POST");
    if (!body) body = "";
    if (!query) query = "";
    if (get && !strcmp(path, "/estado")) {
        char nm[16] = "-", grau[8] = "-"; int note = __atomic_load_n(&c->aNote, __ATOMIC_RELAXED), ton = __atomic_load_n(&c->aTon, __ATOMIC_RELAXED);
        if (note > 0) { nota_nome(note, nm, sizeof nm); int iv = ((note - ton) % 12 + 12) % 12; for (int i = 0; i < 12; i++) if (CGORD[i] == iv) snprintf(grau, sizeof grau, "%s", CGOL[i]); }
        r->type = "application/json";
        bfmt(&b, "{\"reg\":%u,\"pos\":%d,\"passos\":%d,\"nota\":%d,\"nome\":\"%s\",\"grau\":\"%s\",\"gate\":%d,\"run\":%d,\"host\":%d,\"bpm\":%.2f,\"tonica\":%d,\"n\":%u}",
             __atomic_load_n(&c->aReg, __ATOMIC_RELAXED), __atomic_load_n(&c->aPos, __ATOMIC_RELAXED), __atomic_load_n(&c->aLen, __ATOMIC_RELAXED) ? __atomic_load_n(&c->aLen, __ATOMIC_RELAXED) : get_par(c).passos,
             note, nm, grau, __atomic_load_n(&c->aGate, __ATOMIC_RELAXED), __atomic_load_n(&c->aRun, __ATOMIC_RELAXED), __atomic_load_n(&c->aHost, __ATOMIC_RELAXED), __atomic_load_n(&c->aBpm100, __ATOMIC_RELAXED) / 100.0, ton,
             __atomic_load_n(&c->aSteps, __ATOMIC_ACQUIRE));
        if (!b.bad && b.n) { b.n--; b.b[b.n] = 0; }   /* tira o "}" para juntar a escala */
        Par p = get_par(c);
        bstr(&b, ",\"escnome\":"); bjson(&b, ESC_NOME[p.esc]); bstr(&b, ",\"tonnome\":"); bjson(&b, TON_NOME[ton]); bstr(&b, "}");
    } else if (get && !strcmp(path, "/cfg")) { r->type = "application/json"; cfg_json(c, &b); }
    else if (post && !strcmp(path, "/cfg")) { Par p = get_par(c); par_apply(&p, body); set_par(c, &p); bstr(&b, "ok"); }
    else if (post && !strcmp(path, "/acao")) {
        int a = 0;
        if (!strncmp(body, "novo", 4)) push_op(c, OP_NOVO, 0, 0);
        else if (!strncmp(body, "limpar", 6)) push_op(c, OP_LIMPAR, 0, 0);
        else if (sscanf(body, "bit %d", &a) == 1) push_op(c, OP_BIT, a, 0);
        else if (!strncmp(body, "tocar", 5)) push_op(c, OP_TOCAR, 0, 0);
        else if (!strncmp(body, "parar", 5)) push_op(c, OP_PARAR, 0, 0);
        else if (sscanf(body, "salvar %d", &a) == 1 && a >= 1 && a <= 8) {
            pthread_mutex_lock(&c->mx); c->mem[a - 1] = (Mem) { 1, __atomic_load_n(&c->aReg, __ATOMIC_RELAXED), c->pP.passos }; pthread_mutex_unlock(&c->mx);
        } else if (sscanf(body, "carregar %d", &a) == 1 && a >= 1 && a <= 8) {
            Mem m; pthread_mutex_lock(&c->mx); m = c->mem[a - 1]; pthread_mutex_unlock(&c->mx);
            if (m.ok) push_op(c, OP_REG, m.reg, m.len); else r->status = 404;
        } else if (sscanf(body, "apagar %d", &a) == 1 && a >= 1 && a <= 8) {
            pthread_mutex_lock(&c->mx); c->mem[a - 1].ok = 0; pthread_mutex_unlock(&c->mx);
        } else r->status = 400;
        bstr(&b, r->status == 200 ? "ok" : "?");
    } else if (get && !strcmp(path, "/pastas")) { char d[1024] = ""; qparam(query, "d", d, sizeof d); list_dir(c, d, r, &b); }
    else if (post && !strcmp(path, "/amostra")) {
        char pth[1024], err[160] = ""; snprintf(pth, sizeof pth, "%s", body);
        size_t L = strlen(pth); while (L && (pth[L - 1] == '\n' || pth[L - 1] == '\r')) pth[--L] = 0;
        if (load_sample(c, pth, 1, err, sizeof err)) { r->status = 400; bstr(&b, err); } else bstr(&b, "ok");
    } else { r->status = 404; bstr(&b, "?"); }
    if (b.bad) { free(b.b); b.b = NULL; b.n = 0; r->status = 500; }
    r->body = b.b ? b.b : strdup(""); r->len = b.b ? b.n : 0;
}

/* ---------------- estado (projeto) ---------------- */
char *tm_state_save(TmCore *c, size_t *len) {
    Buf b = { 0 }; Par p = get_par(c); char pth[512] = ""; Mem mem[8];
    pthread_mutex_lock(&c->mx); if (c->cur) snprintf(pth, sizeof pth, "%s", c->cur->path); memcpy(mem, c->mem, sizeof mem); pthread_mutex_unlock(&c->mx);
    bstr(&b, "TMLCC1\n"); par_text(&p, &b);
    bfmt(&b, "registro %u\n", __atomic_load_n(&c->aReg, __ATOMIC_RELAXED));
    for (int i = 0; i < 8; i++) if (mem[i].ok) bfmt(&b, "memoria %d %u %d\n", i + 1, mem[i].reg, mem[i].len);
    bstr(&b, "amostra "); bstr(&b, pth); bstr(&b, "\n");
    if (b.bad) { free(b.b); *len = 0; return NULL; }
    *len = b.n + 1;
    return b.b;
}
void tm_state_load(TmCore *c, const char *data, size_t len) {
    if (len < 7 || memcmp(data, "TMLCC1", 6)) return;
    char *t = malloc(len + 1); if (!t) return;
    memcpy(t, data, len); t[len] = 0;
    Par p; par_default(&p); par_apply(&p, t); set_par(c, &p);
    pthread_mutex_lock(&c->mx); memset(c->mem, 0, sizeof c->mem); pthread_mutex_unlock(&c->mx);
    char *sv = NULL;
    for (char *line = strtok_r(t, "\n", &sv); line; line = strtok_r(NULL, "\n", &sv)) {
        unsigned rg; int i, l;
        if (sscanf(line, "registro %u", &rg) == 1) { push_op(c, OP_REG, (int) (rg & 0xFFFF), 0); __atomic_store_n(&c->aReg, (uint16_t) rg, __ATOMIC_RELAXED); }
        else if (sscanf(line, "memoria %d %u %d", &i, &rg, &l) == 3 && i >= 1 && i <= 8) { pthread_mutex_lock(&c->mx); c->mem[i - 1] = (Mem) { 1, (uint16_t) rg, clampi(l, 2, 16) }; pthread_mutex_unlock(&c->mx); }
        else if (!strncmp(line, "amostra ", 8)) { char err[160]; if (line[8]) { if (load_sample(c, line + 8, 0, err, sizeof err)) publish_sample(c, NULL); } else publish_sample(c, NULL); }
    }
    free(t);
}
