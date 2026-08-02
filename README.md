# POLPhone

POLPhone é uma prova técnica de softphone SIP para Windows x64, escrita em C++17 sobre PJSIP/PJSUA2 2.17. O MVP é operado por console e existe para comparar, de forma explícita e auditável, métodos de DTMF em chamadas SIP.

O repositório já contém o build reproduzível do pjproject, utilitários base, testes unitários, logging estruturado com redaction, configuração JSON, ciclo de vida completo do endpoint, transporte SIP UDP, seleção dos dispositivos de áudio WMME, registro de uma conta SIP, chamadas com áudio bidirecional, console interativo e envio DTMF explícito pelos três métodos: RFC 4733, SIP INFO e in-band.

## Funcionalidades disponíveis

- build Debug e Release para Windows x64 com Visual Studio 2022;
- `--version` e `--selftest` do endpoint, transporte UDP, codecs, áudio e 50 ciclos de
  registro/remoção do gerador in-band na conference bridge;
- `--list-devices` para enumerar captura e reprodução WMME em UTF-8;
- seleção de áudio por nome parcial ou `#<id>`, tolerante ao truncamento do WMME;
- registro SIP com retry automático, estado thread-safe e tradução dos erros mais comuns;
- chamada de saída e entrada única, com normalização estrita do destino, estados SIP e desligamento;
- áudio bidirecional pela conference bridge, com tratamento de hold/erro e log do codec/RTP;
- destruição diferida de chamadas fora das callbacks PJSIP;
- console com prompt de estado e comandos de registro, chamada, áudio, codecs e log;
- DTMF RFC 4733 (RFC 2833) com validação, pausa explícita, temporização configurável,
  guards de chamada/mídia, serialização e erro traduzido quando `telephone-event` não foi negociado;
- DTMF SIP INFO com `application/dtmf-relay`, resposta correlacionada por envio, timeout limitado e
  tradução de 415, 481 e 501;
- DTMF in-band com `pjmedia_tonegen`, pool/porta próprios, conexão direta à chamada, worker
  cancelável, timeout de segurança e aviso para codecs inadequados;
- seleção explícita do método DTMF, sem fallback automático, com logs correlacionados e
  dígitos mascarados por padrão;
- encerramento gracioso por `quit` ou `Ctrl+C` (código 130 para interrupção);
- configuração JSON com defaults, validação semântica e diagnóstico por campo;
- logging em console e arquivo, com níveis independentes e rotação;
- mascaramento de credenciais, autenticação SIP e números externos nos logs;
- testes unitários dos utilitários, da configuração e das regras de redaction.

## Fora do escopo

O MVP não inclui interface gráfica, contatos, histórico, gravação, transferência, conferência, vídeo, presença, mensagens, TLS/SRTP, STUN/TURN/ICE, múltiplas contas, múltiplas chamadas, instalador ou atualização automática.

## Pré-requisitos

- Windows 10/11 x64;
- Visual Studio 2022 17.8+ com o workload **Desenvolvimento para desktop com C++**;
- MSVC v143 e Windows SDK 10.0.22621.0 ou compatível (mínimo 10.0.19041.0);
- CMake 3.21+;
- Git 2.30+ com suporte a submodules;
- PowerShell 5.1 ou 7.x.

O build Release usa o runtime `/MD` e, em outra máquina, requer o **Microsoft Visual C++ Redistributable 2015–2022 x64**. Builds Debug usam `/MDd` e não são redistribuíveis.

## Obter o código

```powershell
git clone --recurse-submodules <URL_DO_REPOSITORIO> POLPhone
cd POLPhone
```

Se o clone já foi realizado sem os submodules:

```powershell
git submodule update --init --recursive
```

## pjproject fixado

- Repositório oficial: `https://github.com/pjsip/pjproject.git`
- Versão: tag `2.17`
- Commit: `5a457451fa2712ba18e12b01738e8ff3af2b26fd`
- Integração: Git submodule em `third_party/pjproject`
- Licença upstream: GPL-2.0-or-later ou licença comercial, conforme os termos do pjproject

O submodule nunca acompanha `master`. Para reinicializá-lo no commit registrado pelo POLPhone, use somente:

```powershell
git submodule update --init --recursive
```

Uma atualização futura exige decisão arquitetural e alteração explícita do gitlink; não use `git submodule update --remote`.

## Build no Windows x64

Execute a partir da raiz do repositório:

```powershell
.\scripts\verify-env.ps1
.\scripts\setup-pjproject.ps1 -Config Both
Copy-Item config\polphone.config.example.json config\polphone.config.json
.\scripts\build.ps1 -Config Debug
.\scripts\build.ps1 -Config Release
.\build\Release\polphone.exe --version
.\build\Release\polphone.exe --config .\config\polphone.config.json --selftest
```

O pjproject é compilado por sua solução oficial `pjproject-vs14.sln` com MSBuild, configurações `Debug-Dynamic|x64` e `Release-Dynamic|x64`, toolset v143. O CMake gera apenas a solução do POLPhone; o sistema CMake experimental do pjproject não é utilizado.

