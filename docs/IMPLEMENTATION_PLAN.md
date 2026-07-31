# POLPhone — Plano de Implementação (MVP Console)

> Plano executável em etapas pequenas, sequenciais e verificáveis.
> Cada etapa é uma unidade de trabalho fechada, com critério de conclusão objetivo e comando de
> validação. Nenhuma etapa depende de código que ainda não existe.
>
> Leitura obrigatória antes de começar: [ARCHITECTURE.md](ARCHITECTURE.md),
> [BUILD-STRATEGY.md](BUILD-STRATEGY.md), [DTMF-DESIGN.md](DTMF-DESIGN.md).

---

## 0. Como usar este plano

- **Uma etapa = um commit** (ou um PR pequeno). Não agrupar etapas.
- Nenhuma etapa pode ser marcada como concluída sem o comando de validação executado com sucesso.
- Regra de escopo: se algo não está listado em "Escopo", **não implementar**. O item provavelmente
  pertence a uma etapa posterior ou está fora do MVP.
- Etapas 1 e 2 são de infraestrutura e não produzem funcionalidade — não pule.
- Etapas 12, 13 e 14 são o núcleo da prova técnica; tudo antes existe para viabilizá-las.

**Grafo de dependências:**

```
                          ┌─► E05 (logging) ─┐
E01 ─► E02 ─► E03 ─► E04 ─┤                  ├─► E07 ─┬─► E08 (áudio devs) ─┐
                          └─► E06 (config) ──┘        └─► E09 (registro) ───┴─► E10 ─► E11
                                                                                        │
   E12 (console) ◄──────────────────────────────────────────────────────────────────────┘
     │
     └─► E13 (RFC 4733) ─► E14 (SIP INFO) ─► E15 (in-band) ─► E16 (duração/duplicidade) ─► E17 ─► E18
```

- E05 e E06 são independentes entre si e podem ser feitas em qualquer ordem.
- E08 e E09 são independentes entre si; ambas exigem E07.
- E13 → E14 → E15 é sequência **obrigatória**: cada método reusa os *guards* e o log do anterior.

---

## Etapa 01 — Esqueleto do repositório e licenciamento

**Objetivo:** repositório navegável, licenciado e com higiene de segredos garantida desde o primeiro commit.

**Depende de:** nada.

**Arquivos previstos:**
```
LICENSE                       GNU GPL v2 (texto integral)
README.md                     propósito, escopo, clone com submodules, build, execução
.gitignore                    conforme ARCHITECTURE §11
.gitattributes                * text=auto eol=crlf para .ps1/.sln/.vcxproj
docs/                         (já existente)
config/polphone.config.example.json
src/ scripts/ tests/ third_party/ cmake/   (com .gitkeep onde vazio)
```

**Escopo:**
- Texto integral da GPL v2 em `LICENSE`; cabeçalho de licença padronizado a ser usado em cada `.cpp/.h`.
- `README.md` com: o que é, o que **não** é (lista de não-funcionalidades), pré-requisitos,
  sequência de build da BUILD-STRATEGY §6, aviso de que `config/polphone.config.json` nunca é versionado.
- `.gitignore` cobrindo `build/`, `logs/`, `config/polphone.config.json`, artefatos do pjproject e do VS.
- `config/polphone.config.example.json` com **valores fictícios explícitos** (`REPLACE_ME`), nunca dados reais.

**Fora de escopo:** qualquer código C++.

**Critério de conclusão:**
- `git status --porcelain` limpo após criar `config/polphone.config.json` a partir do exemplo
  (ou seja, o arquivo real é ignorado).
- Nenhuma string de credencial, IP interno ou número real no repositório.

**Comandos de validação:**
```powershell
Copy-Item config\polphone.config.example.json config\polphone.config.json
git status --porcelain                      # deve estar vazio
git grep -n -iE "senha|password.*[^*]|@(10|172|192)\." -- . ':!docs' ':!LICENSE'   # sem resultados reais
```

**Tarefa Codex:** *"Crie o esqueleto do repositório POLPhone: LICENSE (GPL-2.0 integral), README.md
em português com escopo e instruções de build, .gitignore, .gitattributes e a árvore de diretórios de
ARCHITECTURE §11 com .gitkeep. Crie config/polphone.config.example.json com placeholders REPLACE_ME.
Não escreva código C++."*

---

## Etapa 02 — Submodule do pjproject e build das bibliotecas

**Objetivo:** ter `pjsua2` compilada e linkável, em Debug e Release, a partir da tag 2.17.

**Depende de:** E01.

**Arquivos previstos:**
```
.gitmodules
third_party/pjproject/                (submodule @ tag 2.17)
cmake/config_site.h.in
scripts/verify-env.ps1
scripts/setup-pjproject.ps1
```

**Escopo:**
- Adicionar o submodule **fixado na tag 2.17** (BUILD-STRATEGY §2.3). Registrar o SHA no README.
- Escrever `cmake/config_site.h.in` conforme BUILD-STRATEGY §3.2, **conferindo cada macro contra
  `pjlib/include/pj/config.h` e `pjmedia/include/pjmedia/config.h` da tag 2.17**. Remover as que não existirem.
- `verify-env.ps1`: VS2022 via `vswhere`, toolset v143, Windows SDK, CMake ≥ 3.21, Git, submodule
  populado e na tag correta. Relatório + exit code.
- `setup-pjproject.ps1`: init do submodule, cópia do `config_site.h`, MSBuild com
  `/p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=<sdk>` para `Debug-Dynamic|x64` e
  `Release-Dynamic|x64`, validação da lista de `.lib` de BUILD-STRATEGY §5.1.

**Fora de escopo:** CMake do POLPhone, qualquer código C++.

**Critério de conclusão:**
- Ambas as configurações geram as libs esperadas.
- `git -C third_party/pjproject status --porcelain` **vazio** (o `config_site.h` está no `.gitignore`
  e o retarget é feito por linha de comando, sem alterar os `.vcxproj`).

**Comandos de validação:**
```powershell
.\scripts\verify-env.ps1
git -C third_party\pjproject describe --tags              # 2.17
.\scripts\setup-pjproject.ps1 -Config Both
Get-ChildItem third_party\pjproject\lib\*.lib | Measure-Object   # > 15 arquivos
git -C third_party\pjproject status --porcelain           # vazio
```

