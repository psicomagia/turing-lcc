/* Host de teste para o Turing LCC: carrega o .so como a MPC faz (dlopen + VSTPluginMain), toca pads,
 * conversa com a página (HTTP), troca samples enquanto o áudio roda e confere o estado salvo.
 *   cc -O1 -g -fsanitize=address,undefined host_test.c -o host_test -ldl -lpthread -lm && ./host_test ./lccpads.so
 * Com "servir" no fim fica no ar (porta 8090) para testar a página no navegador. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

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
typedef struct {
    int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset;
    unsigned char midiData[4];
    char detune, noteOffVelocity, reserved1, reserved2;
} VstMidiEvent;
typedef struct { int32_t numEvents; intptr_t reserved; void *events[8]; } Evs;

static intptr_t master(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o) {
    (void) e; (void) i; (void) v; (void) p; (void) o;
    return op == 1 ? 2400 : 0;
}
static int fails = 0;
#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); else { printf("  FALHOU: %s\n", m); fails++; } } while (0)

static AEffect *e;
static float bl[44100], br[44100];

static void note(int on, int n, int vel, int delta) {
    VstMidiEvent m; memset(&m, 0, sizeof m);
    m.type = 1; m.byteSize = sizeof m; m.deltaFrames = delta;
    m.midiData[0] = (unsigned char) (on ? 0x90 : 0x80); m.midiData[1] = (unsigned char) n; m.midiData[2] = (unsigned char) (on ? vel : 0);
    Evs ev; ev.numEvents = 1; ev.reserved = 0; ev.events[0] = &m;
    e->dispatcher(e, 25, 0, 0, &ev, 0);
}
/* renderiza n quadros em blocos de 128, como a MPC */
static void renderN(int n) {
    for (int i = 0; i < n; i += 128) {
        float *o[2] = { bl + i, br + i };
        e->processReplacing(e, NULL, o, n - i < 128 ? n - i : 128);
    }
}
static double rms(int n) { double s = 0; for (int i = 0; i < n; i++) s += bl[i] * bl[i]; return sqrt(s / n); }
static double freq(int a, int b) {   /* cruzamentos de zero subindo */
    int c = 0, first = -1, last = -1;
    for (int i = a + 1; i < b; i++) if (bl[i - 1] < 0 && bl[i] >= 0) { if (first < 0) first = i; last = i; c++; }
    return c > 1 ? (c - 1) * 44100.0 / (last - first) : 0;
}

static int http(const char *method, const char *path, const char *body, char *out, size_t outn) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(8100); a.sin_addr.s_addr = htonl(0x7f000001);
    if (connect(s, (struct sockaddr *) &a, sizeof a)) { close(s); return -1; }
    char req[4096]; size_t bl2 = body ? strlen(body) : 0;
    int n = snprintf(req, sizeof req, "%s %s HTTP/1.1\r\nHost: x\r\nContent-Length: %zu\r\n\r\n", method, path, bl2);
    send(s, req, (size_t) n, 0); if (bl2) send(s, body, bl2, 0);
    size_t got = 0; ssize_t r;
    while (got + 1 < outn && (r = recv(s, out + got, outn - 1 - got, 0)) > 0) got += (size_t) r;
    out[got] = 0; close(s);
    int code = 0; sscanf(out, "HTTP/1.1 %d", &code);
    char *b = strstr(out, "\r\n\r\n"); if (b) memmove(out, b + 4, strlen(b + 4) + 1);
    return code;
}

static void write_wav(const char *path, double hz, double secs) {
    int n = (int) (44100 * secs);
    FILE *f = fopen(path, "wb");
    int data = n * 2, v;
    fwrite("RIFF", 1, 4, f); v = 36 + data; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); short s = 1; fwrite(&s, 2, 1, f); s = 1; fwrite(&s, 2, 1, f);
    v = 44100; fwrite(&v, 4, 1, f); v = 88200; fwrite(&v, 4, 1, f); s = 2; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (int i = 0; i < n; i++) { s = (short) (sin(2 * M_PI * hz * i / 44100) * 20000); fwrite(&s, 2, 1, f); }
    fclose(f);
}


