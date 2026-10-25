# Turing LCC

Sequenciador no estilo Turing Machine com as escalas do Conceito Lídio Cromático.

Um registrador de 16 bits gera a melodia. A cada passo o bit que volta ao começo pode virar, com a chance definida em **Mudança**:
- 0% repete a frase de N passos;
- 50% é totalmente aleatório;
- 100% repete invertendo (frase com o dobro de passos).

As notas vêm da **tônica lídia** com a escala do anel (Lídio, Lídio aumentado, diminuto, b7, auxiliares) ou das primeiras N notas da **ordem tonal**. Toca com som próprio (timbre interno ou um WAV seu) e manda MIDI.

## Versões

| Versão | Formato | Onde roda | Página de controle |
|---|---|---|---|
| **Turing LCC (DAW)** | VST3 / AU | Reaper e outros DAWs (Mac) | Dentro da janela do plugin |
| **Turing LCC (MPC)** | VST2 | Trilha de plugin da MPC Gen1 | `http://<ip-da-mpc>:8100` |

As duas usam o mesmo núcleo em C (`core/`) e a mesma página (`web/tm.html`).

## Instalar no Mac (VST3/AU)

1. Baixe `Turing-LCC-mac` em Actions → Compilar → Artifacts e descompacte.
2. Copie os plugins para as pastas de plugins:
   - `Turing LCC.vst3` → `~/Library/Audio/Plug-Ins/VST3`
   - `Turing LCC.component` → `~/Library/Audio/Plug-Ins/Components`
3. Libere os plugins no Terminal:
   ```
   for P in ~/Library/Audio/Plug-Ins/VST3/"Turing LCC.vst3" ~/Library/Audio/Plug-Ins/Components/"Turing LCC.component"; do xattr -cr "$P"; codesign --force --deep --sign - "$P"; done
   ```
4. No Reaper, rode o re-scan de plugins e insira **Turing LCC** como instrumento. O play do Reaper comanda o sequenciador. A saída MIDI vai para outras trilhas por *sends* de MIDI (canal escolhido na página).

## Instalar na MPC

Siga o `LEIA-ME.txt` do `Turing-LCC-MPC.zip`.

## Compilar

- **DAW:** clone o JUCE 8.0.9 em `JUCE/` e rode `cmake -B build && cmake --build build`.
- **MPC:** rode `ALSA_INC=/usr/include sh mpc/build.sh` (precisa de zig).
- **Testes:** `tests/core_test.c` (núcleo) e `tests/host_test.c` (o .so carregado como a MPC faz).