**Tarefa Codex:** *"Adicione o pjproject como submodule fixado na TAG 2.17 (nunca master). Crie
cmake/config_site.h.in validando cada macro contra os headers reais da tag. Crie
scripts/verify-env.ps1 e scripts/setup-pjproject.ps1 conforme BUILD-STRATEGY §3 e §6, usando MSBuild
com PlatformToolset=v143 e configurações Debug-Dynamic/Release-Dynamic x64, sem modificar os
arquivos do submodule."*

---

## Etapa 03 — CMake, "hello PJSIP" e validação do toolchain

**Objetivo:** provar que o POLPhone linka contra o PJSUA2 antes de escrever qualquer lógica.

**Depende de:** E02.

**Arquivos previstos:**
```
CMakeLists.txt
CMakePresets.json
cmake/PJSIP.cmake
cmake/Warnings.cmake
scripts/build.ps1
scripts/run.ps1
scripts/clean.ps1
src/main.cpp                    (mínimo)
third_party/nlohmann/json.hpp   (vendorizado)
third_party/doctest/doctest.h   (vendorizado)
```

**Escopo:**
- `CMakeLists.txt` conforme BUILD-STRATEGY §4.2 (C++17, `/MD`, `NOMINMAX`, `WIN32_LEAN_AND_MEAN`,
  `/utf-8`, `/EHsc`, PDB em Release).
- `cmake/PJSIP.cmake` conforme BUILD-STRATEGY §7 — descoberta por GLOB, erro claro se faltar lib.
- `main.cpp` mínimo: `SetConsoleOutputCP(CP_UTF8)`, imprime versão do POLPhone e
  `pj_get_version()`, cria e destrói o Endpoint (`libCreate` → `libInit` → `libDestroy`) sob
  `--selftest`, e retorna 0.
- Vendorizar `nlohmann/json.hpp` e `doctest.h` com os respectivos avisos de licença (MIT).

**Fora de escopo:** transporte, conta, chamada, config, logging estruturado.

**Critério de conclusão:** binário x64 nas duas configurações; `--selftest` retorna 0 sem crash.

**Comandos de validação:**
```powershell
.\scripts\build.ps1 -Config Debug
.\scripts\build.ps1 -Config Release
.\build\Release\polphone.exe --version
.\build\Release\polphone.exe --selftest ; $LASTEXITCODE      # 0
dumpbin /headers .\build\Release\polphone.exe | Select-String "machine"   # x64
```

**Tarefa Codex:** *"Crie o CMakeLists.txt, CMakePresets.json, cmake/PJSIP.cmake, cmake/Warnings.cmake
e scripts build/run/clean. Escreva um src/main.cpp mínimo que imprima a versão do PJSIP e, com
--selftest, execute libCreate/libInit/libDestroy retornando 0. Vendorize nlohmann/json.hpp e
doctest.h. Não implemente nenhuma funcionalidade SIP."*

---

## Etapa 04 — Utilitários base: `Result`, `Strings`, `Time` + harness de testes

**Objetivo:** ter o alvo de testes funcionando desde cedo, com as primeiras unidades puras.

**Depende de:** E03.

**Arquivos previstos:**
```
src/util/Result.h
src/util/Strings.h / .cpp
src/util/Time.h / .cpp
tests/CMakeLists.txt
tests/test_main.cpp
tests/test_strings.cpp
```

**Escopo:**
- `Result<T>` estilo `expected`: valor **ou** `{código enum, mensagem, detalhe}`. Sem exceções.
- `Strings`: `trim`, `split`, `startsWith`, `iequals`, `maskMiddle(s, keepLast)`, `toUtf8Console`.
- `Time`: timestamp ISO-8601 com fuso, monotônico em ms.
- Alvo `polphone_tests` com doctest.

**Critério de conclusão:** `polphone_tests.exe` executa e passa.

**Comandos de validação:**
```powershell
.\scripts\build.ps1 -Config Debug -Tests
.\build\Debug\polphone_tests.exe ; $LASTEXITCODE   # 0
```

**Tarefa Codex:** *"Implemente src/util/Result.h, Strings e Time, e configure o alvo de testes
polphone_tests com doctest em tests/CMakeLists.txt. Escreva testes unitários para todas as funções
de Strings, incluindo maskMiddle. Sem dependência de PJSIP."*

---

## Etapa 05 — Logging e redaction

**Objetivo:** logging estruturado, thread-safe e com mascaramento **antes** de qualquer coisa que
possa vazar segredo.

**Depende de:** E04.

**Arquivos previstos:**
```
src/logging/Logger.h / .cpp
src/logging/Redactor.h / .cpp
src/logging/PjLogWriter.h / .cpp
tests/test_redactor.cpp
```

**Escopo:** ARCHITECTURE §10 integralmente.
- `Logger`: dois destinos (console/arquivo), níveis 0–6, categorias, mutex, rotação diária + 50 MB,
  `flush` imediato para níveis ≤ 2.
- `Redactor`: **funções puras**, todas as regras da tabela ARCHITECTURE §10.3.
- `PjLogWriter : pj::LogWriter` — `write(const pj::LogEntry&)`, aplica `Redactor`, delega ao `Logger`,
  nunca chama PJSUA2.
- `main.cpp` passa a inicializar o `Logger` antes de tudo e a destruí-lo por último.

**Critério de conclusão:** testes do `Redactor` cobrem todas as regras (incluindo casos negativos:
ramal interno não mascarado, `Call-ID` preservado).

**Comandos de validação:**
```powershell
.\build\Debug\polphone_tests.exe -ts=redactor
.\build\Debug\polphone.exe --selftest --log-level 5
Select-String -Path .\logs\*.log -Pattern "password|response=\"[a-f0-9]" # sem resultados
```

**Tarefa Codex:** *"Implemente Logger, Redactor e PjLogWriter conforme ARCHITECTURE §10. O Redactor
deve ser composto por funções puras e ter teste unitário para cada regra da tabela §10.3, incluindo
os casos em que NÃO deve mascarar. Integre o PjLogWriter em EpConfig.logConfig.writer no --selftest."*

---

## Etapa 06 — Configuração: modelo, carregamento e validação

