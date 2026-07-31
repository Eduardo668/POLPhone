# POLPhone — Estratégia de Build (Windows x64 / Visual Studio 2022)

> Alvo único do MVP: **Windows 10/11 x64**, **Visual Studio 2022 (toolset v143)**,
> **PJSIP/PJSUA2 2.17 fixado por tag**, configurações **Debug** e **Release**.

---

## 1. Pré-requisitos

| Item | Versão | Observação |
|---|---|---|
| Windows | 10 (1809+) ou 11, x64 | build e execução |
| Visual Studio 2022 | 17.8 ou superior | Community serve |
| Workload | "Desenvolvimento para desktop com C++" | traz MSVC v143, MSBuild, CMake |
| Toolset MSVC | **v143** | `cl.exe` 19.3x |
| Windows SDK | **10.0.22621.0** ou superior | qualquer SDK 10.0.19041+ funciona; padronizar um |
| CMake | ≥ 3.21 | vem no VS; `cmake --version` |
| Git | ≥ 2.30 | com suporte a submodules |
| PowerShell | 5.1 ou 7.x | scripts em `scripts/` |

Ferramentas de diagnóstico recomendadas (não obrigatórias no build): Wireshark, `dumpbin` (vem com o VS).

`scripts/verify-env.ps1` valida tudo acima e falha cedo com mensagem clara.

---

## 2. Integração do pjproject — comparação de estratégias

### 2.1 As três opções

| Critério | **Git submodule** | **Código vendorizado** (copiado para o repo) | **Download durante o build** |
|---|---|---|---|
| Reprodutibilidade | Alta (commit fixo registrado no supermódulo) | Máxima (bytes no repositório) | Média (depende de rede e do host permanecer online) |
| Tamanho do repositório | Pequeno (~poucos KB de metadado) | Grande (pjproject ≈ 60–100 MB de fontes) | Pequeno |
| Clone inicial | Requer `--recurse-submodules` (pegadinha comum) | Um passo só | Um passo, mas build exige rede |
| Aplicar patch local no PJSIP | Possível, porém desajeitado (commit no fork) | Trivial | Complicado (patch pós-download) |
| Atualizar versão | `git -C third_party/pjproject checkout 2.18 && git add` | Recopiar tudo; diff gigante e ilegível | Mudar URL/tag no script |
| Rastreabilidade da origem | Explícita (URL + SHA) | Perde histórico upstream | Explícita, mas fora do controle de versão |
| Build offline / ambiente sem rede | Funciona após o clone | Sempre funciona | **Não funciona** |
| CI | Simples (`actions/checkout` com `submodules: recursive`) | Simples | Frágil (rate limit, indisponibilidade) |
| Higiene do repositório | Boa | Ruim: mistura código de terceiros com o nosso nos diffs e no `git blame` | Boa |
| Licenciamento/atribuição | Clara (é um repo externo referenciado) | Exige cuidado extra com avisos de licença | Clara |
| Risco de "master drift" | **Nulo** se fixado por tag/SHA | Nulo | **Alto** se o script apontar para branch |

### 2.2 Decisão

**Git submodule fixado na tag `2.17`.** Ver ADR-004 em [DECISIONS.md](DECISIONS.md).

Motivos determinantes:
- É a única opção que registra **um SHA exato** no histórico do nosso repositório sem inchá-lo;
- Mantém `git diff`/`git blame` do POLPhone limpos;
- Permite build offline depois do clone inicial;
- Torna a atualização de versão um commit de uma linha, auditável em revisão de código.

Mitigação da principal desvantagem (clone sem `--recurse-submodules`):
`scripts/verify-env.ps1` detecta `third_party/pjproject` vazio e instrui exatamente o comando a rodar;
`README.md` traz o comando de clone correto na primeira linha de instruções.

### 2.3 Fixação da versão

