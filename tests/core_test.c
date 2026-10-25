/* Testes do núcleo Turing LCC (sem host): cc -O1 -g -fsanitize=address,undefined core_test.c ../core/tm.c -lm -lpthread */
#include "../core/tm.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails = 0;
#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); else { printf("  FALHOU: %s\n", m); fails++; } } while (0)
static float L[512], R[512];
static TmMidi out[256];
typedef struct { double t; int on, ch, n, v; } Ev;
static Ev evs[20000]; static int nev;
static void http(TmCore *c, const char *m, const char *p, const char *q, const char *b, char *o, size_t n) {
    TmResp r; tm_http(c, m, p, q, b, &r); snprintf(o, n, "%s", r.body); free(r.body);
}
/* toca de a até b tempos com o "DAW" em play */
static void play(TmCore *c, double a, double b, double bpm) {
    double sr = 44100; int bs = 512;
    for (double p = a; p < b - 1e-9; p += bs * bpm / 60 / sr) {
        TmHost h = { 1, 1, p, bpm }; int no = 0;
        memset(L, 0, sizeof L); memset(R, 0, sizeof R);
        tm_process(c, bs, &h, L, R, out, &no, 256);
        for (int i = 0; i < no && nev < 20000; i++) {
            int st = out[i].m[0] & 0xF0;
            evs[nev++] = (Ev) { p + out[i].frame * bpm / 60 / sr, st == 0x90 && out[i].m[2], out[i].m[0] & 15, out[i].m[1], out[i].m[2] };
        }
    }
}
static void stop(TmCore *c) { TmHost h = { 1, 0, 0, 120 }; int no = 0; tm_process(c, 512, &h, L, R, out, &no, 256);
    for (int i = 0; i < no; i++) evs[nev++] = (Ev) { 0, (out[i].m[0] & 0xF0) == 0x90 && out[i].m[2], out[i].m[0] & 15, out[i].m[1], 0 }; }
static int balanced(void) { int on[16][128] = { { 0 } }; for (int i = 0; i < nev; i++) { if (evs[i].on) on[evs[i].ch][evs[i].n]++; else if (on[evs[i].ch][evs[i].n]-- <= 0) return 0; }
    for (int a = 0; a < 16; a++) for (int b = 0; b < 128; b++) if (on[a][b]) return 0; return 1; }
static int ons(int *notes, int max) { int k = 0; for (int i = 0; i < nev && k < max; i++) if (evs[i].on) notes[k++] = evs[i].n; return k; }

static volatile int stress;
static void *hammer(void *a) { TmCore *c = a; char o[4096]; int i = 0;
    while (__atomic_load_n(&stress, __ATOMIC_ACQUIRE)) {
        http(c, "POST", "/cfg", "", i & 1 ? "escala 3\npassos 5\n" : "escala 7\nordem 5\npassos 12\n", o, sizeof o);
        http(c, "GET", "/estado", "", "", o, sizeof o);
        http(c, "POST", "/acao", "", i % 3 ? "bit 3" : "novo", o, sizeof o);
        http(c, "POST", "/amostra", "", i & 2 ? "/tmp/tm_teste_C3.wav" : "", o, sizeof o);
        i++; }
    return NULL; }