**Objetivo:** ler a conta SIP e todos os parâmetros de um JSON local ignorado pelo Git.

**Depende de:** E04 (pode ser paralela a E05).

**Arquivos previstos:**
```
src/config/AppConfig.h
src/config/ConfigLoader.h / .cpp
src/config/ConfigValidator.h / .cpp
config/polphone.config.example.json      (versão final)
tests/test_config_loader.cpp
tests/test_config_validator.cpp
```

**Escopo:** estrutura completa da §2 deste documento; parse com `nlohmann::json`; defaults;
validação semântica separada; `redactedDump()`.

**Critério de conclusão:**
- JSON válido → `AppConfig` correto; JSON inválido → erro apontando **o campo**.
- Ausência do arquivo → mensagem instruindo copiar o `.example` + exit 1.
- `redactedDump()` nunca contém a senha.

**Comandos de validação:**
```powershell
.\build\Debug\polphone_tests.exe -ts=config
.\build\Debug\polphone.exe --config .\config\nao-existe.json ; $LASTEXITCODE   # 1
.\build\Debug\polphone.exe --config .\config\polphone.config.json --selftest
```

**Tarefa Codex:** *"Implemente AppConfig, ConfigLoader (nlohmann/json) e ConfigValidator conforme
IMPLEMENTATION_PLAN §2. Separe parse de validação. Erros devem citar o caminho JSON do campo.
Implemente redactedDump() e teste que a senha nunca aparece. Finalize
config/polphone.config.example.json com placeholders."*

---

## Etapa 07 — `SipEndpoint`: ciclo de vida e transporte UDP

**Objetivo:** requisitos 1 e 2 do MVP.

**Depende de:** E05, E06.

**Arquivos previstos:**
```
src/sip/SipEndpoint.h / .cpp
src/sip/PjErrors.h / .cpp
src/app/Application.h / .cpp   (esqueleto: initialize/run/shutdown)
```

**Escopo:**
- `SipEndpoint` RAII conforme ARCHITECTURE §3.6; `EpConfig` montado a partir do `AppConfig`
  (ARCHITECTURE §5.1 item 7), incluindo `medConfig.noVad = true`.
- Transporte UDP com porta de `network.localPort` (0 = automática); logar IP:porta efetivos.
- `PjErrors::describe` + `PJ_TRY`.
- `Application::shutdown()` idempotente e chamado por RAII em todos os caminhos de erro.
- `applyCodecPriorities()` + log do mapa de codecs efetivo.

**Critério de conclusão:** `--selftest` sobe endpoint + transporte + start + destroy, imprime porta
local e lista de codecs, retorna 0, sem *assert* de heap ao sair.

**Comandos de validação:**
```powershell
.\build\Debug\polphone.exe --selftest --log-level 5 ; $LASTEXITCODE   # 0
Select-String -Path .\logs\*.log -Pattern "transport|codec"
# repetir 10x seguidas para detectar instabilidade de shutdown
1..10 | % { .\build\Debug\polphone.exe --selftest | Out-Null; $LASTEXITCODE }
```

**Tarefa Codex:** *"Implemente SipEndpoint (RAII sobre pj::Endpoint) e PjErrors, e o esqueleto de
Application com initialize/run/shutdown seguindo a ordem de ARCHITECTURE §5.1 e §5.3. O --selftest
deve criar endpoint, transporte UDP, iniciar, aplicar prioridades de codec, logar porta e codecs, e
destruir tudo na ordem inversa. shutdown() deve ser idempotente."*

---

## Etapa 08 — Dispositivos de áudio: listagem e seleção

**Objetivo:** requisito 9.

**Depende de:** E07.

**Arquivos previstos:**
```
src/audio/AudioDeviceService.h / .cpp
```

**Escopo:** ARCHITECTURE §3.10 e §8.3 — `enumDev2()`, classificação entrada/saída,
`resolveByName` por substring tolerante a truncamento de 31 chars do WMME,
`setCaptureDev`/`setPlaybackDev`, recusa de troca durante chamada, tratamento de
`PJMEDIA_EAUD_NODEFDEV`.

**Critério de conclusão:** `--list-devices` imprime a lista com acentuação correta; a seleção por
nome do JSON funciona e é logada; nome inexistente gera erro claro e não fatal.

**Comandos de validação:**
```powershell
.\build\Debug\polphone.exe --list-devices
# alterar audio.captureDevice no JSON para um nome parcial e verificar no log a resolução
```

**Tarefa Codex:** *"Implemente AudioDeviceService conforme ARCHITECTURE §3.10/§8.3. Adicione a flag
--list-devices ao main. A resolução por nome deve ser case-insensitive, por substring, e cair no
índice quando o nome não resolver, sempre registrando qual dispositivo foi efetivamente escolhido."*

---

## Etapa 09 — `SipAccount` e registro

**Objetivo:** requisitos 3, 4 e 5.

**Depende de:** E07.

**Arquivos previstos:**
```
src/sip/SipAccount.h / .cpp
src/app/AppState.h / .cpp
src/app/EventQueue.h
```

**Escopo:**
- `AccountConfig` a partir do JSON (`idUri`, `registrarUri`, `AuthCredInfo`, `timeoutSec`,
  `retryIntervalSec`, proxy opcional).
- `onRegState` **`noexcept`**, publicando `UiEvent` na fila; nunca imprime direto.
- `onIncomingCall` mínimo: `486 Busy` se ocupado, senão `180 Ringing` (a chamada em si vem na E10).
- Tradução dos códigos 401/403/404/408/503 (ARCHITECTURE §9.3).
- `AppState` com o estado de registro consultável por `status`.

**Critério de conclusão:** contra um ramal real do Issabel: registro `200 OK` exibido no console em
até 5 s; credencial errada exibe `401`/`403` traduzido e **não** derruba o app; PABX inalcançável
exibe timeout traduzido e mantém retry.

**Comandos de validação:**
```powershell
.\build\Debug\polphone.exe --config .\config\polphone.config.json
# no console: status
# testes negativos: senha errada no JSON; host inválido no JSON
Select-String -Path .\logs\*.log -Pattern "REGISTER"
```

**Tarefa Codex:** *"Implemente SipAccount, AppState e EventQueue. Todas as callbacks devem ser
noexcept com try/catch triplo e apenas publicar eventos na fila. Traduza os códigos de resposta de
registro conforme ARCHITECTURE §9.3. onIncomingCall responde 486 se já houver chamada, senão 180."*