```bash
git submodule add https://github.com/pjsip/pjproject.git third_party/pjproject
git -C third_party/pjproject fetch --tags
git -C third_party/pjproject checkout 2.17          # tag, não branch
git -C third_party/pjproject rev-parse HEAD          # registrar o SHA no README
git add .gitmodules third_party/pjproject
```

`.gitmodules` documenta a intenção:

```ini
[submodule "third_party/pjproject"]
    path = third_party/pjproject
    url = https://github.com/pjsip/pjproject.git
    # FIXADO na tag 2.17. NÃO usar branch = master.
    # Atualização de versão é decisão arquitetural (ver docs/DECISIONS.md, ADR-004).
```

**Proibido** adicionar `branch = master` ou rodar `git submodule update --remote`.
O `README.md` e o script de setup usam sempre `git submodule update --init --recursive` (sem `--remote`).

---

## 3. Compilação do PJSIP com Visual Studio 2022

### 3.1 Visão geral do processo

O pjproject **não** usa CMake no Windows. Ele fornece uma solução MSVC (`pjproject-vs14.sln`, formato
VS2015) que o VS2022 abre e converte. O build produz bibliotecas **estáticas** (`.lib`) em
`third_party/pjproject/lib/`.

```
1. git submodule update --init --recursive
2. criar pjlib/include/pj/config_site.h            ← OBRIGATÓRIO; não existe no repositório
3. msbuild pjproject-vs14.sln /t:<projetos-de-biblioteca-requeridos>
   /p:Configuration=Debug-Dynamic /p:Platform=x64
   msbuild pjproject-vs14.sln /t:<projetos-de-biblioteca-requeridos>
   /p:Configuration=Release-Dynamic /p:Platform=x64
   (com /p:PlatformToolset=v143 e /p:WindowsTargetPlatformVersion=10.0.22621.0)
4. verificar que os .lib esperados existem em third_party/pjproject/lib
```

### 3.2 `config_site.h` — o passo que todo mundo esquece

O build **falha imediatamente** com `cannot open include file: 'pj/config_site.h'` se o arquivo não
existir. Ele é intencionalmente ausente no repositório do pjproject: é o ponto de customização do usuário.

Mantemos o nosso em `cmake/config_site.h.in` (versionado) e o script copia para
`third_party/pjproject/pjlib/include/pj/config_site.h` (ignorado pelo Git).

Conteúdo previsto para o MVP:

```c
/* POLPhone — config_site.h  (MVP: áudio, UDP, Windows x64) */
#ifndef POLPHONE_CONFIG_SITE_H
#define POLPHONE_CONFIG_SITE_H

/* --- Escopo: sem vídeo --------------------------------------------------- */
#define PJMEDIA_HAS_VIDEO                   0

/* --- Escopo: sem TLS/OpenSSL no MVP (evita dependência externa e o
       conflito de licença GPLv2 + OpenSSL). Ver ADR-003 / ADR-007. -------- */
#define PJSIP_HAS_TLS_TRANSPORT             0
#define PJ_HAS_SSL_SOCK                     0

/* --- Codecs: G.711 e G.722 bastam. Compressivos desligados para não
       mascarar o experimento de DTMF in-band. --------------------------- */
#define PJMEDIA_HAS_G711_CODEC              1
#define PJMEDIA_HAS_G722_CODEC              1
#define PJMEDIA_HAS_GSM_CODEC               0
#define PJMEDIA_HAS_SPEEX_CODEC             0
#define PJMEDIA_HAS_ILBC_CODEC              0
#define PJMEDIA_HAS_G7221_CODEC             0
#define PJMEDIA_HAS_OPUS_CODEC              0
#define PJMEDIA_HAS_L16_CODEC               0

/* --- Áudio: WMME como backend padrão do MVP ------------------------------ */
#define PJMEDIA_AUDIO_DEV_HAS_WMME          1
#define PJMEDIA_AUDIO_DEV_HAS_PORTAUDIO     0
/* A solução WinDesktop da tag 2.17 não compila wasapi_dev.cpp. Ver ADR-020. */
#define PJMEDIA_AUDIO_DEV_HAS_WASAPI        0

/* --- Rede ---------------------------------------------------------------- */
#define PJ_HAS_IPV6                         1

/* --- Limites do MVP ------------------------------------------------------ */
#define PJSUA_MAX_CALLS                     4
#define PJSUA_MAX_ACC                       2

/* --- Log: manter nível 5 disponível em Release para diagnóstico de campo -- */
#define PJ_LOG_MAX_LEVEL                    5

#endif /* POLPHONE_CONFIG_SITE_H */
```

