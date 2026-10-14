# BoomBox Stream 🔊

Plugin para o **OBS Studio** que faz a tela inteira (ou qualquer fonte) **pulsar
como um alto-falante** no ritmo do áudio: **zoom** + **bloom** + flash de brilho
a cada batida.

## Como usar

1. Instale o plugin (veja abaixo) e reinicie o OBS.
2. Clique com o botão direito numa **fonte** → *Filtros* → **+** → **BoomBox (Reativo ao Áudio)**.
   - Para a **tela inteira** reagir, adicione o filtro na **cena** (botão direito na cena → *Filtros*).
3. Escolha a **Fonte de áudio**:
   - *Áudio da área de trabalho* (padrão) — o som global do PC;
   - *Esta fonte* — usa o próprio áudio da fonte filtrada (ex.: fonte de mídia, navegador);
   - ou qualquer outra fonte com áudio (microfone, Spotify via captura de app etc.).

## Ajustes

| Grupo | Parâmetro | O que faz |
|---|---|---|
| Áudio | Reage a | *Graves/bumbo* (passa-baixa de 2 polos) ou *faixa completa* |
| | Corte dos graves | Frequência do passa-baixa (120 Hz pega bem o bumbo) |
| | Sensibilidade | Ganho em dB antes da análise |
| | Piso de ruído | Abaixo disso nada reage |
| | Curva de resposta | >1 deixa partes calmas paradas e batidas fortes com mais *punch* |
| | Ataque / Release | Velocidade de subida / queda do envelope |
| | Elasticidade | Simula a mola do cone do falante — com overshoot e “balanço” |
| Zoom | Zoom máximo | Quanto amplia no pico (6% já é bem visível) |
| | Centro X / Y | Ponto em torno do qual o zoom acontece |
| Bloom | Bloom base | Brilho constante mesmo sem som |
| | Bloom na batida | Intensidade extra do bloom no pico |
| | Limiar do bloom | Só o que for mais claro que isso brilha |
| | Raio / Cor | Tamanho (px) e cor do glow |
| | Flash de brilho | Clareia a imagem inteira no pico |

## Compilar

Requer CMake ≥ 3.16 e o `libobs` (headers + CMake config).

### Linux

```sh
sudo apt install libobs-dev cmake build-essential   # Ubuntu/Debian
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

Instalação só para o usuário (sem sudo): copie `build/boomboxstream.so` para
`~/.config/obs-studio/plugins/boomboxstream/bin/64bit/` e a pasta `data/` para
`~/.config/obs-studio/plugins/boomboxstream/data/`.

### Windows / macOS

Compile apontando o CMake para um build do OBS Studio (que exporta o
`libobsConfig.cmake`):

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/caminho/obs-studio/build/libobs"
cmake --build build --config Release
cmake --install build --prefix "C:/Program Files/obs-studio"
```

No Windows isso coloca a DLL em `obs-plugins/64bit/` e os dados em
`data/obs-plugins/boomboxstream/`.

## Como funciona

```
thread de áudio  ─► callback de captura: mixa p/ mono → passa-baixa → soma RMS
thread de vídeo  ─► tick: RMS → dB → normaliza (piso/ganho/curva)
                          → envelope ataque/release → mola do cone
                 ─► render: shader de uma passada (data/boombox.effect)
                          zoom no UV + bloom em espiral de 32 amostras + flash
```

- `src/boombox-filter.c` — análise de áudio, física da mola e propriedades.
- `data/boombox.effect` — shader de zoom + bloom.
- `data/locale/` — textos em pt-BR e en-US.
