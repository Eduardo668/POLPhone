# POLPhone

POLPhone é um softphone SIP nativo para Windows x64, escrito em C++17 sobre PJSIP/PJSUA2 2.17. O motor continua disponível pelo console e agora possui uma primeira interface WinUI 3 em C++/WinRT. O modo de demonstração permite validar o fluxo funcional da interface sem PABX, rede ou credenciais SIP.

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
- biblioteca `polphone_core` compartilhada pela CLI e pela GUI, com fachada assíncrona;
- interface WinUI 3 branca e azul, com telefone, chamada recebida, teclado, Modo URA,
  configurações validadas e diagnóstico sanitizado;
- backend de demonstração determinístico (`polphone.exe --demo`) sem PJSIP ou rede;
- 128 testes e 710 asserções independentes de uma janela WinUI ou de um PABX.

## Fora do escopo

Ainda não há contatos, histórico, gravação, transferência, conferência, vídeo, presença, mensagens,
TLS/SRTP, STUN/TURN/ICE, múltiplas contas, múltiplas chamadas, MSIX, instalador ou atualização
automática. O modo demo não prova interoperabilidade SIP, áudio ou DTMF em uma URA real.

## Pré-requisitos

- Windows 10/11 x64;
- Visual Studio 2022 17.8+ com o workload **Desenvolvimento para desktop com C++**;
- componente **C++ WinUI app development tools**
  (`Microsoft.VisualStudio.ComponentGroup.WindowsAppDevelopment.VC.BuildTools`), necessário somente para a GUI;
- MSVC v143 e Windows SDK 10.0.22621.0 ou compatível (mínimo 10.0.19041.0);
- CMake 3.21+;
- Git 2.30+ com suporte a submodules;
- PowerShell 5.1 ou 7.x.
- acesso ao NuGet na primeira restauração da GUI. O projeto fixa Windows App SDK
  `1.6.250205002` e C++/WinRT `2.0.240405.15`.

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
.\build\Release\polphone_cli.exe --version
.\build\Release\polphone_cli.exe --config .\config\polphone.config.json --selftest
.\scripts\build-gui.ps1 -Config Debug
.\scripts\build-gui.ps1 -Config Release
```

O pjproject é compilado por sua solução oficial `pjproject-vs14.sln` com MSBuild, configurações `Debug-Dynamic|x64` e `Release-Dynamic|x64`, toolset v143. O CMake gera apenas a solução do POLPhone; o sistema CMake experimental do pjproject não é utilizado.

A solução gerada `build/POLPhone.sln` é descartável. Faça alterações nos arquivos CMake, não nos projetos gerados.

## Configuração local e dados sensíveis

`config/polphone.config.json` é local e ignorado pelo Git. Copie o exemplo e substitua os marcadores apenas na sua máquina. Todos os campos têm defaults; chaves desconhecidas geram aviso e valores inválidos são rejeitados com o caminho do campo. Nunca versione credenciais, logs, dumps ou capturas SIP/RTP.

Para validar somente o bootstrap e a configuração pela CLI, sem registrar uma conta SIP:

```powershell
.\build\Release\polphone_cli.exe --config .\config\polphone.config.json --selftest
```

Para iniciar o console operacional:

```powershell
.\build\Release\polphone_cli.exe --config .\config\polphone.config.json
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
.\build\Release\polphone_cli.exe --list-devices
```

Os campos `audio.captureDevice` e `audio.playbackDevice` aceitam uma parte não ambígua do nome ou
`#<id>`. Um nome ausente gera aviso e mantém o dispositivo padrão do sistema.

Para compilar e executar os testes unitários:

```powershell
.\scripts\build.ps1 -Config Debug -Tests
.\scripts\test.ps1 -Config Debug
```

## Interface gráfica e modo de demonstração

A GUI é unpackaged e gera `build\gui\<Config>\polphone.exe`. Para compilar e executar o modo
seguro, sem ler ou alterar a configuração SIP real:

```powershell
.\scripts\verify-env.ps1 -Gui
.\scripts\build-gui.ps1 -Config Debug
.\scripts\run-demo.ps1 -Config Debug
```