---

## Etapa 10 — `SipCall`, `CallRegistry` e chamada de saída

**Objetivo:** requisitos 6, 10 e 11 (sinalização).

**Depende de:** E09.

**Arquivos previstos:**
```
src/sip/SipCall.h / .cpp
src/sip/CallRegistry.h / .cpp
```

**Escopo:**
- `SipCall` com `onCallState` e `onCallTsxState` (`noexcept`), publicando transições.
- `CallRegistry` com `adopt/retire/reap` — **destruição diferida** (ARCHITECTURE §6.5).
- Normalização do destino (ARCHITECTURE §7.2).
- `hangup(CallOpParam(true))` e tratamento de `DISCONNECTED` com código e motivo.

**Fora de escopo:** áudio (E11) e DTMF (E13+).

**Critério de conclusão:**
- Chamada para ramal interno percorre `CALLING → EARLY → CONFIRMED → DISCONNECTED` com todos os
  estados exibidos.
- `hangup` funciona nos dois sentidos (local e remoto).
- 20 ciclos de call/hangup consecutivos sem crash nem vazamento de `SipCall`.

**Comandos de validação:**
```powershell
# no console: call 1002 ; hangup ; status
# repetir 20x e verificar contador de objetos vivos no log de shutdown
```

**Tarefa Codex:** *"Implemente SipCall e CallRegistry. SipCall NUNCA deve fazer delete this: em
DISCONNECTED deve chamar registry.retire(); a destruição ocorre em registry.reap() na thread
principal. Implemente a normalização de destino de ARCHITECTURE §7.2 sem heurísticas adicionais."*

---

## Etapa 11 — Áudio bidirecional

**Objetivo:** requisitos 7 e 8.

**Depende de:** E08, E10.

**Arquivos previstos:** `src/sip/SipCall.cpp` (+`onCallMediaState`), `src/audio/AudioDeviceService.cpp`.

**Escopo:** ARCHITECTURE §8.2 — conectar `callMedia → playback` e `capture → callMedia`;
tratar `ACTIVE`, `LOCAL_HOLD`, `REMOTE_HOLD`, `ERROR`, `NONE`; guardar `AudioMedia`/`confSlot` para
uso do DTMF; desconectar no `DISCONNECTED`; logar codec negociado e IP:porta RTP.

**Critério de conclusão:** áudio bidirecional audível em chamada para o teste de eco do Asterisk
(`*43`) e para outro ramal; codec negociado aparece no log; sem áudio unidirecional.

**Comandos de validação:**
```powershell
# no console: call *43   (echo test do Asterisk) — falar e ouvir o retorno
# no console: call 1002  — conversa bidirecional com outro ramal
Select-String -Path .\logs\*.log -Pattern "codec negociado|media state"
```

**Tarefa Codex:** *"Implemente onCallMediaState conectando o áudio conforme ARCHITECTURE §8.2,
tratando explicitamente os cinco status de mídia. Guarde a AudioMedia e o conf slot da chamada para
uso posterior pelo DTMF. Registre no log o codec negociado e o par IP:porta RTP."*

---

## Etapa 12 — Console: parser, laço de comandos e `status`

**Objetivo:** interface de operação completa (sem DTMF ainda).

**Depende de:** E11.

**Arquivos previstos:**
```
src/app/CommandParser.h / .cpp
src/app/ConsoleUi.h / .cpp
tests/test_command_parser.cpp
```

**Escopo:**
- `CommandParser` **puro** (string → `Command`), com todos os verbos de ARCHITECTURE §3.3
  (os de DTMF já parseados, mesmo que a execução venha depois).
- `ConsoleUi`: prompt com estado (`[reg:OK][call:CONFIRMED][dtmf:rfc4733]`), drenagem da `EventQueue`,
  `reap()` a cada iteração, `help`, `status`, `Ctrl+C` gracioso (ARCHITECTURE §5.3).

**Critério de conclusão:** todos os comandos respondem (mesmo que com "ainda não implementado" para
DTMF); `Ctrl+C` encerra com código 130 e sem crash; parser tem cobertura de teste incluindo entradas
malformadas.

**Comandos de validação:**
```powershell
.\build\Debug\polphone_tests.exe -ts=parser
# no console: help ; status ; devices ; comando inexistente ; Ctrl+C
```

**Tarefa Codex:** *"Implemente CommandParser como função pura com testes unitários abrangentes
(incluindo flags --method/--duration/--gap e entradas inválidas) e ConsoleUi com prompt de estado,
drenagem da EventQueue, reap() por iteração e encerramento gracioso por Ctrl+C conforme
ARCHITECTURE §5.3."*

---

## Etapa 13 — DTMF por RFC 4733 / RFC 2833

**Objetivo:** requisito 12(a) — **primeiro núcleo da prova técnica**.

**Depende de:** E12. **Pré-requisito obrigatório:** conferir os itens C1, C2, C7 de DTMF-DESIGN §10
nos headers reais da tag 2.17.

**Arquivos previstos:**
```
src/dtmf/DtmfMethod.h
src/dtmf/DtmfPlan.h / .cpp
src/dtmf/DtmfSender.h / .cpp
tests/test_dtmf_plan.cpp
```

**Escopo:**
- `DtmfPlan` puro: validação de caracteres, expansão de `,` em pausa de 500 ms, sequência
  `on/off` — totalmente testável sem PJSIP.
- `DtmfSender` com guards (chamada, estado, mídia, `inFlight`), `correlationId`, log antes/depois de
  cada dígito, **sem fallback**.
- Envio via `CallSendDtmfParam{method=PJSUA_DTMF_METHOD_RFC2833, duration, digits}`.
- Tradução de `PJMEDIA_RTP_EREMNORFC2833` (DTMF-DESIGN §3.3).
- Detecção e log de `telephone-event` negociado (via trace SIP e/ou `Call::dump`).

**Critério de conclusão:** dígito enviado a um ramal de teste com `Read()` no dialplan é reconhecido;
Wireshark mostra RTP `PT=101` com `duration` correspondente ao configurado; erro traduzido quando
`telephone-event` não é negociado.

