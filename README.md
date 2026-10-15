# BoomBox Stream 🔊

*Português (Brasil) · [English](README.en-US.md)*

Plugin para o **OBS Studio** que faz a tela inteira (ou qualquer fonte) **pulsar
como um alto-falante** no ritmo do áudio: **zoom** + **bloom** + flash de brilho
a cada batida.

## Opção 1 — Só um shader (para o plugin Shaderfilter) ⭐

Não precisa compilar nada: o arquivo [`shaders/boombox.shader`](shaders/boombox.shader)
roda no plugin **[Shaderfilter](https://github.com/exeldro/obs-shaderfilter)
(exeldro)** versão **2.6.0 ou mais nova**. O áudio (`audio_peak`/`audio_magnitude`)
só entrou na 2.6.0 (dez/2025); nas versões anteriores o shader carrega, mas nada reage.

1. Botão direito na fonte — ou na **cena**, para a tela inteira — → *Filtros* → **+** →
   **User-defined shader**.
2. Marque **Load shader text from file** e escolha `boombox.shader`
   (ou cole o conteúdo em *Shader text* e clique em *Reload effect*).
3. Em **Audio source** escolha a fonte de som (Desktop Audio, música, mic…).
4. Ligue **Simular batida** para ajustar sem música; depois desligue.

### Não está reagindo? (checklist)

1. **Ligue “Simular batida”.** Se a imagem pulsar, o shader está ok e o problema é o
   áudio. Se nem assim pulsar, o shader não compilou: veja *Ajuda → Arquivos de log*
   e procure por erros do `obs-shaderfilter`.
2. **Ligue “Mostrar medidor”.** Aparecem 4 barras no canto inferior esquerdo:
   vermelho = pico, azul = RMS (como chegam do OBS), amarelo = Batida e verde = Corpo
   (depois dos ajustes). A linha branca é o piso de ruído.
   - **Vermelho/azul parados em zero** → o shader não recebe áudio:
     - Confira a versão do Shaderfilter (*Ferramentas → … / Sobre*, ou o nome do
       instalador). Precisa ser **2.6.0+**: sem ela o campo **Audio source** nem
       aparece nas propriedades do filtro.
     - Em **Audio source** escolha a fonte que toca a música (ex.: *Desktop Audio*/
       *Áudio da área de trabalho*). Com *None*, ele usa o áudio da própria fonte
       filtrada, e uma cena **não** inclui o Desktop Audio.
     - O nível é medido depois do volume do mixer: a fonte não pode estar muda nem
       com o volume no zero.
   - **Vermelho/azul mexem, amarelo/verde não** → baixe o **Piso de ruído** (ex.: -50 dB),
     aumente a **Sensibilidade** ou reduza a **Curva de punch**.

| Grupo | O que faz |
|---|---|
| Áudio | Piso de ruído (dB), sensibilidade, curva de punch, simulação de batida (BPM) |
| Zoom (cone do falante) | Zoom máximo, **vibração do cone** (oscila enquanto tem som), centro X/Y |
| Bloom | Base, intensidade na batida, limiar, raio, cor e flash de brilho |
| Aberração cromática | Separação RGB radial que abre nas batidas |
| Tremor de câmera | Tremor suave (ruído) cuja força segue o áudio |

Cada efeito escolhe o que o dirige: **Corpo** (`audio_magnitude`, RMS, suave),
**Batida** (`audio_peak`, pico, rápido) ou **Mistura**. Por padrão o zoom usa a
mistura e o bloom, a aberração e o tremor reagem às batidas.

> **Limitações do modo shader:** o Shaderfilter só entrega o volume da faixa inteira
> (sem separar graves de agudos) e um shader não guarda estado entre frames. Por isso não
> há filtro de graves, ataque/release nem a mola com “balanço”. Para isso, use o plugin
> nativo abaixo.

## Opção 2 — Plugin nativo (filtro BoomBox) — cada efeito na sua faixa de frequência

Analisa o áudio por conta própria. **Cada efeito escuta a sua própria faixa de
frequência** (passa-banda Butterworth de 4ª ordem), com sensibilidade e
ataque/release próprios. Por exemplo: o zoom no bumbo, o bloom nos vocais e o
flash nos pratos.

### Instalar no Windows

Baixe da página **[Releases](https://github.com/xFreDxx/boomboxstream/releases)**:

- **Instalador (mais fácil):** `boomboxstream-X.Y.Z-windows-x64-installer.exe`. Feche o OBS e
  execute; ele encontra a pasta do OBS sozinho (pelo registro do Windows) e confere se é a
  pasta certa. Para remover: *Configurações → Aplicativos → BoomBox Stream*.
- **Manual:** extraia `boomboxstream-X.Y.Z-windows-x64.zip` dentro da pasta do OBS
  (normalmente `C:\Program Files\obs-studio`). Ficam
  `obs-plugins\64bit\boomboxstream.dll` e `data\obs-plugins\boomboxstream\…`.

Depois: botão direito na fonte ou na **cena** → *Filtros* → **+** →
**BoomBox (Reativo ao Áudio)**. Compilado contra a API do OBS 30.2, funciona no OBS 30, 31 e 32.

> **“Make sure you trust…” / “O Windows protegeu o computador”:** o instalador ainda não
> tem assinatura digital, então o Edge e o SmartScreen avisam. No Edge, clique na setinha ao
> lado de **Delete** → **Keep** → **Keep anyway**. Ao executar, clique em **Mais informações →
> Executar assim mesmo**. Para evitar os avisos, use o `.zip`.

> Builds de teste de cada commit também ficam em *Actions → build* (artefatos, exigem login
> no GitHub e expiram em 90 dias).

### Publicar uma nova versão (mantenedor)

1. Atualize `VERSION` em `project(...)` no `CMakeLists.txt` (ex.: `0.3.0`).
2. Faça commit e crie/envie a tag com o mesmo número:
   `git tag v0.3.0 && git push origin v0.3.0`. Ou crie a Release pelo GitHub
   (*Releases → Draft a new release*) com uma tag nova `v0.3.0`.
3. O CI compila Linux + Windows, gera o zip e o instalador e publica a Release.
   Se a tag não bater com a versão do `CMakeLists.txt`, o build falha de propósito.

### Como usar

1. Em **Áudio**, escolha a **Fonte de áudio**: *Áudio da área de trabalho* (padrão),
   *Esta fonte* (o áudio da própria fonte filtrada) ou qualquer outra fonte com som.
2. Ligue/desligue cada efeito pela caixinha do grupo e escolha a **Faixa de frequência**.
3. Ligue **Mostrar medidor das faixas** para ver no canto da tela quanto cada efeito
   está reagindo: laranja = zoom, branco = bloom, ciano = flash,
   magenta = aberração, verde = tremor.

### Faixas pré-definidas

| Faixa | Hz | Pega |
|---|---|---|
| Bumbo / sub | 40–120 | kick, 808 |
| Graves | 60–250 | baixo + bumbo |
| Médio-graves | 250–800 | corpo de voz e guitarras |
| Vocais | 300–3400 | voz |
| Médios | 500–4000 | voz, caixa, guitarras |
| Caixa / ataque | 1500–5000 | estalo da caixa, ataques |
| Agudos / pratos | 6000–16000 | pratos, chimbal |
| Faixa completa | tudo | — |
| **Personalizada…** | você escolhe a mínima e a máxima | — |

### Ajustes

| Grupo | Parâmetros |
|---|---|
| Áudio (global) | Fonte de áudio, piso de ruído, curva de resposta, medidor |
| Em **todo** efeito | Faixa de frequência, mínima/máxima (Hz, na personalizada), sensibilidade (dB), ataque, release |
| Zoom (cone do falante) | Zoom máximo, elasticidade (mola com “balanço”), centro X/Y — padrão: **bumbo** |
| Bloom | Intensidade na batida, base, limiar, raio, cor — padrão: **médios**, +6 dB |
| Flash de brilho | Intensidade — padrão: **agudos/pratos**, +12 dB |
| Aberração cromática | Separação RGB (px) — desligado por padrão |
| Tremor de câmera | Intensidade (px), velocidade — desligado por padrão |

> Dica: as faixas médias e agudas têm bem menos energia que os graves na música, por
> isso o bloom e o flash já vêm com sensibilidade extra. Use o medidor para equilibrar.

### Compilar

Requer CMake ≥ 3.16 e o `libobs` (headers + CMake config), ou headers + import
libs passados à mão (veja o topo do `CMakeLists.txt` e o job `windows` em
`.github/workflows/build.yml`).

#### Linux

```sh
sudo apt install libobs-dev cmake build-essential   # Ubuntu/Debian
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

Instalação só para o usuário (sem sudo): copie `build/boomboxstream.so` para
`~/.config/obs-studio/plugins/boomboxstream/bin/64bit/` e a pasta `data/` para
`~/.config/obs-studio/plugins/boomboxstream/data/`.

#### Windows / macOS

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
thread de áudio  ─► callback de captura: mixa p/ mono → 1 passa-banda por efeito
                    (Butterworth 4ª ordem: 2 biquads passa-alta + 2 passa-baixa) → RMS
thread de vídeo  ─► tick, por efeito: RMS → dB → normaliza (piso/sensibilidade/curva)
                          → envelope ataque/release; o zoom ainda passa pela mola
                 ─► render: shader de uma passada (data/boombox.effect)
                          zoom + tremor no UV, aberração RGB, bloom em espiral
                          de 32 amostras, flash e medidor
```

- `src/boombox-filter.c` — análise de áudio, física da mola e propriedades.
- `data/boombox.effect` — shader do plugin nativo (zoom, tremor, aberração, bloom, flash, medidor).
- `data/locale/` — textos em pt-BR e en-US.
- `shaders/boombox.shader` — versão só-shader para o Shaderfilter.
- `installer/boomboxstream.iss` — instalador do Windows (Inno Setup).