> Cada macro acima deve ser **conferida contra `pjlib/include/pj/config.h` e
> `pjmedia/include/pjmedia/config.h` da tag 2.17** na Etapa 1. Se alguma não existir nessa versão,
> remover em vez de adivinhar.

### 3.3 Configurações da solução do pjproject

O `pjproject-vs14.sln` expõe múltiplas configurações. As relevantes:

| Configuração | Runtime C | Uso no POLPhone |
|---|---|---|
| `Debug` / `Release` | estático (`/MTd`, `/MT`) | **não usar** |
| `Debug-Dynamic` / `Release-Dynamic` | dinâmico (`/MDd`, `/MD`) | **usar estas** |
| `Debug-Static` / `Release-Static` | estático | não usar |

**Decisão: `Debug-Dynamic` e `Release-Dynamic` (runtime `/MDd` e `/MD`).** Ver ADR-006.
Motivo: `/MD` é o padrão do MSVC e do CMake para novos projetos, é o que a maioria das bibliotecas
de terceiros usa, e evita o problema de múltiplos heaps do CRT. O custo é depender do
Visual C++ Redistributable no destino — aceitável, e resolvível com `/MT` numa fase posterior de
distribuição, desde que **as duas metades sejam recompiladas juntas**.

> **A regra inviolável não é "qual runtime", é "o mesmo runtime nos dois lados".**
> Se um dia o pjproject for compilado em `Release` (`/MT`) e o POLPhone em `/MD`, o link falha com
> LNK2038 (ver §8).

### 3.4 Retargeting para v143

A solução está no formato VS2015 (toolset v140). Duas formas de resolver:

**(a) Via MSBuild, sem modificar o repositório do submodule (preferida):**

```powershell
msbuild third_party\pjproject\pjproject-vs14.sln `
    /p:Configuration=Release-Dynamic `
    /p:Platform=x64 `
    /p:PlatformToolset=v143 `
    /p:WindowsTargetPlatformVersion=10.0.22621.0 `
    /m /v:minimal
```

Vantagem decisiva: os `.vcxproj` do submodule **não são modificados**, então
`git -C third_party/pjproject status` permanece limpo e o submodule continua exatamente na tag 2.17.

**(b) Abrir a `.sln` no VS2022 e aceitar "Retarget solution".**
Funciona, mas suja o working tree do submodule. Usar apenas para depuração pontual e reverter depois
(`git -C third_party/pjproject checkout .`).

O `setup-pjproject.ps1` usa **(a)**.

O script passa a `/t:` somente os 21 projetos de biblioteca consumidos pelo POLPhone. O alvo padrão
da solução também inclui executáveis, testes, samples, bindings e projetos UWP que não são dependências
do aplicativo e não fazem parte do critério de preparação. A lista fechada e a justificativa estão no
ADR-020; cada `.lib` continua sendo validada após o MSBuild.

### 3.5 Bibliotecas produzidas

Após o build, os diretórios `lib/` de cada componente sob `third_party/pjproject/` conterão as
bibliotecas (por exemplo, `pjsip/lib`, `pjmedia/lib`, `pjlib/lib`, `pjlib-util/lib`, `pjnath/lib` e
`third_party/lib`). Os nomes têm sufixo de plataforma/toolset/config, por exemplo
`pjlib-x86_64-x64-vc14-Release-Dynamic.lib`; **o padrão exato deve ser descoberto, não codificado à
mão**. Ver ADR-019:

| Grupo | Bibliotecas |
|---|---|
| Núcleo | `pjlib`, `pjlib-util` |
| SIP | `pjsip-core`, `pjsip-simple`, `pjsip-ua`, `pjsua-lib`, `pjsua2-lib` |
| Mídia | `pjmedia`, `pjmedia-codec`, `pjmedia-audiodev`, `pjmedia-videodev` |
| NAT | `pjnath` |
| Terceiros (compilados junto) | `libsrtp`, `libresample`, `libgsmcodec`, `libspeex`, `libilbccodec`, `libg7221codec`, `libyuv`, `libwebrtc`, `libbaseclasses` |

Observações:
- `pjmedia-videodev` e `libyuv` continuam a ser gerados mesmo com `PJMEDIA_HAS_VIDEO 0`
  (viram stubs) e **ainda precisam ser linkadas** — omiti-las causa símbolos não resolvidos.
- Os nomes exatos variam por versão. O `cmake/PJSIP.cmake` deve **descobrir** os arquivos por
  `file(GLOB ...)` no diretório `lib/` filtrando pela configuração, e falhar com mensagem clara
  se o conjunto esperado não existir.

---

## 4. Build do POLPhone

### 4.1 Sistema de build

**CMake ≥ 3.21 gerando uma solução do Visual Studio 2022.** Ver ADR-005.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
cmake --build build --config Release
```