typedef struct { double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
    int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags; } VstTimeInfo;
static VstTimeInfo ti; static int playing = 0;
static intptr_t master2(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o) {
    (void) e; (void) i; (void) v; (void) p; (void) o;
    if (op == 7) { ti.flags = (1 << 9) | (1 << 10) | (playing ? 2 : 0); ti.tempo = 120; return (intptr_t) &ti; }
    return op == 1 ? 2400 : 0;
}
static void advance(int n) { ti.ppqPos += n * 120.0 / 60.0 / 44100.0; }
static void renderT(int n) { for (int i = 0; i < n; i += 128) { float *o[2] = { bl + i, br + i }; int k = n - i < 128 ? n - i : 128; e->processReplacing(e, NULL, o, k); if (playing) advance(k); } }

int main(int argc, char **argv) {
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
    AEffect *(*entry)(audioMasterCallback) = (AEffect * (*)(audioMasterCallback)) dlsym(h, "VSTPluginMain");
    char out[1 << 16], name[64] = "";
    const char *tmp = argc > 3 ? argv[3] : "/tmp";
    puts("1) carregar como a MPC");
    e = entry(master2);
    CHECK(e && e->magic == 0x56737450 && (e->flags & (1 << 8)), "AEffect válido, instrumento");
    e->dispatcher(e, 0, 0, 0, NULL, 0); e->dispatcher(e, 10, 0, 0, NULL, 44100);
    e->dispatcher(e, 7, 0, 0, name, 0); printf("  Pagina = %s\n", name);
    CHECK(strstr(name, ":8100") != NULL, "página na porta 8100");
    e->setParameter(e, 0, 1.0f); e->setParameter(e, 0, 0.0f);
    puts("2) parado: silêncio; play do host: toca");
    renderT(44100); CHECK(rms(44100) < 1e-6, "parado, nada toca");
    int c = http("POST", "/cfg", "densidade 100\nmudanca 50\ndivisao 2\n", out, sizeof out); CHECK(c == 200, "POST /cfg");
    playing = 1; ti.ppqPos = 0; renderT(44100);
    CHECK(rms(44100) > 0.01, "com o play, a melodia toca (timbre interno)");
    c = http("GET", "/estado", NULL, out, sizeof out);
    CHECK(c == 200 && strstr(out, "\"run\":1"), "estado: tocando");
    puts("3) sample e estado salvo");
    char wav[300]; snprintf(wav, sizeof wav, "%s/tm_A3.wav", tmp); write_wav(wav, 440, 1);
    c = http("POST", "/amostra", wav, out, sizeof out); CHECK(c == 200, "sample carregado");
    renderT(22050); CHECK(rms(22050) > 0.01, "toca com o sample");
    http("POST", "/acao", "salvar 3", out, sizeof out);
    void *chunk = NULL; intptr_t n = e->dispatcher(e, 23, 0, 0, &chunk, 0);
    char *copy = malloc((size_t) n); memcpy(copy, chunk, (size_t) n);
    playing = 0; renderT(1024);
    e->dispatcher(e, 1, 0, 0, NULL, 0);
    CHECK(http("GET", "/estado", NULL, out, sizeof out) < 0, "effClose desliga a página");
    e = entry(master2); e->dispatcher(e, 0, 0, 0, NULL, 0); e->dispatcher(e, 24, 0, n, copy, 0); free(copy);
    http("GET", "/cfg", NULL, out, sizeof out);
    CHECK(strstr(out, "tm_A3.wav") && strstr(out, "\"memorias\":[null,null,{"), "sample e memória voltam");
    if (argc > 2 && !strcmp(argv[2], "servir")) {
        puts("servindo em http://127.0.0.1:8100"); playing = 1;
        for (;;) { renderT(4410); usleep(100000); }
    }
    e->dispatcher(e, 1, 0, 0, NULL, 0); dlclose(h);
    printf("\n%s (%d falhas)\n", fails ? "COM FALHAS" : "TUDO OK", fails);
    return fails != 0;
}