int main(void) {
    char o[8192]; int notes[4096];
    /* WAV de teste: seno em C3 (261.63 Hz) */
    { FILE *f = fopen("/tmp/tm_teste_C3.wav", "wb"); int n = 44100, d = n * 2, v; short s;
      fwrite("RIFF", 1, 4, f); v = 36 + d; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
      s = 1; fwrite(&s, 2, 1, f); fwrite(&s, 2, 1, f); v = 44100; fwrite(&v, 4, 1, f); v = 88200; fwrite(&v, 4, 1, f); s = 2; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
      fwrite("data", 1, 4, f); fwrite(&d, 4, 1, f); for (int i = 0; i < n; i++) { s = (short) (sin(2 * M_PI * 261.6256 * i / 44100) * 20000); fwrite(&s, 2, 1, f); } fclose(f); }

    puts("1) notas sempre da escala escolhida (F Lídio)");
    TmCore *c = tm_new(44100, "/tmp");
    http(c, "POST", "/cfg", "", "tonica 5\nescala 0\npassos 16\nmudanca 50\ndensidade 100\ndivisao 2\nbase 48\nextensao 2\n", o, sizeof o);
    nev = 0; play(c, 0, 32, 120); stop(c);
    int k = ons(notes, 4096), okscale = 1, lo = 999, hi = 0;
    for (int i = 0; i < k; i++) { int iv = ((notes[i] - 5) % 12 + 12) % 12; if (!(iv == 0 || iv == 2 || iv == 4 || iv == 6 || iv == 7 || iv == 9 || iv == 11)) okscale = 0; if (notes[i] < lo) lo = notes[i]; if (notes[i] > hi) hi = notes[i]; }
    printf("  %d notas, de %d a %d\n", k, lo, hi);
    CHECK(k >= 128 && k <= 129, "densidade 100%: uma nota por semicolcheia (32 tempos = 128)");
    CHECK(okscale && lo >= 48 && hi <= 72, "todas em F Lídio, dentro de 2 oitavas a partir de 48");
    CHECK(balanced(), "toda nota ligada é desligada");

    puts("2) mudança 0% = loop fixo do tamanho dos passos");
    http(c, "POST", "/cfg", "", "mudanca 0\npassos 5\n", o, sizeof o);
    nev = 0; play(c, 40, 50, 120); stop(c); k = ons(notes, 4096);
    int rep = 1; for (int i = 16 + 5; i < k; i++) if (notes[i] != notes[i - 5]) rep = 0;   /* 16 passos até os bits antigos saírem */
    CHECK(k > 20 && rep, "a frase se repete a cada 5 passos");
    puts("3) mudança 100% = loop invertido (repete a cada 2x passos)");
    http(c, "POST", "/cfg", "", "mudanca 100\npassos 4\n", o, sizeof o);
    nev = 0; play(c, 60, 70, 120); stop(c); k = ons(notes, 4096);
    rep = 1; for (int i = 16 + 8; i < k; i++) if (notes[i] != notes[i - 8]) rep = 0;
    int same4 = 1; for (int i = 16 + 4; i < k; i++) if (notes[i] != notes[i - 4]) same4 = 0;
    CHECK(!same4, "e não repete a cada 4 (é o dobro)");
    CHECK(k > 20 && rep, "repete a cada 8 passos");

    puts("4) densidade 0% = silêncio; ordem tonal 1-3 = só T, V, II");
    http(c, "POST", "/cfg", "", "densidade 0\n", o, sizeof o);
    nev = 0; play(c, 80, 84, 120); stop(c);
    CHECK(ons(notes, 10) == 0, "nenhuma nota");
    http(c, "POST", "/cfg", "", "densidade 100\nescala 7\nordem 3\ntonica 0\nmudanca 50\n", o, sizeof o);
    nev = 0; play(c, 90, 98, 120); stop(c); k = ons(notes, 4096);
    int ok3 = 1; for (int i = 0; i < k; i++) { int pc = notes[i] % 12; if (pc != 0 && pc != 7 && pc != 2) ok3 = 0; }
    CHECK(k > 0 && ok3, "só C, G e D (ordem tonal de C)");

    puts("5) registrador: bit, memórias e estado");
    http(c, "POST", "/acao", "", "limpar", o, sizeof o); play(c, 100, 100.1, 120);
    http(c, "GET", "/estado", "", "", o, sizeof o);
    CHECK(strstr(o, "\"reg\":0") != NULL || strstr(o, "\"reg\":") != NULL, "estado responde");
    http(c, "POST", "/cfg", "", "mudanca 0\npassos 8\n", o, sizeof o);
    http(c, "POST", "/acao", "", "salvar 2", o, sizeof o);
    http(c, "GET", "/cfg", "", "", o, sizeof o);
    CHECK(strstr(o, "\"memorias\":[null,{") != NULL, "memória 2 gravada");
    size_t n; char *st = tm_state_save(c, &n);
    TmCore *c2 = tm_new(44100, "/tmp"); tm_state_load(c2, st, n); free(st);
    http(c2, "GET", "/cfg", "", "", o, sizeof o);
    CHECK(strstr(o, "\"escala\":7") && strstr(o, "\"ordem\":3") && strstr(o, "\"memorias\":[null,{"), "estado volta igual (escala, ordem, memórias)");

    puts("6) sample afinado pela raiz do nome (C3)");
    http(c2, "POST", "/amostra", "", "/tmp/tm_teste_C3.wav", o, sizeof o);
    http(c2, "GET", "/cfg", "", "", o, sizeof o);
    CHECK(strstr(o, "\"raiz\":60") && strstr(o, "tm_teste_C3.wav"), "raiz 60 lida do nome");
    http(c2, "GET", "/pastas", "d=/tmp", "", o, sizeof o);
    CHECK(strstr(o, "tm_teste_C3.wav") != NULL, "lista WAVs da pasta");

    puts("7) interface mexendo enquanto o áudio roda (troca de sample, escala, bits)");
    __atomic_store_n(&stress, 1, __ATOMIC_RELEASE); pthread_t t; pthread_create(&t, NULL, hammer, c2);
    http(c2, "POST", "/cfg", "", "densidade 100\nmudanca 50\n", o, sizeof o);
    nev = 0; play(c2, 0, 64, 160);
    __atomic_store_n(&stress, 0, __ATOMIC_RELEASE); pthread_join(t, NULL); stop(c2);
    int fin = 1; for (int i = 0; i < 512; i++) if (!isfinite(L[i])) fin = 0;
    CHECK(fin && balanced(), "sem travar, sem nota presa");

    puts("8) relógio interno e parâmetros do host");
    http(c, "POST", "/cfg", "", "relogio 1\nbpm 120\n", o, sizeof o);
    http(c, "POST", "/acao", "", "tocar", o, sizeof o);
    nev = 0; for (int i = 0; i < 100; i++) { int no = 0; tm_process(c, 441, NULL, L, R, out, &no, 256); for (int j = 0; j < no; j++) if ((out[j].m[0] & 0xF0) == 0x90 && out[j].m[2]) nev++; }
    printf("  notas em 1 s a 120 BPM (1/16): %d\n", nev);
    CHECK(nev >= 7 && nev <= 9, "8 semicolcheias por segundo");
    char d[64]; tm_param_set(c, 2, 1.0f); tm_param_display(c, 2, d, sizeof d);
    CHECK(!strcmp(d, "16"), "parâmetro Passos = 16");
    tm_param_set(c, 5, 1.0f); tm_param_display(c, 5, d, sizeof d); printf("  escala no máximo: %s\n", d);
    tm_free(c); tm_free(c2);
    printf("\n%s (%d falhas)\n", fails ? "COM FALHAS" : "TUDO OK", fails);
    return fails != 0;
}