Isso produz `build/POLPhone.sln`, que abre no VS2022 com depuração completa (F5, breakpoints,
inspeção de variáveis) — atendendo ao requisito de "nativo e fácil de depurar" — e ao mesmo tempo
mantém o build scriptável e reproduzível em CI.

Alternativa considerada (`.vcxproj` escrito à mão) em ADR-005.

### 4.2 Esqueleto do `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.21)
project(POLPhone VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# /MD e /MDd — DEVE casar com Debug-Dynamic/Release-Dynamic do pjproject
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

include(cmake/PJSIP.cmake)      # define o alvo importado PJSIP::pjsua2
include(cmake/Warnings.cmake)

add_executable(polphone src/main.cpp ...)

target_compile_definitions(polphone PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
    UNICODE _UNICODE
    PJ_WIN32=1
    PJ_M_X86_64=1        # confirmar necessidade contra o config.h de 2.17
)

target_compile_options(polphone PRIVATE
    /W4 /utf-8 /EHsc /permissive-   # ver §8: /permissive- pode precisar ser removido
    $<$<CONFIG:Debug>:/Zi /Od /RTC1>
    $<$<CONFIG:Release>:/O2 /Zi>    # /Zi também em Release: PDB para diagnóstico de campo
)

target_link_options(polphone PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)

target_link_libraries(polphone PRIVATE PJSIP::pjsua2 nlohmann_json)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT polphone)
```

### 4.3 Debug × Release

| Aspecto | Debug | Release |
|---|---|---|
| Runtime | `/MDd` | `/MD` |
| pjproject | `Debug-Dynamic` | `Release-Dynamic` |
| Otimização | `/Od` | `/O2` |
| Símbolos | `/Zi` + PDB | **`/Zi` + PDB também** (essencial para diagnosticar crash em campo) |
| Checagens em runtime | `/RTC1`, `_ITERATOR_DEBUG_LEVEL=2` | desativadas |
| Nível de log padrão | console 4, arquivo 5 | console 3, arquivo 5 |
| Uso | desenvolvimento, `--selftest` | testes de campo com a URA |

> **Não misturar**: um binário Debug do POLPhone **não** pode linkar libs `Release-Dynamic` do
> pjproject (`_ITERATOR_DEBUG_LEVEL` mismatch). O `setup-pjproject.ps1` compila **as duas**
> configurações de uma vez para evitar essa armadilha.

### 4.4 Saídas

```
build/
├── POLPhone.sln
├── Debug/   polphone.exe  polphone.pdb  polphone_tests.exe
└── Release/ polphone.exe  polphone.pdb
```

O `polphone.exe` procura a configuração em `config/polphone.config.json` **relativo ao diretório
de trabalho**; `scripts/run.ps1` roda a partir da raiz do repositório e/ou aceita `--config <path>`.

---

## 5. Bibliotecas a vincular

### 5.1 Do pjproject (ordem importa para linkers estáticos)

```
pjsua2-lib
pjsua-lib
pjsip-ua
pjsip-simple
pjsip-core
pjmedia-codec
pjmedia
pjmedia-audiodev
pjmedia-videodev
pjnath
pjlib-util
libsrtp
libresample
libgsmcodec
libspeex
libilbccodec
libg7221codec
libyuv
libwebrtc
pjlib
```

Regra prática: **do mais alto nível para o mais baixo**, com `pjlib` por último.
O linker do MSVC é mais tolerante que o do GNU quanto a ordem, mas manter a ordem correta elimina
uma classe inteira de erros LNK2019 intermitentes.

### 5.2 Do Windows SDK

| Biblioteca | Por quê |
|---|---|
| `ws2_32.lib` | Winsock 2 — todo o transporte SIP/RTP |
| `mswsock.lib` | extensões de socket usadas pelo ioqueue |
| `iphlpapi.lib` | enumeração de interfaces / descoberta de IP local |
| `winmm.lib` | WMME (waveIn/waveOut) e timers multimídia |
| `dsound.lib`, `dxguid.lib` | DirectSound (backend alternativo do pjmedia-audiodev) |
| `ole32.lib`, `oleaut32.lib` | COM — usado por WASAPI e por partes do audiodev |
| `user32.lib`, `gdi32.lib` | dependências transitivas |
| `advapi32.lib` | registro/segurança |
| `netapi32.lib` | informações de rede |
| `secur32.lib` | SSPI (mesmo sem TLS, referenciado) |
| `crypt32.lib` | apenas se TLS for habilitado no futuro — **não necessário no MVP** |
| `strmiids.lib` | apenas se vídeo/DirectShow for habilitado — **não necessário no MVP** |

No CMake:

```cmake
target_link_libraries(polphone PRIVATE
    ws2_32 mswsock iphlpapi winmm dsound dxguid
    ole32 oleaut32 user32 gdi32 advapi32 netapi32 secur32)