Também é possível definir `POLPHONE_DEMO=1` durante o desenvolvimento. A opção ou variável escolhe
o `MockTelephonyBackend`; ele não abre sockets, não inicializa PJSIP e não grava credenciais. O painel
“Cenários de demonstração” oferece chamada recebida, falha de registro, falha de chamada e perda de
conexão. Registro, chamada de saída, áudio simulado, cronômetro, mudo, DTMF e desligamento seguem os
controles normais.

Sem `--demo`, a GUI usa `config\polphone.config.json` ou o caminho informado por `--config`. As
alterações da tela de configurações passam pelo `ConfigValidator`, são escritas via arquivo temporário
e a configuração DTMF é aplicada ao motor em runtime; não é necessário editar o JSON nem reiniciar.
Para executar a GUI a partir de uma árvore UNC/WSL, use `scripts\run-gui.ps1 -Config Release`, que
sincroniza automaticamente o arquivo de execução com `config\polphone.config.json`. Alterações de
conta SIP que dependam de recriar a conta continuam exigindo reconexão. A senha permanece mascarada
e não entra em logs nem no diagnóstico.

### Passo a passo para abrir o POLPhone

No PowerShell, a partir da raiz do repositório:

1. Confirme que o arquivo local existe em `config\polphone.config.json`.
2. Se a GUI ainda não foi compilada nesta configuração, compile-a:

   ```powershell
   .\scripts\build-gui.ps1 -Config Release
   ```

3. Abra a GUI usando o launcher oficial:

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/run-gui.ps1 -Config Release
   ```

   No Windows, também é possível executar `scripts\run-gui.cmd -Config Release`.

4. Na janela do POLPhone, use **Configurações**, altere os campos e clique em **Salvar**.
   As alterações DTMF são aplicadas imediatamente e o launcher sincroniza o JSON local.
5. Para testar o DTMF, registre a conta, faça uma chamada e envie dígitos pelo teclado da GUI.
   Use **Diagnóstico** para conferir o método configurado, o método efetivo e o resultado do último
   envio.
6. Para encerrar, feche a janela normalmente. Não edite o JSON enquanto o POLPhone estiver aberto.

O comando `run-demo.ps1` abre somente o modo de demonstração, sem rede SIP; ele não deve ser usado
para testar registro, áudio ou DTMF contra o Issabel.

A identidade visual está centralizada em `gui/Theme.h`; a cor principal é exatamente `#0A6087`.
Detalhes da separação de processos, threads e ciclo de vida estão em
[`docs/GUI-ARCHITECTURE.md`](docs/GUI-ARCHITECTURE.md).

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

## Asterisk DTMF Lab

O laboratório opcional em `lab/asterisk/` constrói localmente um Asterisk 20.19.0 isolado, sem
troncos ou rotas externas, para testar registro fictício, eco de áudio e os três métodos DTMF antes
de qualquer ensaio autorizado em infraestrutura real. Ele não participa do build normal do
POLPhone. A operação oficial é Bash/WSL:

```bash
./scripts/lab-init.sh
./scripts/lab-up.sh --build
./scripts/lab-status.sh
./scripts/lab-logs.sh --dtmf
./scripts/lab-down.sh --volumes
```

Os scripts PowerShell são somente wrappers opcionais para WSL. Veja
[`docs/LAB-ASTERISK-GUIDE.md`](docs/LAB-ASTERISK-GUIDE.md).

## Documentação

Arquitetura, ordem de implementação e decisões estão em `docs/`. Esses documentos são normativos para o projeto.

## Licença

POLPhone é distribuído sob a GNU General Public License, versão 2. Consulte `LICENSE`. As dependências mantêm suas próprias licenças e avisos.

## Dependências vendorizadas

- nlohmann/json `v3.12.0`, commit `55f93686c01528224f448c19128836e7df245f72` (MIT), em `third_party/nlohmann`;
- doctest `v2.4.12`, commit `1da23a3e8119ec5cce4f9388e91b065e20bf06f5` (MIT), em `third_party/doctest`.

Os headers e textos de licença são versionados; o build não baixa dependências.