**Comandos de validação:**
```powershell
.\build\Debug\polphone_tests.exe -ts=dtmf-plan
# no console: call <ramal-de-teste> ; dtmf 5 --method rfc4733 ; hangup
# Wireshark: filtro rtpevent  → conferir event, E-bit, duration
# Asterisk:  core set verbose 5 → conferir dígito recebido
```

**Tarefa Codex:** *"Implemente DtmfMethod, DtmfPlan (puro, com testes) e DtmfSender com o método
RFC 4733 via Call::sendDtmf. ANTES de codificar, leia pjsua2/call.hpp e pjsua-lib/pjsua.h da tag 2.17
e confirme os campos de CallSendDtmfParam e os valores de pjsua_dtmf_method. Implemente todos os
guards e a tradução de PJMEDIA_RTP_EREMNORFC2833. Não implemente fallback entre métodos."*

---

## Etapa 14 — DTMF por SIP INFO

**Objetivo:** requisito 12(c).

**Depende de:** E13. **Pré-requisito:** item C1/C2 já confirmado.

**Arquivos previstos:** `src/dtmf/DtmfSender.cpp`, `src/sip/SipCall.cpp`.

**Escopo:**
- Envio via `CallSendDtmfParam{method=PJSUA_DTMF_METHOD_SIP_INFO, ...}`.
- Captura da resposta ao `INFO` em `onCallTsxState`, correlacionada pelo `correlationId`.
- Tradução de `415`, `481`, `501` e timeout (DTMF-DESIGN §5.3).

**Critério de conclusão:** `INFO` com `Content-Type: application/dtmf-relay` e corpo
`Signal=<d>\r\nDuration=<ms>` visível no trace; resposta do PABX exibida no console;
dígito reconhecido pelo dialplan de teste.

**Comandos de validação:**
```powershell
# no console: dtmf 5 --method info
Select-String -Path .\logs\*.log -Pattern "dtmf-relay|INFO"
# Wireshark: filtro sip.Method == "INFO"
# Asterisk:  pjsip set logger on
```

**Tarefa Codex:** *"Adicione ao DtmfSender o método SIP INFO usando PJSUA_DTMF_METHOD_SIP_INFO.
Implemente SipCall::onCallTsxState para capturar e logar a resposta ao INFO correlacionada pelo
correlationId, com tradução de 415/481/501/timeout conforme DTMF-DESIGN §5.3."*

---

## Etapa 15 — DTMF in-band (`ToneGenerator`)

**Objetivo:** requisito 12(b) — a etapa tecnicamente mais delicada.

**Depende de:** E14. **Pré-requisito obrigatório:** confirmar C3, C4, C5 de DTMF-DESIGN §10.

**Arquivos previstos:**
```
src/audio/ToneGenerator.h / .cpp
src/dtmf/DtmfSender.cpp
```

**Escopo:** DTMF-DESIGN §4.2 integralmente.
- Pool próprio + `pjmedia_tonegen_create2` com `samplesPerFrame` derivado do clock rate da bridge.
- Registro na conference bridge (`AudioMedia::registerMediaPort2` **ou** `pjsua_conf_add_port`,
  conforme o header real) e conexão `tonegen → slot da chamada`.
- `playDigits` com `on_msec`/`off_msec`/`volume`; conclusão detectada por `pjmedia_tonegen_is_busy()`
  no laço principal, com timeout de segurança.
- Aviso quando o codec negociado não é G.711/G.722 (não bloqueia).
- Feedback local **desligado** por padrão.
- Destruição correta: desconectar → remover porta → destruir porta → liberar pool.

**Critério de conclusão:**
- Tom audível e correto na chamada (validado por ouvido e por espectro no Wireshark/Audacity);
- Dígito reconhecido em chamada com PCMU;
- 50 envios consecutivos sem vazamento de porta na bridge (verificar contagem de slots no log);
- Sem crash ao encerrar a chamada com tonegen ainda anexado.

**Comandos de validação:**
```powershell
# no console: dtmf 5 --method inband --duration 250
# gravar o RTP no Wireshark → Telephony > RTP > Stream Analysis > salvar áudio → inspecionar em Audacity
# repetir 50x: for ($i=0;$i -lt 50;$i++) { ... }  e conferir slots da bridge no log
```

**Tarefa Codex:** *"Implemente ToneGenerator conforme DTMF-DESIGN §4.2 e integre-o ao DtmfSender como
método inband. ANTES de codificar, leia pjmedia/tonegen.h e pjsua2/media.hpp da tag 2.17 para
confirmar as assinaturas de pjmedia_tonegen_create2, pjmedia_tone_digit e registerMediaPort2. O
tonegen deve ser conectado ao slot da chamada, nunca ao dispositivo de reprodução (feedback local
opcional e desligado por padrão). Garanta liberação correta de porta e pool."*

---

## Etapa 16 — Duração, intervalo e garantias de não-duplicidade

**Objetivo:** requisitos 13 e a invariante de DTMF-DESIGN §6.

**Depende de:** E15.

**Arquivos previstos:** `src/dtmf/DtmfSender.cpp`, `src/app/CommandParser.cpp`, `src/config/*`.

**Escopo:**
- Comandos `dtmfmode <m>` e `dtmfcfg duration|gap|volume <v>` com validação de faixa
  (DTMF-DESIGN §7.1) e efeito imediato, sem reiniciar a chamada.
- Flags `--method`, `--duration`, `--gap` por requisição, sem alterar o padrão da sessão.
- Serialização estrita: uma requisição em voo; segunda requisição concorrente é recusada com o id da
  que está em execução.
- Teste automatizado da invariante: nenhum caminho de código envia por mais de um método.

**Critério de conclusão:**
- Alterar duração para 250 ms muda mensuravelmente o `duration` do RTP `PT=101` e o `on_msec` do tom;
- Duas requisições simultâneas produzem exatamente uma sequência enviada + uma recusa registrada;
- Nenhum dígito duplicado em nenhum dos três métodos (verificado no dialplan de teste).

**Comandos de validação:**
```powershell
# no console: dtmfcfg duration 250 ; dtmf 5 --method rfc4733
# Wireshark: conferir o campo duration do rtpevent
# no console: dtmf 1234567890 --method inband  (e tentar outro dtmf durante o envio)
```