```

### 5.3 Runtimes

| Runtime | Debug | Release | Distribuição |
|---|---|---|---|
| Universal C Runtime | `ucrtbased.dll` | `ucrtbase.dll` (componente do Windows) | já presente no Win10/11 |
| MSVC C++ Runtime | `msvcp140d.dll`, `vcruntime140d.dll` | `msvcp140.dll`, `vcruntime140.dll`, `vcruntime140_1.dll` | **exige VC++ 2015-2022 Redistributable x64** na máquina de destino |

> Binários **Debug não são redistribuíveis** (as DLLs `*d.dll` só existem em máquinas com VS instalado).
> Os testes de campo com a URA devem usar o binário **Release** + PDB.
> Se a dependência do Redistributable se tornar um problema operacional, a alternativa é migrar
> tudo para `/MT` — recompilando **pjproject e POLPhone juntos** (ADR-006).

---

## 6. Scripts necessários

Todos em PowerShell, em `scripts/`, idempotentes, com `$ErrorActionPreference = "Stop"` e
código de saída ≠ 0 em falha.

| Script | Responsabilidade |
|---|---|
| `verify-env.ps1` | Verifica VS2022 (via `vswhere`), toolset v143, Windows SDK, CMake ≥ 3.21, Git, e se `third_party/pjproject` está populado **e na tag 2.17**. Imprime um relatório e falha com instrução acionável. |
| `setup-pjproject.ps1` | `git submodule update --init --recursive`; confere que `HEAD` do submodule é a tag 2.17; copia `cmake/config_site.h.in` → `pjlib/include/pj/config_site.h`; roda MSBuild para os projetos de biblioteca requeridos em `Debug-Dynamic\|x64` e `Release-Dynamic\|x64`; valida a existência dos `.lib` esperados; imprime resumo. Aceita `-Clean` e `-Config <Debug\|Release\|Both>`. |
| `build.ps1` | `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` + `cmake --build build --config <cfg>`. Aceita `-Config`, `-Clean`, `-Tests`. |
| `run.ps1` | Executa `build/<cfg>/polphone.exe` a partir da raiz, com `--config config/polphone.config.json`. Cria `logs/` se não existir. |
| `clean.ps1` | Remove `build/`, `logs/*.log` e, com `-All`, também os artefatos do pjproject (`lib/`, `bin/`, `obj/`). |

Sequência para um clone novo:

```powershell
git clone --recurse-submodules <url> POLPhone
cd POLPhone
.\scripts\verify-env.ps1
.\scripts\setup-pjproject.ps1        # demorado (10-30 min na primeira vez)
Copy-Item config\polphone.config.example.json config\polphone.config.json
# editar config\polphone.config.json com as credenciais locais
.\scripts\build.ps1 -Config Debug -Tests
.\scripts\run.ps1
```

---

## 7. `cmake/PJSIP.cmake` — como localizar as libs

Requisitos do módulo:

1. Receber a configuração (`Debug`/`Release`) e mapear para o sufixo do pjproject
   (`Debug-Dynamic`/`Release-Dynamic`).
2. Descobrir recursivamente `.lib` apenas nos diretórios `lib/` dos componentes sob
   `third_party/pjproject`, filtrando pelo sufixo da configuração (ADR-019).
3. Validar que **todas** as bibliotecas da lista de §5.1 foram encontradas; se faltar alguma,
   `message(FATAL_ERROR "...  Rode scripts/setup-pjproject.ps1 -Config <cfg> primeiro.")`.
4. Montar um alvo importado `PJSIP::pjsua2` com:
   - `INTERFACE_INCLUDE_DIRECTORIES`: `pjlib/include`, `pjlib-util/include`, `pjnath/include`,
     `pjmedia/include`, `pjsip/include`;
   - `IMPORTED_LOCATION_DEBUG` / `IMPORTED_LOCATION_RELEASE` e `INTERFACE_LINK_LIBRARIES`
     com as demais libs + as libs do SDK.
5. Nunca codificar o nome completo do arquivo `.lib` — o sufixo muda entre versões do pjproject.

---

## 8. Problemas comuns de compilação no Windows

| # | Sintoma | Causa | Solução |
|---|---|---|---|
| 1 | `fatal error C1083: Cannot open include file: 'pj/config_site.h'` | Arquivo não criado | Rodar `setup-pjproject.ps1` (copia de `cmake/config_site.h.in`) |
| 2 | `LNK2038: mismatch detected for 'RuntimeLibrary': value 'MT_StaticRelease' doesn't match value 'MD_DynamicRelease'` | pjproject em `Release`, app em `/MD` | Compilar pjproject em `Release-Dynamic`; conferir `CMAKE_MSVC_RUNTIME_LIBRARY` |
| 3 | `LNK2038: mismatch detected for '_ITERATOR_DEBUG_LEVEL': value '0' doesn't match value '2'` | Binário Debug linkando libs Release | Compilar as duas configs do pjproject; nunca cruzar |
| 4 | `LNK1112: module machine type 'x86' conflicts with target machine type 'x64'` | pjproject compilado em Win32 | `/p:Platform=x64` no MSBuild; `-A x64` no CMake |
| 5 | `error C2065: 'max'/'min'` ou `C2589` em headers do PJSIP/STL | macros de `windows.h` | `NOMINMAX` (já em §4.2) |
| 6 | `error C2011: 'fd_set': 'struct' type redefinition` / `WSA*` duplicado | `windows.h` incluído antes de `winsock2.h` | Incluir `pjsua2.hpp` **antes** de qualquer header do Windows; usar `WIN32_LEAN_AND_MEAN` |
| 7 | Erros de conformidade dentro de `pjsua2/*.hpp` | `/permissive-` | Remover `/permissive-` do alvo (o pjsua2 não é garantidamente conforme ao modo estrito) |
| 8 | `LNK2019: unresolved external symbol __imp_waveOutOpen` (e similares) | Falta lib do SDK | Adicionar `winmm`, `dsound`, `dxguid`, `ws2_32`, `mswsock`, `iphlpapi`, `ole32` |
| 9 | `LNK2019` em símbolos `pjmedia_vid_*` / `libyuv` | Não linkou `pjmedia-videodev`/`libyuv` mesmo com vídeo desligado | Linkar mesmo assim (viram stubs) |
| 10 | `error C4996: 'strdup': The POSIX name...` | CRT seguro | `_CRT_SECURE_NO_WARNINGS` |
| 11 | Acentos corrompidos nos nomes de dispositivo | Console em CP-850/1252 e strings UTF-8 do PJSIP | `SetConsoleOutputCP(CP_UTF8)` no `main`; `/utf-8` no compilador |
| 12 | Build do pjproject falha com "path too long" | Caminho profundo + nomes longos do pjproject | Clonar em caminho curto (`C:\dev\POLPhone`); habilitar long paths no Windows/Git |
| 13 | Build do pjproject falha aleatoriamente com `/m` | Paralelismo + dependências implícitas | Rodar sem `/m` na primeira vez, ou `/m:1` |
| 14 | Build muito lento / arquivos "sumindo" | Windows Defender varrendo `obj/` e `lib/` | Exceção de pasta para o diretório do projeto |
| 15 | `MSB8020: The build tools for v140 cannot be found` | Solução não retargetada | `/p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0.22621.0` |
| 16 | `polphone.exe` não inicia: "vcruntime140.dll não encontrado" | Falta VC++ Redistributable no destino | Instalar o Redistributable x64 ou migrar para `/MT` |
| 17 | *Assert* do heap ao encerrar / travamento no `exit` | `libDestroy()` não chamado ou ordem de destruição errada | Ver ARCHITECTURE §5.3 |
| 18 | `throw` dentro de callback → `std::terminate` | Exceção cruzando a fronteira C do PJSIP | `noexcept` + try/catch em toda callback |
| 19 | `error LNK2001: unresolved external symbol "public: virtual ... pj::LogWriter"` | RTTI/exceções desabilitadas | Manter `/EHsc` e RTTI ligado |
| 20 | Warnings viram erros e travam o build do pjproject | `/WX` herdado | Não usar `/WX` nos alvos do pjproject |

---

## 9. Critérios de aceitação do build

| # | Critério | Comando de verificação |
|---|---|---|
| B1 | `verify-env.ps1` passa em máquina limpa com VS2022 | `.\scripts\verify-env.ps1` |
| B2 | Submodule está exatamente na tag 2.17 | `git -C third_party/pjproject describe --tags` → `2.17` |
| B3 | `setup-pjproject.ps1` gera as libs Debug **e** Release | `Get-ChildItem third_party\pjproject\lib\*.lib` |
| B4 | `build.ps1 -Config Debug` compila sem erros | exit code 0 |
| B5 | `build.ps1 -Config Release` compila sem erros | exit code 0 |
| B6 | Binário é x64 | `dumpbin /headers build\Release\polphone.exe \| Select-String machine` |
| B7 | `polphone.exe --version` imprime versão do app e do PJSIP | execução |
| B8 | `polphone.exe --selftest` inicia e finaliza o PJSUA2 sem crash nem vazamento | exit code 0; log sem `FATAL` |
| B9 | Nenhum arquivo do submodule modificado após o build | `git -C third_party/pjproject status --porcelain` vazio |
| B10 | Repositório limpo após build (sem artefatos versionados) | `git status --porcelain` vazio |
| B11 | Testes unitários passam | `build\Debug\polphone_tests.exe` |
| B12 | Build reproduzível a partir de clone limpo | repetir a sequência da §6 em diretório novo |
