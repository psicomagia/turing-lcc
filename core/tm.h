/* Turing LCC - núcleo (C puro, usado pela versão MPC/VST2 e pela versão VST3/AU)
 *
 * Sequenciador no estilo Turing Machine (registrador de deslocamento de 16 bits com "trava"),
 * com as notas escolhidas pelo Conceito Lídio Cromático: tônica lídia + escala do anel
 * (Lídio, aumentado, diminuto, b7, auxiliares) ou as primeiras N notas da ordem tonal.
 * Toca com som próprio (sample WAV ou timbre interno) e gera MIDI.
 */
#ifndef TM_H
#define TM_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct TmCore TmCore;
typedef struct { int hasHost, playing; double ppq, bpm; } TmHost;   /* posição do DAW no início do bloco */
typedef struct { int frame; uint8_t m[3]; } TmMidi;

TmCore *tm_new(double sampleRate, const char *startDir);   /* startDir: pasta inicial do navegador de samples */
void tm_free(TmCore *c);
void tm_set_rate(TmCore *c, double sampleRate);

/* ---- thread de áudio ---- */
void tm_midi_in(TmCore *c, const TmMidi *ev, int n);      /* chamado antes de tm_process, no mesmo bloco */
/* L/R são somados (não zerados); out recebe o MIDI gerado (frame dentro do bloco) */
void tm_process(TmCore *c, int n, const TmHost *h, float *L, float *R, TmMidi *out, int *nout, int maxout);
int tm_midi_out_enabled(TmCore *c);                        /* "mandar MIDI" ligado */
int tm_midi_out_channel(TmCore *c);                        /* 0-15 */

/* ---- interface (qualquer outra thread) ---- */
typedef struct { int status; const char *type; char *body; size_t len; } TmResp;   /* body: malloc, quem chama libera */
void tm_http(TmCore *c, const char *method, const char *path, const char *query, const char *body, TmResp *r);
char *tm_state_save(TmCore *c, size_t *len);               /* malloc */
void tm_state_load(TmCore *c, const char *data, size_t len);

/* parâmetros para a tela do host / Q-Links */
int tm_param_count(void);
const char *tm_param_name(int i);
float tm_param_get(TmCore *c, int i);                      /* 0..1 */
void tm_param_set(TmCore *c, int i, float v);
void tm_param_display(TmCore *c, int i, char *out, size_t n);
void tm_set_page_info(TmCore *c, const char *text);        /* o que o parâmetro 0 mostra (ex.: endereço da página) */

#ifdef __cplusplus
}
#endif
#endif