**Tarefa Codex:** *"Implemente os comandos dtmfmode e dtmfcfg com validação de faixa, as flags por
requisição, e a serialização estrita do DtmfSender (uma requisição em voo, segunda recusada com
mensagem citando o id em execução). Escreva teste unitário garantindo a invariante de
DTMF-DESIGN §6: nunca mais de um método por requisição."*

---

## Etapa 17 — Endurecimento de erros e encerramento

**Objetivo:** requisitos 1 e 14 em condições adversas.

**Depende de:** E16.

**Arquivos previstos:** `src/app/Application.cpp`, `src/sip/*`, `src/logging/Logger.cpp`.

**Escopo:**
- Todas as traduções de erro de ARCHITECTURE §9.3 implementadas e testadas manualmente.
- Sequência de encerramento de ARCHITECTURE §5.3 com timeouts (3 s cada), incluindo un-REGISTER.
- Códigos de saída de ARCHITECTURE §3.1.
- Auditoria: `noexcept` + try/catch triplo em **todas** as callbacks.
- Teste de estresse de shutdown: encerrar durante `CALLING`, durante `CONFIRMED`, durante envio de DTMF.

**Critério de conclusão:** nenhum crash em 20 execuções do roteiro de estresse; log sempre termina
com "encerramento concluído"; un-REGISTER visível no trace.

**Comandos de validação:**
```powershell
# Ctrl+C durante: registro / CALLING / CONFIRMED / envio in-band em andamento
# 20 execuções; conferir exit code e última linha do log
Select-String -Path .\logs\*.log -Pattern "encerramento concluído" | Measure-Object
```

**Tarefa Codex:** *"Implemente as traduções de erro de ARCHITECTURE §9.3, a sequência de encerramento
de §5.3 com timeouts de 3 s e os códigos de saída de §3.1. Audite todas as callbacks garantindo
noexcept e try/catch para pj::Error, std::exception e (...). Não deixe nenhuma espera infinita."*

---

## Etapa 18 — Validação de campo e documentação de resultados

**Objetivo:** executar a prova técnica e registrar o resultado.

**Depende de:** E17.

**Arquivos previstos:**
```
docs/TEST-MATRIX.md
docs/FIELD-TEST-GUIDE.md
README.md   (seção de resultados)
```

**Escopo:**
- Executar o roteiro de DTMF-DESIGN §9.1 contra a URA externa (caso GoDaddy), com a instrumentação
  simultânea de §9.2.
- Preencher a matriz de §9.3 e concluir conforme §9.4.
- Registrar as capturas (`.pcap`) e logs **fora do repositório** (contêm números reais).

**Critério de conclusão:** matriz completamente preenchida e conclusão documentada sobre qual método
e qual duração funcionam com a URA de destino.

**Comandos de validação:** roteiro manual de DTMF-DESIGN §9.1, executado no binário **Release**.

**Tarefa Codex:** *"Crie docs/FIELD-TEST-GUIDE.md com o roteiro passo a passo de DTMF-DESIGN §9 e
docs/TEST-MATRIX.md com a tabela vazia pronta para preenchimento. Inclua aviso explícito de que
capturas e logs de campo contêm números reais e não devem ser commitados."*

---

## 2. Estrutura de configuração

`config/polphone.config.json` — **nunca versionado**. `config/polphone.config.example.json` — versionado,
idêntico em estrutura, com placeholders.

```jsonc
{
  "$schema": "./polphone.config.schema.json",

  "sip": {
    "idUri":          "sip:1001@pabx.local",   // AOR do ramal
    "registrarUri":   "sip:pabx.local:5060",
    "realm":          "*",                      // "*" = qualquer realm
    "username":       "1001",
    "password":       "REPLACE_ME",             // nunca versionar
    "domain":         "pabx.local",             // usado na normalização de destino
    "proxyUri":       "",                       // opcional
    "regTimeoutSec":  300,
    "regRetryIntervalSec": 10,
    "registerOnStartup": true
  },

  "network": {
    "localPort":      0,                        // 0 = porta automática
    "boundAddress":   "",                       // "" = qualquer interface
    "transport":      "udp"                     // MVP: apenas "udp"
  },

  "audio": {
    "captureDevice":  "",                       // "" = padrão; ou substring do nome; ou "#3"
    "playbackDevice": "",
    "clockRate":      8000,                     // 8000 recomendado p/ testes in-band
    "channelCount":   1,
    "ptimeMs":        20,
    "ecTailMs":       200,
    "quality":        8,
    "noVad":          true                      // manter true (DTMF in-band)
  },

  "codecs": {
    "priority": {                               // 0 = desabilitado, 255 = máxima
      "PCMU/8000/1":  254,
      "PCMA/8000/1":  253,
      "G722/16000/1": 100,
      "speex/8000/1":   0,
      "iLBC/8000/1":    0,
      "GSM/8000/1":     0
    }
  },

  "dtmf": {
    "defaultMethod":  "rfc4733",                // "rfc4733" | "inband" | "info"
    "durationMs":     160,                      // 40..2000
    "gapMs":          100,                      // 20..2000
    "volumeDbm0":     -10,                      // -30..0, apenas in-band
    "localFeedback":  false,
    "logDigits":      false                     // true grava dígitos em claro no log
  },

  "logging": {
    "consoleLevel":   4,                        // 0..6
    "fileLevel":      5,
    "directory":      "logs",
    "maxFileMB":      50,
    "sipMessageTrace": true
  }
}
```

Regras:
- Todo campo tem default no código; o JSON só precisa conter o que difere.
- Campo desconhecido → **aviso**, não erro (tolerância a evolução).
- Campo com tipo errado ou fora de faixa → **erro** apontando o caminho (`dtmf.durationMs`).
- `password` só existe no arquivo real; o `.example` traz `REPLACE_ME`.
- Nada de variáveis de ambiente ou *keystore* no MVP — simplicidade deliberada, documentada em ADR-008.

---

## 3. Estratégia de testes

O MVP é um app de rede e áudio: a maior parte do valor está em testes manuais instrumentados.
A estratégia é dividir o que é **puro** (automatizável barato) do que é **integrado** (roteiro manual
reprodutível).

### 3.1 Camada 1 — Testes unitários (doctest, no CI)