A solução gerada `build/POLPhone.sln` é descartável. Faça alterações nos arquivos CMake, não nos projetos gerados.

## Configuração local e dados sensíveis

`config/polphone.config.json` é local e ignorado pelo Git. Copie o exemplo e substitua os marcadores apenas na sua máquina. Todos os campos têm defaults; chaves desconhecidas geram aviso e valores inválidos são rejeitados com o caminho do campo. Nunca versione credenciais, logs, dumps ou capturas SIP/RTP.

Para validar somente o bootstrap e a configuração, sem registrar uma conta SIP:

```powershell
.\build\Release\polphone.exe --config .\config\polphone.config.json --selftest
```

Para iniciar o console operacional:

```powershell
.\build\Release\polphone.exe --config .\config\polphone.config.json
```

Use `help` para listar todos os comandos. O fluxo básico é `status`, `call <destino>`, `answer`,
`hangup` e `quit`. Em uma chamada confirmada com áudio ativo, envie RFC 4733 com:

```text
dtmf 5 --method rfc4733
dtmf 5 --method info
dtmf 5 --method inband --duration 250
dtmf 12,3# --duration 250 --gap 100
```

A vírgula insere uma pausa fixa de 500 ms. São aceitos `0-9`, `*`, `#` e `A-D`; duração e
intervalo por requisição não alteram os defaults. O envio in-band ocorre em segundo plano e deixa o
console responsivo; prefira PCMU/PCMA, `audio.clockRate=8000` e `audio.noVad=true` nos testes.
Os defaults da sessão podem ser alterados imediatamente, inclusive durante uma chamada, sem mudar o
arquivo JSON:

```text
dtmfmode inband
dtmfcfg duration 250
dtmfcfg gap 150
dtmfcfg volume -5
status
```

As faixas aceitas são 40–2000 ms para duração, 20–2000 ms para intervalo e -30–0 dBm0 para o
volume in-band. Uma segunda requisição enquanto outra está em voo é recusada com o ID da primeira.

Para listar os dispositivos sem exigir um arquivo de configuração local:

```powershell
.\build\Release\polphone.exe --list-devices
```

Os campos `audio.captureDevice` e `audio.playbackDevice` aceitam uma parte não ambígua do nome ou
`#<id>`. Um nome ausente gera aviso e mantém o dispositivo padrão do sistema.

Para compilar e executar os testes unitários:

```powershell
.\scripts\build.ps1 -Config Debug -Tests
.\build\Debug\polphone_tests.exe
```

## Diagnóstico e encerramento

Erros conhecidos de transporte, áudio, registro, chamada e DTMF são traduzidos para uma ação do
operador, mantendo o código PJSIP no detalhe técnico. O processo retorna `0` no encerramento normal,
`1` para configuração/argumentos, `2` para falha de inicialização, `3` para falha fatal em runtime e
`130` quando interrompido por `Ctrl+C`.

O shutdown cancela o DTMF in-band, encerra chamadas, aplica `hangupAllCalls`, solicita un-REGISTER e
destrói conta, áudio e endpoint nessa ordem. Cada espera de rede/callback tem limite de três segundos;
o arquivo de log termina com `encerramento concluído` mesmo quando um timeout exige continuar a
limpeza.

## Validação de campo DTMF

O roteiro reproduzível está em [`docs/FIELD-TEST-GUIDE.md`](docs/FIELD-TEST-GUIDE.md) e a tabela de
resultados em [`docs/TEST-MATRIX.md`](docs/TEST-MATRIX.md). A matriz permanece explicitamente como
**não executada** até haver um ensaio autorizado contra o ramal interno e a URA externa, com
observação simultânea no PABX e no tronco. Nenhum método ou duração é declarado vencedor sem essa
evidência de ponta a ponta.

Preparação validada em 2 de agosto de 2026: build Windows x64 Release concluído, versão
`POLPhone 0.1.0`/`PJSIP 2.17` conferida e `--selftest` encerrado com código `0` e marcador final. Essa
validação comprova o binário e o roteiro, não substitui o ensaio de campo.

Capturas `.pcap`/`.pcapng`, logs SIP/RTP, credenciais e números reais devem ficar fora do repositório.
Na documentação versionada, use somente aliases de destino e resultados sanitizados.

## Documentação

Arquitetura, ordem de implementação e decisões estão em `docs/`. Esses documentos são normativos para o projeto.

## Licença

POLPhone é distribuído sob a GNU General Public License, versão 2. Consulte `LICENSE`. As dependências mantêm suas próprias licenças e avisos.

## Dependências vendorizadas

- nlohmann/json `v3.12.0`, commit `55f93686c01528224f448c19128836e7df245f72` (MIT), em `third_party/nlohmann`;
- doctest `v2.4.12`, commit `1da23a3e8119ec5cce4f9388e91b065e20bf06f5` (MIT), em `third_party/doctest`.

Os headers e textos de licença são versionados; o build não baixa dependências.