| Alvo | Casos mínimos |
|---|---|
| `Strings::maskMiddle` | número curto, longo, vazio, com `+`, com caracteres não numéricos |
| `Redactor` | cada regra de ARCHITECTURE §10.3 + casos que **não** devem mascarar |
| `ConfigLoader` | JSON válido, campo ausente (default), tipo errado, arquivo inexistente, JSON malformado |
| `ConfigValidator` | cada faixa de valor no limite inferior, superior e fora |
| `CommandParser` | todos os verbos, flags, ordem trocada, flag sem valor, verbo desconhecido, linha vazia |
| `DtmfPlan` | dígitos válidos/ inválidos, `,` como pausa, sequência longa, duração/intervalo aplicados |
| `Result<T>` | propagação de erro, valor, conversão |

Meta: **100% das funções puras cobertas**. Nenhum teste unitário toca a rede, o áudio ou o PJSUA2.

### 3.2 Camada 2 — Teste de fumaça automatizável

`polphone.exe --selftest` — sobe e derruba o PJSUA2 completo (endpoint, transporte, codecs) sem
registrar. Executado 10× em sequência detecta a maioria dos bugs de ciclo de vida.
Executável em CI (Windows runner) porque não exige PABX nem dispositivo de áudio real
(usar dispositivo nulo se necessário).

### 3.3 Camada 3 — Testes de integração com PABX de laboratório

Ambiente: Issabel/Asterisk de teste, dois ramais (1001 = POLPhone, 1002 = referência).

| Teste | Procedimento | Resultado esperado |
|---|---|---|
| I1 Registro | subir o app | `200 OK` em < 5 s |
| I2 Registro negativo | senha errada | `401`/`403` traduzido, app segue vivo |
| I3 Chamada saída | `call 1002` | todos os estados exibidos, áudio bidirecional |
| I4 Eco | `call *43` | ouvir a própria voz com atraso |
| I5 Encerramento local | `hangup` | `DISCONNECTED`, BYE no trace |
| I6 Encerramento remoto | desligar no 1002 | `DISCONNECTED` com código |
| I7 Ciclo | 20× call/hangup | sem crash, sem vazamento |
| I8 DTMF rfc4733 | dialplan com `Read()` | dígito correto, uma vez |
| I9 DTMF info | idem | dígito correto, `200 OK` ao INFO |
| I10 DTMF inband | idem, com PCMU | dígito correto, uma vez |
| I11 Duração | `dtmfcfg duration 250` | `duration` do RTP muda proporcionalmente |
| I12 Duplicidade | 3 métodos em sequência | exatamente 3 dígitos no dialplan, nunca 6 |
| I13 Shutdown sob carga | `Ctrl+C` durante DTMF | encerramento limpo, exit 130 |
| I14 Dispositivos | trocar entrada/saída | áudio migra para o dispositivo escolhido |

Dialplan sugerido para o ramal de teste (não versionar com dados reais):

```
exten => 9999,1,Answer()
 same => n,Wait(1)
 same => n,Read(DIGIT,beep,1,,1,10)
 same => n,NoOp(DTMF RECEBIDO: ${DIGIT})
 same => n,SayDigits(${DIGIT})
 same => n,Goto(9999,3)
```

### 3.4 Camada 4 — Validação de campo (a prova técnica)

Roteiro de DTMF-DESIGN §9, contra a URA externa real, com captura simultânea nos dois lados do PABX.
É o único teste que responde à pergunta original do projeto.

### 3.5 O que **não** será testado no MVP

Testes de carga, múltiplas chamadas simultâneas, reconexão de rede, NAT/STUN, fuzzing de SIP,
comparação de qualidade de áudio, e portabilidade.

### 3.6 CI (opcional, mas recomendado)

Windows runner: `verify-env` → `setup-pjproject` (com cache do diretório `lib/`) → build Debug+Release
→ `polphone_tests.exe` → `polphone.exe --selftest`. Sem PABX no CI.

---

## 4. Critérios de aceitação do MVP

Mapeamento direto dos 15 requisitos. Cada critério é verificável e vinculado à etapa que o entrega.

| # | Requisito | Critério de aceitação | Etapa | Como verificar |
|---|---|---|---|---|
| 1 | Inicializar/finalizar o PJSUA2 | `--selftest` retorna 0 em 10 execuções consecutivas; log termina em "encerramento concluído"; sem *assert* de heap | E07, E17 | script de repetição + exit code |
| 2 | Transporte SIP UDP | Log informa IP:porta local efetivos; `netstat -ano \| findstr <porta>` mostra o socket UDP | E07 | `netstat` + log |
| 3 | Conta SIP de JSON ignorado pelo Git | App lê `config/polphone.config.json`; `git status` permanece limpo; ausência do arquivo → exit 1 com instrução | E01, E06 | `git status` + execução sem o arquivo |
| 4 | Registrar no servidor SIP | REGISTER com `200 OK` visível no trace e no console em < 5 s | E09 | console + Wireshark |
| 5 | Exibir status do registro | `status` mostra estado, código, motivo e `expires`; falhas 401/403/404/408/503 traduzidas | E09 | testes negativos |
| 6 | Realizar ligação | `call <destino>` estabelece chamada para ramal interno e para número externo | E10 | execução |
| 7 | Reproduzir áudio remoto | Áudio do `*43` audível sem cortes | E11 | teste de eco |
| 8 | Capturar/enviar áudio do microfone | Interlocutor no 1002 ouve o POLPhone; RTP de saída visível no Wireshark | E11 | chamada entre ramais |
| 9 | Selecionar dispositivos de E/S | `devices` lista com acentuação correta; `setdev in/out` altera efetivamente o dispositivo; seleção por nome no JSON funciona | E08 | troca de headset durante teste |
| 10 | Exibir estados da chamada | `CALLING`, `EARLY`, `CONNECTING`, `CONFIRMED`, `DISCONNECTED` exibidos com código e motivo | E10 | console |
| 11 | Encerrar a ligação | `hangup` gera BYE e leva a `DISCONNECTED`; encerramento remoto também é tratado | E10 | trace SIP |
| 12a | DTMF RFC 4733/2833 | Dígito reconhecido pelo dialplan; RTP `PT=101` com E-bit e duração corretos no Wireshark | E13 | Wireshark + Asterisk |
| 12b | DTMF in-band | Tom correto no áudio (verificado espectralmente); dígito reconhecido com PCMU | E15 | Audacity + Asterisk |
| 12c | DTMF SIP INFO | `INFO` com `application/dtmf-relay` e `Signal=`/`Duration=`; resposta do PABX exibida | E14 | Wireshark + console |
| 12d | Seleção explícita | Os três métodos selecionáveis por comando e por config; **nenhum fallback automático** | E13–E16 | inspeção + teste de erro |
| 13 | Duração e intervalo configuráveis | `dtmfcfg duration 250` altera mensuravelmente o RTP/tom; validação de faixa recusa valores inválidos | E16 | Wireshark |
| 14 | Logs técnicos sem expor segredos | Busca por senha, `response=` e número completo nos logs não retorna nada; trace SIP presente e útil | E05, E17 | `Select-String` nos logs |
| 15 | Windows x64 | `dumpbin /headers` confirma x64; build Debug e Release limpos; roda em máquina sem VS (com Redistributable) | E02, E03 | `dumpbin` + máquina limpa |
| 16 | **Prova técnica** | Mesmo dígito enviado pelos 3 métodos na mesma chamada para a URA externa, com resultado documentado na matriz | E18 | roteiro DTMF-DESIGN §9 |

**Critério global:** o MVP é aceito quando os 16 itens acima estão verdes **e** a matriz de
`docs/TEST-MATRIX.md` está preenchida com uma conclusão sobre o caso da URA externa.

---

## 5. Lista objetiva de tarefas para o Codex

Ordem de execução obrigatória. Cada item é uma tarefa fechada, entregue e validada antes da próxima.

| # | Tarefa | Entrega principal | Depende de |
|---|---|---|---|
| **T01** | Esqueleto do repositório, GPL-2.0, README, `.gitignore`, árvore de diretórios, config de exemplo com placeholders | E01 | — |
| **T02** | Submodule `pjproject` fixado na **tag 2.17**, `cmake/config_site.h.in` validado contra os headers reais, `verify-env.ps1`, `setup-pjproject.ps1` (Debug-Dynamic + Release-Dynamic x64, sem alterar o submodule) | E02 | T01 |
| **T03** | `CMakeLists.txt` + `CMakePresets.json` + `cmake/PJSIP.cmake` + scripts `build/run/clean`; `main.cpp` mínimo com `--version` e `--selftest`; vendorização de `nlohmann/json.hpp` e `doctest.h` | E03 | T02 |
| **T04** | `Result<T>`, `Strings`, `Time` e alvo `polphone_tests` com doctest + testes unitários | E04 | T03 |
| **T05** | `Logger`, `Redactor` (todas as regras de ARCHITECTURE §10.3 com testes) e `PjLogWriter` integrado ao `EpConfig` | E05 | T04 |
| **T06** | `AppConfig`, `ConfigLoader`, `ConfigValidator`, `redactedDump()`, config de exemplo final, testes | E06 | T04 |
| **T07** | `SipEndpoint` (RAII, UDP, codecs), `PjErrors`, esqueleto de `Application` com ordem de init/shutdown | E07 | T05, T06 |
| **T08** | `AudioDeviceService` (enumeração, seleção por nome/índice, `--list-devices`) | E08 | T07 |
| **T09** | `SipAccount` + registro + `AppState` + `EventQueue` + tradução de códigos de registro | E09 | T07 |
| **T10** | `SipCall` + `CallRegistry` com **destruição diferida** + chamada de saída + estados + `hangup` | E10 | T09 |
| **T11** | `onCallMediaState` com conexão de áudio bidirecional e tratamento dos 5 status de mídia | E11 | T08, T10 |
| **T12** | `CommandParser` (puro, com testes) + `ConsoleUi` com prompt de estado, drenagem de eventos, `reap()` e `Ctrl+C` gracioso | E12 | T11 |
| **T13** | **DTMF RFC 4733** — `DtmfMethod`, `DtmfPlan` (testado), `DtmfSender` com guards, `correlationId` e tradução de `PJMEDIA_RTP_EREMNORFC2833`. *Confirmar C1/C2/C7 nos headers da tag 2.17 antes de codificar* | E13 | T12 |
| **T14** | **DTMF SIP INFO** + captura da resposta ao `INFO` em `onCallTsxState` + tradução de 415/481/501 | E14 | T13 |
| **T15** | **DTMF in-band** — `ToneGenerator` (tonegen + conference bridge), aviso de codec incompatível, liberação correta de porta e pool. *Confirmar C3/C4/C5 antes de codificar* | E15 | T14 |
| **T16** | `dtmfmode`/`dtmfcfg`, flags por requisição, serialização estrita e teste da invariante de não-duplicidade | E16 | T15 |
| **T17** | Endurecimento: traduções de erro §9.3, encerramento §5.3 com timeouts, códigos de saída, auditoria de `noexcept` em todas as callbacks | E17 | T16 |
| **T18** | `docs/FIELD-TEST-GUIDE.md` e `docs/TEST-MATRIX.md`; execução do roteiro de campo e registro da conclusão | E18 | T17 |

### Regras permanentes para o Codex

1. **Nunca** commitar `config/polphone.config.json`, logs, capturas, credenciais, IPs internos ou
   números de telefone reais.
2. **Nunca** apontar o submodule para `master` nem rodar `git submodule update --remote`.
3. **Nunca** assumir assinatura de API do PJSIP — ler o header da tag 2.17 antes de usar
   (lista em DTMF-DESIGN §10).
4. **Nunca** implementar fallback automático entre métodos DTMF.
5. **Nunca** fazer `delete this` em `SipCall`, nem destruir `Call`/`Account` dentro de callback.
6. Toda callback do PJSUA2 é `noexcept` com `try/catch` para `pj::Error`, `std::exception` e `(...)`.
7. Não adicionar dependências além de `pjproject`, `nlohmann/json` e `doctest`.
8. Não implementar contatos, histórico, gravação, transferência, conferência, vídeo, presença,
   mensagens, GUI, TLS/SRTP, múltiplas contas ou múltiplas chamadas simultâneas.
9. Toda mensagem ao usuário em **português**, com código do erro e ação sugerida.
10. Uma etapa por commit; nenhuma etapa concluída sem o comando de validação executado.
