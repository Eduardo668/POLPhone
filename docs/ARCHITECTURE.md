# POLPhone — Arquitetura (MVP Console)

> Documento de arquitetura da prova técnica do POLPhone.
> Escopo: aplicação **console**, **Windows x64**, **C++17**, **PJSIP/PJSUA2 2.17**, **somente áudio**.
> Este documento **não** descreve código implementado — descreve o desenho a ser implementado.

---

## 1. Visão geral da solução

### 1.1 Problema

Em chamadas externas originadas de ramais Windows (MicroSIP) através de um PABX Issabel/Asterisk,
determinadas URAs de destino (ex.: GoDaddy) **não reconhecem os dígitos DTMF** enviados pelo usuário,
embora a sinalização SIP e o áudio bidirecional funcionem normalmente.

A causa raiz do DTMF não reconhecido quase nunca é única. Ela costuma estar em uma combinação de:

| Camada | Falha típica |
|---|---|
| Softphone → PABX | Método DTMF negociado diferente do esperado (RFC 4733 não negociado, INFO ignorado) |
| SDP | `telephone-event` ausente na oferta/resposta, ou *payload type* divergente (101 vs 96/96+) |
| Duração | Evento RFC 4733 curto demais (< 100 ms) para o detector da URA |
| Transcodificação | In-band destruído por codec compressivo (G.729/GSM/Opus/iLBC) ou por VAD/supressão de silêncio |
| PABX → tronco | Asterisk não regenera o DTMF no método que o tronco/operadora espera (`dtmfmode` do trunk) |
| Gateway remoto | Gateway do destino só aceita um dos métodos |

Como o softphone só controla **o primeiro salto (softphone → PABX)**, a ferramenta necessária é
**diagnóstica**: um softphone que permita **escolher explicitamente** o método DTMF, a duração e o
intervalo entre dígitos, e que registre exatamente o que foi enviado — para que o par
(método no softphone × `dtmfmode` no trunk do Asterisk) possa ser correlacionado empiricamente.

### 1.2 Objetivo do MVP

Um executável de console capaz de:

1. Registrar uma conta SIP lida de um JSON local;
2. Fazer uma chamada de saída com áudio bidirecional;
3. Escolher dispositivos de entrada/saída do Windows;
4. Enviar o **mesmo dígito** por **três métodos distintos e explicitamente selecionáveis**
   (RFC 4733/2833, In-band, SIP INFO), com duração e intervalo configuráveis;
5. Produzir log técnico correlacionável com a captura SIP/RTP, sem vazar segredos.

### 1.3 Princípios de projeto

| Princípio | Consequência prática |
|---|---|
| **Simplicidade acima de abstração** | Sem *frameworks*, sem DI container, sem camadas de plugin. Classes concretas. |
| **Nativo e depurável** | Um único processo, poucas threads, *breakpoints* funcionam em toda a cadeia. |
| **Escopo fechado** | Nada de contatos, histórico, gravação, transferência, conferência, vídeo, presença, mensagens. |
| **Sem fallback silencioso** | Se o método DTMF escolhido não pode ser usado, isso é **erro visível**, não substituição automática. |
| **Configuração fora do código** | Nenhuma credencial, IP interno ou número no repositório. |
| **PJSIP isolado** | Apenas a camada `sip/`, `audio/` e `dtmf/` conhecem PJSUA2. `config/`, `logging/util` e `app/` são testáveis sem PJSIP. |

### 1.4 Fora de escopo (MVP)

Vídeo, TLS/SRTP, TCP, IPv6 como requisito, STUN/TURN/ICE, chamadas entrantes como funcionalidade
(apenas tratamento mínimo educado), múltiplas contas, múltiplas chamadas simultâneas, GUI,
instalador, auto-update, portabilidade para Linux/macOS.

---

## 2. Componentes

### 2.1 Diagrama de camadas

```
┌──────────────────────────────────────────────────────────────────────────┐
│  main.cpp                                                                 │
│  argv → bootstrap → Application::run() → código de saída                  │
└───────────────┬──────────────────────────────────────────────────────────┘
                │
┌───────────────▼──────────────────────────────────────────────────────────┐
│  Camada de Aplicação  (src/app)                     [sem PJSIP]           │
│  Application · ConsoleUi · CommandParser · AppState · EventBus            │
└───────┬──────────────────┬───────────────────┬───────────────────────────┘
        │                  │                   │
┌───────▼────────┐ ┌───────▼────────┐ ┌────────▼──────────────────────────┐
│ Configuração   │ │ Logging        │ │  Camada SIP/Mídia  (src/sip,      │
│ (src/config)   │ │ (src/logging)  │ │  src/audio, src/dtmf)             │
│                │ │                │ │                                   │
│ AppConfig      │ │ Logger         │ │  SipEndpoint                      │
│ ConfigLoader   │ │ PjLogWriter    │ │  SipAccount : pj::Account         │
│ ConfigValidator│ │ Redactor       │ │  SipCall    : pj::Call            │
│                │ │ FileSink       │ │  CallRegistry                     │
│ [sem PJSIP]    │ │ [PjLogWriter   │ │  AudioDeviceService               │
│                │ │  toca PJSIP]   │ │  ToneGenerator                    │
│                │ │                │ │  DtmfSender (+3 estratégias)      │
└────────────────┘ └────────────────┘ └───────────┬───────────────────────┘
                                                   │
                                      ┌────────────▼────────────┐
                                      │  PJSUA2 / PJSIP 2.17    │
                                      │  (libs estáticas)       │
                                      └────────────┬────────────┘
                                                   │
                                      ┌────────────▼────────────┐
                                      │  Windows: WMME/WASAPI,  │
                                      │  Winsock                │
                                      └─────────────────────────┘
```

### 2.2 Inventário de componentes

| # | Componente | Arquivo previsto | Conhece PJSIP? | Testável isoladamente |
|---|---|---|---|---|
| 1 | `Application` | `src/app/Application.{h,cpp}` | indireto | parcial |
| 2 | `ConsoleUi` | `src/app/ConsoleUi.{h,cpp}` | não | sim |
| 3 | `CommandParser` | `src/app/CommandParser.{h,cpp}` | não | **sim** |
| 4 | `AppState` | `src/app/AppState.{h,cpp}` | não | sim |
| 5 | `AppConfig` (POD) | `src/config/AppConfig.h` | não | **sim** |
| 6 | `ConfigLoader` | `src/config/ConfigLoader.{h,cpp}` | não | **sim** |
| 7 | `ConfigValidator` | `src/config/ConfigValidator.{h,cpp}` | não | **sim** |
| 8 | `Logger` | `src/logging/Logger.{h,cpp}` | não | **sim** |
| 9 | `Redactor` | `src/logging/Redactor.{h,cpp}` | não | **sim** |
| 10 | `PjLogWriter` | `src/logging/PjLogWriter.{h,cpp}` | sim | não |
| 11 | `SipEndpoint` | `src/sip/SipEndpoint.{h,cpp}` | sim | não |
| 12 | `SipAccount` | `src/sip/SipAccount.{h,cpp}` | sim | não |
| 13 | `SipCall` | `src/sip/SipCall.{h,cpp}` | sim | não |
| 14 | `CallRegistry` | `src/sip/CallRegistry.{h,cpp}` | sim | parcial |
| 15 | `AudioDeviceService` | `src/audio/AudioDeviceService.{h,cpp}` | sim | não |
| 16 | `ToneGenerator` | `src/audio/ToneGenerator.{h,cpp}` | sim | não |
| 17 | `DtmfSender` | `src/dtmf/DtmfSender.{h,cpp}` | sim | parcial |
| 18 | `DtmfPlan` | `src/dtmf/DtmfPlan.{h,cpp}` | não | **sim** |
| 19 | `PjErrors` | `src/sip/PjErrors.{h,cpp}` | sim | não |

---

## 3. Responsabilidades por classe

### 3.1 `main.cpp`

**Responsabilidade única:** *bootstrap* e definição do código de saída.

- Configura o console: `SetConsoleOutputCP(CP_UTF8)`, `SetConsoleCP(CP_UTF8)`.
- Instala *handler* de `Ctrl+C` (`SetConsoleCtrlHandler`) que sinaliza desligamento gracioso.
- Interpreta argumentos mínimos: `--config <path>` (default `config/polphone.config.json`),
  `--log-level <0..6>`, `--selftest` (inicia e finaliza a biblioteca sem registrar), `--version`.
- Cria `Application`, chama `run()`, converte o resultado em `int`.
- **Não** contém lógica de negócio. **Não** chama PJSUA2 diretamente.

**Códigos de saída:**

| Código | Significado |
|---|---|
| 0 | Encerramento normal |
| 1 | Erro de configuração (arquivo ausente/inválido) |
| 2 | Falha de inicialização do PJSUA2 / transporte |
| 3 | Falha fatal em tempo de execução |
| 130 | Interrompido por `Ctrl+C` |

---

### 3.2 `Application`

**Responsabilidade:** orquestrar o ciclo de vida completo e ser o **dono** de todos os objetos de longa duração.

```
Application
 ├─ owns  Logger
 ├─ owns  AppConfig
 ├─ owns  SipEndpoint            (que é dono do pj::Endpoint)
 ├─ owns  SipAccount             (unique_ptr, destruído ANTES do endpoint)
 ├─ owns  CallRegistry
 ├─ owns  AudioDeviceService
 ├─ owns  DtmfSender
 └─ owns  ConsoleUi
```

- `Result<void> initialize()` — carrega config, sobe logging, sobe endpoint, seleciona dispositivos,
  cria conta, dispara registro.
- `int run()` — laço do console até `quit`/`Ctrl+C`.
- `void shutdown()` — sequência de encerramento (seção 5.3). **Idempotente** e chamada por RAII.
- É o único ponto que traduz comandos do usuário em ações sobre SIP/áudio/DTMF.
- Mantém a *ordem de destruição* correta — este é o ponto mais crítico de estabilidade do MVP.

---

### 3.3 `ConsoleUi` e `CommandParser`

`CommandParser` — **função pura**: `string → Command` (struct com `verb` + argumentos tipados).
Sem I/O, sem estado. É a peça mais facilmente coberta por testes unitários.

`ConsoleUi` — leitura de `stdin` (bloqueante, na thread principal), impressão formatada, e um
*log de eventos* que recebe notificações vindas das *callbacks* PJSIP através de fila thread-safe
(seção 6.4). Nunca formata mensagem enquanto segura o *lock* do estado.

**Conjunto de comandos do MVP (fechado):**

```
help                                  lista comandos
status                                estado do registro + chamada atual + dispositivos + config DTMF
devices                               lista dispositivos de captura e reprodução
setdev in <id> | out <id>             seleciona dispositivo (bloqueado durante chamada ativa)
reg on | off                          força REGISTER / un-REGISTER
call <destino>                        disca (número ou URI SIP completa)
answer                                atende chamada entrante (uso interno de teste)
hangup                                encerra a chamada corrente
dtmf <dígitos> [--method rfc4733|inband|info] [--duration ms] [--gap ms]
dtmfmode rfc4733 | inband | info      define o método padrão da sessão
dtmfcfg duration <ms> | gap <ms> | volume <dBm0>
codecs                                lista codecs e prioridades efetivas
loglevel <0..6>                       ajusta verbosidade em tempo de execução
quit                                  encerramento gracioso
```

> Regra de projeto: `dtmf` **nunca** escolhe o método sozinho. Se `--method` não for informado,
> usa o modo corrente da sessão, e o modo corrente é sempre exibido no *prompt*:
> `POLPhone [reg:OK][call:CONFIRMED][dtmf:rfc4733]>`

---

### 3.4 `AppConfig`, `ConfigLoader`, `ConfigValidator`

`AppConfig` — struct agregada, **POD-like**, sem métodos além de `redactedDump()`.
`ConfigLoader` — lê arquivo → `nlohmann::json` → `AppConfig`; erros de parse viram `Result` com
mensagem e caminho JSON do campo problemático. Aplica *defaults*.
`ConfigValidator` — regras semânticas separadas do parsing (URI válida, portas 0–65535,
`durationMs` ∈ [40, 2000], `gapMs` ∈ [20, 2000], método DTMF conhecido, prioridade de codec 0–255).

Separar *parse* de *validação* permite testar as duas coisas sem arquivo em disco.

---

### 3.5 `Logger`, `Redactor`, `PjLogWriter`

`Logger` — fachada thread-safe com dois destinos: console (verbosidade baixa, legível) e arquivo
(verbosidade alta, técnico). Formata: `2026-07-31T09:14:22.318-03:00 [INFO ] [sip] mensagem`.

`Redactor` — funções puras de mascaramento. Detalhes na seção 10.

`PjLogWriter : public pj::LogWriter` — implementa `void write(const pj::LogEntry &entry)`,
aplica `Redactor` e delega ao `Logger`. Instalado em `EpConfig.logConfig.writer`.

> **Regra de tempo de vida (tag 2.17):** o `Logger` é criado antes do endpoint e destruído depois
> dele. O `PjLogWriter` é alocado dinamicamente e sua propriedade é transferida ao PJSUA2 em
> `libInit()` por `EpConfig.logConfig.writer`; `Endpoint::libDestroy()` emite os últimos logs e então
> executa `delete` no writer. A aplicação não pode usar um writer de pilha nem destruí-lo novamente.
> Ver ADR-021.

---

### 3.6 `SipEndpoint`

Wrapper RAII de `pj::Endpoint` — o *singleton* de fato do PJSUA2.

| Método | Chama |
|---|---|
| `create()` | `Endpoint::libCreate()` |
| `init(cfg)` | monta `EpConfig` e chama `libInit()` |
| `createUdpTransport(port)` | `transportCreate(PJSIP_TRANSPORT_UDP, TransportConfig)` |
| `start()` | `libStart()` |
| `applyCodecPriorities(cfg)` | `codecEnum2()` + `codecSetPriority()` |
| `destroy()` | `libDestroy()` — idempotente |
| `registerThisThread(name)` | `libRegisterThread()` se `libIsThreadRegistered()` for falso |

Responsável também por **imprimir o mapa de codecs efetivo** após `libStart()` — informação
essencial para o diagnóstico de DTMF in-band.

---

### 3.7 `SipAccount : public pj::Account`

Uma única instância no MVP.

- `create(AccountConfig)` a partir de `AppConfig::sip`.
- `onRegState(OnRegStateParam &prm)` — publica evento com `prm.code`, `prm.reason`,
  `getInfo().regIsActive`, `regExpiresSec`. **Não** imprime diretamente no console.
- `onIncomingCall(OnIncomingCallParam &prm)` — MVP: se já existe chamada ativa, responde `486 Busy Here`;
  caso contrário cria um `SipCall`, responde `180 Ringing` e aguarda comando `answer`.
  Existe apenas para não deixar INVITE sem resposta — **não é funcionalidade**.
- Nunca é destruída dentro de uma *callback*.

---

### 3.8 `SipCall : public pj::Call`

Uma instância por chamada. Sobrescreve:

| Callback | Uso no MVP |
|---|---|
| `onCallState` | publica transição de estado; ao chegar em `PJSIP_INV_STATE_DISCONNECTED` marca a chamada para **remoção diferida** |
| `onCallMediaState` | conecta/desconecta o áudio (seção 8) e informa o `DtmfSender` que a mídia está ativa |
| `onDtmfEvent` (2.12+) / `onDtmfDigit` | apenas registra dígitos **recebidos**, com máscara |
| `onCallTsxState` | opcional: registra a resposta a `INFO` (200 OK × 415 × 501) — importante para o diagnóstico |

Expõe, para uso do `DtmfSender`:
- `bool hasActiveAudio() const`
- `pj::AudioMedia* audioMedia()` — obtido via `getAudioMedia(idx)`
- `int confSlot() const` — *slot* na *conference bridge*
- `bool telephoneEventNegotiated() const` — derivado da inspeção do stream/SDP

> **Regra de projeto crítica:** `SipCall` **nunca** faz `delete this`. Ver seção 6.5.

---

### 3.9 `CallRegistry`

Guarda a chamada corrente e a fila de chamadas a destruir.

```
class CallRegistry {
  std::mutex m;
  std::unique_ptr<SipCall> current;              // no máximo 1 no MVP
  std::vector<std::unique_ptr<SipCall>> graveyard; // aguardando reaping
public:
  SipCall* current();                 // ponteiro observador, sob lock
  void adopt(std::unique_ptr<SipCall>);
  void retire();                      // move current -> graveyard  (chamado da callback)
  void reap();                        // destrói graveyard          (chamado da thread principal)
  bool hasActiveCall() const;
};
```

---

### 3.10 `AudioDeviceService`

Encapsula `pj::AudDevManager`.

- `list()` → vetor de `{id, name, inputCount, outputCount, driver}` obtido de `enumDev2()`.
- `selectCapture(id)` / `selectPlayback(id)` → `setCaptureDev` / `setPlaybackDev`.
- `resolveByName(substring)` — permite fixar o dispositivo por nome no JSON, robusto a mudança de índice
  (índices do WMME mudam quando um USB é conectado; **nome parcial é mais estável que índice**).
- `connectCallAudio(SipCall&)` / `disconnectCallAudio(SipCall&)` — seção 8.
- Regra: troca de dispositivo é **recusada** durante chamada ativa no MVP (evita a classe inteira de
  bugs de reabertura de *sound device* durante mídia ativa).

---

### 3.11 `ToneGenerator`

Encapsula o gerador de tons do PJMEDIA usado exclusivamente pelo DTMF **in-band**.
Detalhamento completo em [DTMF-DESIGN.md](DTMF-DESIGN.md).

- Cria um `pj_pool_t` próprio e um `pjmedia_tonegen`, registra na *conference bridge*.
- `playDigits(digits, onMs, offMs, volume)`, `stop()`, `isBusy()`.
- `attachTo(SipCall&)` / `detach()` — conecta o *slot* do tonegen ao *slot* da chamada.
- É criado sob demanda (primeiro uso in-band) e destruído no `shutdown()` ou ao final da chamada.

---

### 3.12 `DtmfSender`

Ponto único de envio de DTMF. Recebe um `DtmfPlan` e executa com o método pedido.

```
enum class DtmfMethod { Rfc4733, Inband, SipInfo };

struct DtmfRequest {
  std::string digits;
  DtmfMethod  method;
  unsigned    durationMs;   // "on"
  unsigned    gapMs;        // "off"
  int         volumeDbm0;   // apenas in-band
};
```

- Garante **serialização**: apenas uma requisição em execução por vez (`inFlight` + fila curta).
- Aplica os *guards* de pré-condição (mídia ativa, método viável, codec compatível).
- Emite log estruturado antes e depois de cada dígito.
- **Não** faz fallback entre métodos.

---

### 3.13 `PjErrors`

- `std::string describe(const pj::Error&)` — `status`, `title`, `reason`, arquivo/linha.
- `std::string statusToString(pj_status_t)` via `pj_strerror`.
- Macro/utilitário `POLPHONE_PJ_TRY(expr)` que captura `pj::Error` e converte para `Result<>`.
  O prefixo evita colisão com a macro `PJ_TRY` do próprio PJLIB (ADR-024).

---

## 4. Ciclo de vida do PJSUA2

### 4.1 Regras invioláveis

1. `Endpoint` é **um por processo**. Criar → inicializar → transportar → iniciar → destruir, nessa ordem.
2. `libDestroy()` **deve** ser chamado, mesmo em caminho de erro. Sem ele: *threads* penduradas,
   *sound device* preso, e *assert* do heap de depuração ao sair.
3. Objetos `Account` e `Call` são de propriedade da **aplicação**, não da biblioteca — mas devem ser
   destruídos **antes** de `libDestroy()`.
4. Todo objeto `Call` deve ter sido desconectado (ou `hangupAllCalls()` chamado) antes da destruição.
5. Nenhuma exceção C++ pode atravessar a fronteira de uma *callback* para dentro do código C do PJSIP.

### 4.2 Máquina de estados do `Endpoint`

```
      ┌────────┐  libCreate()  ┌─────────┐  libInit(EpConfig)  ┌──────┐
      │  NULO  │──────────────►│ CREATED │────────────────────►│ INIT │
      └────────┘               └─────────┘                     └──┬───┘
           ▲                                                      │ transportCreate(UDP)
           │                                                      ▼
           │                                                 ┌────────────┐
           │                                                 │ TRANSPORTED│
           │                                                 └──────┬─────┘
           │                                                        │ libStart()
           │                    libDestroy()                        ▼
           └────────────────────────────────────────────────  ┌─────────┐
                                                              │ RUNNING │
                                                              └─────────┘
```

`libCreate()` também **registra a thread chamadora** na biblioteca. Por isso a thread principal
(que fará todas as chamadas de API a partir do console) deve ser a que chama `libCreate()`.

### 4.3 Máquina de estados do `Account`

```
create()  ──► (não registrada)
    │ setRegistration(true) | regConfig.registerOnAdd
    ▼
REGISTERING ──► onRegState(code=200, regIsActive=true)  ──► REGISTERED
    │                                                          │ expira / re-REGISTER automático
    │ onRegState(code 401/403/404/408/503, regIsActive=false)   │
    ▼                                                          ▼
  FALHA (retry automático a cada regConfig.retryIntervalSec)  REGISTERED
    │ setRegistration(false)
    ▼
UNREGISTERING ──► onRegState(code=200, regIsActive=false) ──► (não registrada)
```

Códigos que a UI deve traduzir explicitamente: `401/407` (credenciais/realm), `403` (conta bloqueada
ou IP não permitido), `404` (ramal inexistente), `408`/timeout (rede/firewall UDP 5060),
`503` (PABX indisponível), `PJSIP_SC_*` genéricos.

### 4.4 Máquina de estados do `Call` (saída)

```
makeCall()
   │
   ▼
CALLING ──(100 Trying)──► CALLING ──(180/183)──► EARLY ──(200 OK)──► CONNECTING ──(ACK)──► CONFIRMED
   │                                              │                                          │
   │                                              │ (486/603/404/408/503)                    │ hangup()/BYE
   ▼                                              ▼                                          ▼
DISCONNECTED ◄────────────────────────────────────┴──────────────────────────────────────────┘
```

Eventos de mídia são **ortogonais** ao estado da chamada e chegam por `onCallMediaState`.
Em `EARLY` já pode existir mídia (*early media* / ringback da operadora) — nesse caso é **legítimo**
ouvir áudio antes do `CONFIRMED`, mas o MVP **só habilita envio de DTMF em `CONFIRMED` com mídia ativa**.

### 4.5 Tempo de vida cruzado

```
Logger             ├────────────────────────────────────────────────────────────┤
Endpoint                ├────────────────────────────────────────────────────┤
PjLogWriter                       ├───────────────────────────────────────────┤
Transport UDP               ├────────────────────────────────────────────┤
Account                         ├────────────────────────────────────┤
Call                                  ├──────────────┤   ├────────┤
ToneGenerator                              ├──────┤
                        libCreate                              libDestroy
```

Invariante: nenhuma barra interna pode ultrapassar as bordas da barra que a contém.

---

## 5. Fluxo de inicialização e encerramento

### 5.1 Inicialização (caminho feliz)

```
 1. main: console UTF-8, handler de Ctrl+C
 2. Logger::init(nível console, arquivo de log)          ← já grava a partir daqui
 3. ConfigLoader::load(path)        → AppConfig
 4. ConfigValidator::validate(cfg)  → falha ⇒ exit(1)
 5. Logger::info(cfg.redactedDump())                     ← config com senha mascarada
 6. SipEndpoint::create()                                 [libCreate]  ← registra a thread principal
 7. monta EpConfig:
       uaConfig.threadCnt   = 1
       uaConfig.maxCalls    = 4
       uaConfig.userAgent   = "POLPhone/0.1 (PJSIP 2.17)"
       logConfig.level      = cfg.logging.fileLevel
       logConfig.consoleLevel = cfg.logging.fileLevel
                                      (na tag 2.17 este campo limita também o callback; ADR-022)
       logConfig.writer     = new PjLogWriter(logger)  (posse transferida ao Endpoint; ADR-021)
       logConfig.msgLogging = true       (SIP trace no arquivo)
       medConfig.clockRate  = cfg.audio.clockRate
       medConfig.noVad      = true       (crítico p/ in-band)
       medConfig.ecTailLen  = cfg.audio.ecTailMs
 8. SipEndpoint::init(epCfg)                              [libInit]
 9. SipEndpoint::createUdpTransport(cfg.network.localPort) [transportCreate]
10. SipEndpoint::start()                                   [libStart]  ← worker thread nasce aqui
11. SipEndpoint::applyCodecPriorities(cfg)  + log do mapa de codecs
12. AudioDeviceService::apply(cfg.audio)   (resolve por nome/índice, setCaptureDev/setPlaybackDev)
13. SipAccount::create(accCfg)                             ← dispara REGISTER
14. DtmfSender::configure(cfg.dtmf)
15. ConsoleUi::run()  → laço de comandos
```

Qualquer falha entre 6 e 14 executa `shutdown()` parcial (RAII, na ordem inversa) e retorna 2.

### 5.2 Laço principal

```
enquanto (não encerrando):
    callRegistry.reap()                 // destrói chamadas aposentadas — fora de callback
    drenar fila de eventos → imprimir   // eventos vindos das threads PJSIP
    linha = readLine()                  // bloqueante
    cmd = CommandParser::parse(linha)
    despachar(cmd)                      // try/catch pj::Error
```

> Alternativa considerada e rejeitada para o MVP: `threadCnt = 0` com `libHandleEvents()` no laço
> principal. Ver ADR-010 em [DECISIONS.md](DECISIONS.md).

### 5.3 Encerramento (ordem obrigatória)

```
 1. sinaliza encerramento (flag atômica); ConsoleUi para de aceitar comandos
 2. ToneGenerator::stop() + detach()            se existir
 3. se há chamada ativa: call->hangup(CallOpParam(true))    → aguarda DISCONNECTED até 3 s
 4. Endpoint::hangupAllCalls()                   (rede de segurança)
 5. account->setRegistration(false)              → aguarda onRegState(regIsActive=false) até 3 s
 6. callRegistry.reap()                          (esvazia graveyard)
 7. account.reset()                              destrói SipAccount
 8. ToneGenerator.reset()                        libera pool/porta da bridge
 9. SipEndpoint::destroy()                       [libDestroy; destrói também PjLogWriter]
10. Logger::flush() e fecha arquivo
11. return código de saída
```

**Espera com timeout, nunca espera infinita.** Cada espera usa `condition_variable::wait_for`.
Se o timeout estourar, registra `WARN` e prossegue — a etapa 4 e o `libDestroy()` limpam o resto.

**`Ctrl+C`:** o *handler* apenas seta a flag e sinaliza a *condition variable*; **não** chama
PJSUA2 (a thread do handler não está registrada e o handler roda em contexto restrito).
Como `readLine()` é bloqueante, o handler também fecha/desbloqueia `stdin`
(`CancelIoEx`/`SetEvent` no *handle* do console) para que o laço principal acorde.

---

## 6. Gerenciamento de threads e callbacks

### 6.1 Inventário de threads

| # | Thread | Criada por | Executa | Pode chamar API PJSUA2? |
|---|---|---|---|---|
| T0 | Principal / console | CRT | `libCreate`, config, comandos, `reap()` | **Sim** (registrada por `libCreate`) |
| T1 | Worker PJSUA2 | `libStart()` (`uaConfig.threadCnt = 1`) | ioqueue + timer heap; dispara **todas** as callbacks SIP | Sim |
| T2 | Sound device (captura) | driver WMME/WASAPI via pjmedia-audiodev | callback de áudio em tempo real | **Não** |
| T3 | Sound device (reprodução) | idem | idem | **Não** |
| Tn | Threads internas do transporte/DNS | PJLIB | — | n/a |

### 6.2 Regras de thread

1. **Toda thread criada por nós** que precise tocar PJSUA2 deve chamar
   `Endpoint::libRegisterThread("nome")` uma vez, após verificar `libIsThreadRegistered()`.
   No MVP não criamos nenhuma — é uma regra defensiva para evoluções.
2. **Callbacks devem ser curtas.** Nada de I/O de console, nada de espera em *lock* contestado,
   nada de `sleep`, nada de operação de rede síncrona. Elas rodam em T1 e bloqueiam o processamento
   de SIP inteiro (retransmissões, timers, ACK).
3. **Nada de exceções escapando de callback.** Toda callback tem a forma:

   ```
   void SipCall::onCallState(OnCallStateParam &prm) noexcept {
       try   { ... }
       catch (const pj::Error &e) { logger.error(...); }
       catch (const std::exception &e) { logger.error(...); }
       catch (...) { logger.error("callback: exceção desconhecida"); }
   }
   ```

4. **Não destruir `Call`/`Account` dentro de callback.** Ver 6.5.
5. **Callback do `LogWriter` é reentrante e vem de qualquer thread** (inclusive T2/T3 e de dentro
   do `libDestroy`). O `Logger` precisa de mutex próprio e não pode chamar de volta nenhuma API PJSIP.
6. **Ordem de locks fixa:** `AppState` → `CallRegistry`. Nunca o inverso. Nenhum lock é mantido
   durante uma chamada à API do PJSUA2 que possa bloquear.

### 6.3 Mapa de callbacks

| Callback | Thread | Ação permitida | Ação proibida |
|---|---|---|---|
| `Account::onRegState` | T1 | publicar evento, atualizar `AppState` | imprimir com lock, destruir conta |
| `Account::onIncomingCall` | T1 | criar `SipCall`, `answer(180)` | trabalho longo |
| `Call::onCallState` | T1 | atualizar estado, `retire()` a chamada | `delete`, `hangup` reentrante |
| `Call::onCallMediaState` | T1 | `startTransmit`/`stopTransmit` | reabrir *sound device* |
| `Call::onDtmfEvent` | T1 | log mascarado | responder DTMF |
| `Call::onCallTsxState` | T1 | log de resposta a INFO | modificar diálogo |
| `LogWriter::write` | qualquer | escrever em buffer/arquivo | chamar PJSUA2 |

### 6.4 Comunicação callback → console

Fila SPSC/MPSC simples protegida por mutex + `condition_variable`:

```
struct UiEvent { Severity sev; std::string category; std::string text; };
EventQueue: push() nas callbacks (rápido, sem formatação pesada) ;
            drain() no laço principal (formata e imprime)
```

Benefício: a saída do console nunca se intercala no meio de uma linha, e a T1 nunca bloqueia
esperando o terminal.

### 6.5 Padrão de destruição diferida de `Call`

O problema clássico: em `onCallState(DISCONNECTED)` é tentador fazer `delete this`. Isso é frágil —
a `callback` ainda está executando dentro do objeto, e podem existir callbacks de mídia/transação
em voo para a mesma chamada.

Padrão adotado:

```
onCallState:
    se prm-> estado == DISCONNECTED:
        registry.retire();              // move unique_ptr para graveyard, sob lock
        eventQueue.push("call disconnected: <code> <reason>");
        // NENHUM delete aqui

laço principal (T0):
    registry.reap();                    // destrói tudo do graveyard
```

Como o `reap()` roda em T0 e a chamada já está `DISCONNECTED`, não há callback pendente para o objeto.

---

## 7. Fluxo das chamadas

### 7.1 Chamada de saída

```
 usuário            Application        SipCall            PJSIP           PABX
   │  call 9911...      │                 │                 │               │
   ├───────────────────►│                 │                 │               │
   │                    │ normalizeDest() │                 │               │
   │                    │ (número → sip:num@dominio)         │               │
   │                    ├─ new SipCall ──►│                 │               │
   │                    │ registry.adopt()│                 │               │
   │                    ├─ makeCall(uri) ─┼────────────────►│── INVITE ────►│
   │                    │                 │                 │◄── 100 ───────┤
   │◄─ "CALLING" ───────┼─ onCallState ◄──┤◄────────────────┤◄── 180 ───────┤
   │◄─ "EARLY"    ──────┼─ onCallState ◄──┤                 │◄── 183+SDP ───┤
   │                    │◄ onCallMediaState (early media)    │               │
   │◄─ "CONFIRMED" ─────┼─ onCallState ◄──┤◄────────────────┤◄── 200 OK ────┤
   │                    │                 │                 │─── ACK ──────►│
   │                    │◄ onCallMediaState (ACTIVE)         │               │
   │                    ├─ connectCallAudio()                │◄══ RTP ══════►│
   │  dtmf 5            │                 │                 │               │
   ├───────────────────►│ DtmfSender::send(...)              │               │
   │                    │                 │                 │               │
   │  hangup            │                 │                 │               │
   ├───────────────────►├─ hangup() ──────┼────────────────►│─── BYE ──────►│
   │◄─ "DISCONNECTED" ──┼─ onCallState ◄──┤◄────────────────┤◄── 200 OK ────┤
   │                    ├─ registry.retire()                 │               │
   │                    ├─ (T0) registry.reap()              │               │
```

### 7.2 Normalização do destino

Regra simples e previsível (sem "inteligência"):

| Entrada do usuário | URI gerada |
|---|---|
| `sip:algo@host` ou `sips:...` | usada literalmente |
| `1001` (só dígitos, `*`, `#`, `+`) | `sip:1001@<domínio da conta>` |
| qualquer outra coisa | erro explícito, sem tentativa de adivinhação |

O domínio vem de `sip.domain` no JSON (ou é derivado do `idUri`).

### 7.3 Chamada entrante (mínimo)

Existe apenas para não violar o protocolo. Se `hasActiveCall()` → `486 Busy Here`.
Caso contrário → `180 Ringing` + evento no console; `answer` atende com `200 OK`;
`hangup` responde `603 Decline`. Sem toque, sem notificação, sem histórico.

---

## 8. Fluxo de áudio

### 8.1 A *conference bridge* do PJSUA

O PJSUA mantém uma matriz de portas ("conference bridge"). O **slot 0** é sempre o dispositivo
de som do sistema. Cada chamada com mídia ativa ganha um slot; o `ToneGenerator` ganha outro.
Conectar áudio = declarar arestas `origem → destino` nessa matriz.

```
                     ┌──────────────────────── conference bridge ────────────────────────┐
                     │                                                                    │
 Microfone ──► [slot 0 : sound device] ──startTransmit──► [slot N : call media] ──► RTP tx│
 Alto-falante ◄──────────┘                    ◄──startTransmit── [slot N]        ◄── RTP rx│
                     │                                                                    │
 ToneGenerator ──► [slot T : tonegen] ──startTransmit──► [slot N : call media]            │
                     │              (opcional, feedback local) ──► [slot 0]                │
                     └────────────────────────────────────────────────────────────────────┘
```

### 8.2 Conexão na `onCallMediaState`

```
CallInfo ci = getInfo();
para cada i em ci.media:
    se ci.media[i].type == PJMEDIA_TYPE_AUDIO
       e ci.media[i].status == PJSUA_CALL_MEDIA_ACTIVE   (ou REMOTE_HOLD → tratar)
         AudioMedia &am = getAudioMedia(i);
         AudDevManager &mgr = Endpoint::instance().audDevManager();
         am.startTransmit(mgr.getPlaybackDevMedia());     // remoto → alto-falante
         mgr.getCaptureDevMedia().startTransmit(am);      // microfone → remoto
         guardar am / confSlot para o DtmfSender e o ToneGenerator
```

Estados de mídia a tratar explicitamente (todos devem gerar log):

| `pjsua_call_media_status` | Ação |
|---|---|
| `PJSUA_CALL_MEDIA_ACTIVE` | conectar |
| `PJSUA_CALL_MEDIA_LOCAL_HOLD` / `REMOTE_HOLD` | desconectar transmissão, manter slot |
| `PJSUA_CALL_MEDIA_ERROR` | log `ERROR` + evento; chamada continua sinalizada |
| `PJSUA_CALL_MEDIA_NONE` | nada a fazer |

> `getAudioMedia(idx)` é a API preferida em 2.17. Caso a assinatura difira na tag exata,
> o fallback é `AudioMedia::typecastFromMedia(getMedia(idx))`. **Confirmar no header de 2.17.**

### 8.3 Dispositivos do Windows

- Backend do MVP no Windows: **WMME**. A tag 2.17 contém uma implementação WASAPI, mas o projeto
  oficial WinDesktop não compila `wasapi_dev.cpp`; por isso o MVP define
  `PJMEDIA_AUDIO_DEV_HAS_WASAPI=0` (ADR-020). WMME permanece habilitado e é usado por previsibilidade.
- `enumDev2()` devolve nome + contagem de canais de entrada/saída. Um dispositivo é de captura se
  `inputCount > 0`, de reprodução se `outputCount > 0`.
- A maior parte dos textos do PJSIP vem em UTF-8, mas o backend WMME pode inserir nomes na code page
  ANSI do Windows. O `PjLogWriter` preserva UTF-8 válido e converte essas entradas nativas para UTF-8
  antes de redaction e escrita (ADR-023). O console usa `CP_UTF8` (feito no `main`).
- Nomes do WMME são **truncados em 31 caracteres** — a busca por nome deve ser por *substring*
  e tolerante a truncamento.
- `setCaptureDev/setPlaybackDev` são aplicados no MVP **somente fora de chamada**.
- Relação de *clock rates*:
  - `medConfig.clockRate` — taxa da bridge (padrão 16000);
  - `medConfig.sndClockRate` — taxa do dispositivo (0 = igual à bridge);
  - Se o codec negociado for G.711 (8 kHz), haverá reamostragem 16k↔8k.
  - **Para testes de DTMF in-band recomenda-se `clockRate = 8000`** (config `audio.clockRate`),
    eliminando a reamostragem do tom gerado e reduzindo distorção harmônica.

### 8.4 Ajustes de mídia relevantes para DTMF

| Parâmetro | Valor MVP | Motivo |
|---|---|---|
| `medConfig.noVad` | `true` | VAD/supressão de silêncio pode cortar o início do tom in-band |
| `medConfig.ecTailLen` | 200 ms (config) | EC atua no caminho do microfone; o tonegen entra depois, mas EC agressivo + `0` é pior |
| `medConfig.audioFramePtime` | 20 ms | alinha com o *ptime* típico do RTP |
| `medConfig.quality` | 8..10 | qualidade de reamostragem — relevante quando há 16k↔8k |
| Prioridade de codecs | PCMU/PCMA no topo | in-band só sobrevive em G.711/G.722 |

---

## 9. Tratamento de erros

### 9.1 Taxonomia

| Classe | Exemplos | Reação |
|---|---|---|
| **Configuração** | JSON ausente/ inválido, URI malformada, `durationMs` fora da faixa | mensagem apontando campo + `exit(1)`; nunca subir a lib |
| **Inicialização fatal** | `libCreate/libInit` falha, porta UDP 5060 ocupada, nenhum dispositivo de áudio | log `FATAL`, `shutdown()` parcial, `exit(2)` |
| **Registro** | 401/403/404/408/503 | **não fatal**: exibe motivo traduzido, mantém retry automático do PJSIP |
| **Chamada** | 404/486/603/408/503, `makeCall` lança | não fatal: chamada vai a `DISCONNECTED`, app continua |
| **Mídia** | `PJSUA_CALL_MEDIA_ERROR`, falha ao abrir dispositivo | log `ERROR`, chamada mantida, aviso ao usuário |
| **DTMF** | método indisponível, mídia inativa, tonegen ocupado | **erro explícito ao usuário**, sem fallback |
| **Comando inválido** | verbo desconhecido, argumento fora da faixa | mensagem + `help`, sem efeito colateral |
| **Programação** | violação de invariante | `assert` em Debug; log `FATAL` + encerramento controlado em Release |

### 9.2 Mecanismo

- PJSUA2 sinaliza erros por **exceção `pj::Error`**. Toda chamada à API é envolvida.
- Internamente, o código de aplicação usa `Result<T>` (`expected`-like: valor ou
  `{código, mensagem, detalhe}`), **não** exceções — exceções ficam confinadas à borda do PJSUA2.
- Conversão: `POLPHONE_PJ_TRY(expr)` captura `pj::Error` → `Result` com `e.status`, `e.reason`, `e.title`,
  `e.srcFile:e.srcLine`.
- Toda mensagem de erro exibida ao usuário inclui: **o que falhou**, **o código**, **o que fazer**.
  Exemplo: `REGISTER falhou: 408 Request Timeout — verifique conectividade UDP até <host>:<porta> e regras de firewall.`

### 9.3 Erros que exigem tradução amigável (obrigatório no MVP)

| Sintoma técnico | Mensagem esperada |
|---|---|
| `PJSIP_EBUSY` / porta ocupada | "Porta UDP local já em uso. Use `network.localPort: 0` para porta automática." |
| `PJMEDIA_EAUD_NODEFDEV` | "Nenhum dispositivo de áudio padrão. Verifique `devices` e `audio.captureDevice`." |
| `PJMEDIA_RTP_EREMNORFC2833` | "O outro lado não negociou `telephone-event`; RFC 4733 indisponível nesta chamada." |
| `415 Unsupported Media Type` em INFO | "O PABX rejeitou `application/dtmf-relay`. Teste `--method rfc4733` ou ajuste `dtmfmode` no trunk." |
| `501 Not Implemented` em INFO | "O PABX não implementa SIP INFO para DTMF." |
| `PJ_ETIMEDOUT` no REGISTER | "Sem resposta do registrar — firewall/NAT bloqueando UDP." |

---

## 10. Estratégia de logs e redaction

### 10.1 Dois destinos, duas verbosidades

| Destino | Nível padrão | Público | Conteúdo |
|---|---|---|---|
| Console | 3 (Release) / 4 (Debug) | operador do teste | eventos legíveis: registro, estados de chamada, DTMF, erros traduzidos |
| Arquivo `logs/polphone-YYYYMMDD.log` | 5 | análise técnica | tudo do console + trace SIP completo + logs internos do PJSIP |

Níveis (idênticos aos do PJSIP para evitar tradução mental):
`0` desligado · `1` fatal · `2` erro · `3` aviso · `4` info · `5` debug · `6` trace.

### 10.2 Formato

```
2026-07-31T09:14:22.318-03:00 [INFO ] [sip ] REGISTER: 200 OK (expires=300)
2026-07-31T09:14:31.902-03:00 [INFO ] [call] estado: CALLING -> EARLY (180 Ringing)
2026-07-31T09:14:35.118-03:00 [INFO ] [dtmf] id=dtmf-0001 method=rfc4733 digit=* duration=160ms → OK
2026-07-31T09:14:40.001-03:00 [DEBUG] [pjsip] ...trace do PJSIP, já redigido...
```

Campos fixos: timestamp ISO-8601 com fuso · nível · categoria (`app`, `cfg`, `sip`, `call`, `media`,
`dtmf`, `audio`, `pjsip`) · mensagem. Categoria permite `findstr /C:"[dtmf]"` no arquivo.

### 10.3 Regras de redaction (obrigatórias)

Aplicadas pelo `Redactor` **em toda linha** que passa pelo `PjLogWriter` e pelo `Logger`.

| Dado | Regra | Antes → Depois |
|---|---|---|
| Senha SIP | **nunca** logada, em nenhum nível | `"password": "s3nh4"` → `"password": "***"` |
| `Authorization` / `Proxy-Authorization` | mascarar `response=`, `nonce=`, `cnonce=` | `response="a1b2..."` → `response="***"` |
| Número discado / URI de destino | manter os 4 últimos dígitos | `sip:5511987654321@pbx` → `sip:551198****4321@pbx` |
| `From`/`To`/`Contact` com ramal | manter ramal interno (≤ 5 dígitos) inteiro; mascarar externos | `sip:1001@pbx` → inalterado |
| Dígitos DTMF | `*` por dígito, salvo `dtmf.logDigits = true` | `digit=7` → `digit=*` |
| Corpo `application/dtmf-relay` no trace SIP | mascarar `Signal=` | `Signal=7` → `Signal=*` |
| IP interno / hostname do PABX | **não** mascarado (necessário ao diagnóstico); é dado local, não segredo | — |
| `User-Agent`, `Call-ID`, tags | inalterados (essenciais para correlação) | — |

`dtmf.logDigits = true` é uma escolha consciente do operador durante o experimento; o `Logger`
emite um `WARN` no início da sessão quando essa opção está ativa:

```
[AVISO] dtmf.logDigits=true — dígitos serão gravados em claro no log. Não use com dados sensíveis.
```

### 10.4 Requisitos não-funcionais do logging

1. **Thread-safe**: `write()` é chamado de T0, T1 e das threads de áudio; mutex interno.
2. **Não reentrante em PJSIP**: o `LogWriter` jamais chama API do PJSUA2 (risco de deadlock).
3. **Vive até o fim de `libDestroy()`**: o endpoint da tag 2.17 destrói o writer depois de emitir
   seus últimos logs; o `Logger` referenciado continua vivo (ARCHITECTURE §4.5, ADR-021).
4. **Rotação simples**: um arquivo por dia + limite de tamanho (ex.: 50 MB) com renomeação
   `.1`, `.2`; sem biblioteca externa.
5. **`flush` imediato** para níveis ≤ 2 (fatal/erro), *buffered* para os demais — para que um crash
   não perca a última linha relevante.
6. **Correlação**: todo envio de DTMF e toda chamada carregam um id (`dtmf-000N`, `call-000N`) que
   aparece em todas as linhas relacionadas, permitindo casar log ↔ captura Wireshark.

Na tag 2.17, `pjsua_logging_config.console_level` também limita a chamada ao callback customizado.
Com `logConfig.writer` instalado, deve receber o mesmo teto técnico do arquivo (normalmente 5); o
callback substitui a escrita nativa nesse ramo, e o `Logger` aplica separadamente o nível de cada sink.
Definir zero silenciaria o próprio `PjLogWriter`. Ver ADR-022.

### 10.5 O que nunca vai para o log

Senha em claro (nem no *dump* de configuração), conteúdo de mídia/RTP payload, caminho absoluto
contendo o nome do usuário do Windows (usar caminho relativo à raiz do projeto), e qualquer campo
marcado como sensível no JSON de configuração.

---

## 11. Estrutura de diretórios

```
POLPhone/
├── .gitignore
├── .gitmodules
├── LICENSE                              # GNU GPL v2
├── README.md
├── CMakeLists.txt
├── CMakePresets.json
│
├── cmake/
│   ├── PJSIP.cmake                      # localiza libs de third_party/pjproject e monta o alvo importado
│   ├── Warnings.cmake
│   └── config_site.h.in                 # template copiado para pjproject antes do build
│
├── config/
│   ├── polphone.config.example.json     # versionado, sem segredos
│   └── polphone.config.json             # IGNORADO pelo Git
│
├── docs/
│   ├── ARCHITECTURE.md
│   ├── IMPLEMENTATION_PLAN.md
│   ├── DTMF-DESIGN.md
│   ├── BUILD-STRATEGY.md
│   ├── DECISIONS.md
│   └── TEST-MATRIX.md                   # gerado na Etapa 16
│
├── scripts/
│   ├── verify-env.ps1                   # checa VS2022, SDK, CMake, submodule
│   ├── setup-pjproject.ps1              # submodule + config_site.h + build das libs
│   ├── build.ps1                        # cmake configure + build (Debug|Release x64)
│   ├── run.ps1
│   └── clean.ps1
│
├── src/
│   ├── main.cpp
│   ├── app/
│   │   ├── Application.h / .cpp
│   │   ├── ConsoleUi.h / .cpp
│   │   ├── CommandParser.h / .cpp
│   │   ├── AppState.h / .cpp
│   │   └── EventQueue.h
│   ├── config/
│   │   ├── AppConfig.h
│   │   ├── ConfigLoader.h / .cpp
│   │   └── ConfigValidator.h / .cpp
│   ├── logging/
│   │   ├── Logger.h / .cpp
│   │   ├── PjLogWriter.h / .cpp
│   │   └── Redactor.h / .cpp
│   ├── sip/
│   │   ├── SipEndpoint.h / .cpp
│   │   ├── SipAccount.h / .cpp
│   │   ├── SipCall.h / .cpp
│   │   ├── CallRegistry.h / .cpp
│   │   └── PjErrors.h / .cpp
│   ├── audio/
│   │   ├── AudioDeviceService.h / .cpp
│   │   └── ToneGenerator.h / .cpp
│   ├── dtmf/
│   │   ├── DtmfMethod.h
│   │   ├── DtmfPlan.h / .cpp
│   │   └── DtmfSender.h / .cpp
│   └── util/
│       ├── Result.h
│       ├── Strings.h / .cpp
│       └── Time.h / .cpp
│
├── tests/
│   ├── CMakeLists.txt
│   ├── test_main.cpp
│   ├── test_command_parser.cpp
│   ├── test_config_loader.cpp
│   ├── test_config_validator.cpp
│   ├── test_redactor.cpp
│   └── test_dtmf_plan.cpp
│
└── third_party/
    ├── pjproject/                       # SUBMODULE, fixado na tag 2.17
    ├── nlohmann/json.hpp                # vendorizado (MIT)
    └── doctest/doctest.h                # vendorizado (MIT)
```

### `.gitignore` (mínimo obrigatório)

```
/build/
/out/
/logs/
config/polphone.config.json
config/*.local.json
*.user
*.suo
.vs/
third_party/pjproject/pjlib/include/pj/config_site.h
third_party/pjproject/lib/
third_party/pjproject/bin/
**/x64/Debug/
**/x64/Release/
```

---

## 12. Riscos técnicos

| # | Risco | Prob. | Impacto | Mitigação |
|---|---|---|---|---|
| R1 | Build do pjproject 2.17 no VS2022 exige *retarget* e `config_site.h` inexistente | Alta | Bloqueia tudo | Etapa 1 isolada; `setup-pjproject.ps1` automatiza; `verify-env.ps1` valida antes |
| R2 | Mismatch de *runtime* (`/MT` × `/MD`) → LNK2038 | Alta | Bloqueia link | ADR-006: **tudo `/MD`**; script verifica configuração usada |
| R3 | RFC 4733 não negociado com o PABX (sem `telephone-event`) | Média | Um dos 3 métodos indisponível | Detectar e reportar antes de enviar; log do SDP negociado |
| R4 | In-band destruído por transcodificação no PABX/tronco | Média | Método in-band inútil no destino externo | Forçar PCMU/PCMA; documentar como resultado válido do experimento |
| R5 | SIP INFO aceito pelo PABX mas não repassado ao tronco | Média | Falso negativo no diagnóstico | Instrumentar no Asterisk (`pjsip set logger on`), não só no softphone |
| R6 | Crash no shutdown por ordem de destruição | Média | Instabilidade | Ordem explícita (5.3) + destruição diferida (6.5) + teste automatizado `--selftest` |
| R7 | Áudio unidirecional por NAT/firewall | Média | Falha de teste confundida com DTMF | PABX na LAN no MVP; log de IP/porta RTP negociados |
| R8 | Dispositivo de áudio ocupado por outro app (Teams/MicroSIP) | Média | Falha na abertura | Erro claro + `devices` + orientação de fechar concorrente |
| R9 | Índices de dispositivo mudam entre execuções (USB) | Média | Dispositivo errado silenciosamente | Seleção por nome (substring) com índice como fallback |
| R10 | Exceção escapando de callback → `terminate()` | Média | Crash | `noexcept` + try/catch em toda callback (regra 6.2.3) |
| R11 | Duplicidade de DTMF (in-band + RFC 4733 simultâneos) | Média | URA recebe dígito dobrado | `DtmfSender` mutuamente exclusivo; ver DTMF-DESIGN §6 |
| R12 | Log em nível 5 vazando `Authorization`/números | Média | Segurança | `Redactor` obrigatório no `PjLogWriter`; teste unitário do redator |
| R13 | Licenciamento: PJSIP GPLv2 + OpenSSL | Baixa | Jurídico | MVP **sem TLS/OpenSSL**; projeto sob GPL-2.0 (ADR-003, ADR-007) |
| R14 | Antivírus/Firewall do Windows bloqueando UDP | Média | "Não registra" sem causa aparente | `verify-env.ps1` + mensagem de erro orientada (§9.3) |
| R15 | Reamostragem 16k↔8k distorcendo o tom in-band | Baixa | DTMF in-band não detectado | `audio.clockRate = 8000` para os testes in-band |
| R16 | `pjsua2.hpp` incompatível com `/permissive-` | Média | Erros de compilação | Desativar `/permissive-` no alvo (ADR-005) |

---

## 13. Relação com os demais documentos

| Documento | Conteúdo |
|---|---|
| [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) | Etapas sequenciais, dependências, critérios, tarefas para o Codex |
| [DTMF-DESIGN.md](DTMF-DESIGN.md) | Os três métodos, APIs, duração/intervalo, duplicidade, codecs |
| [BUILD-STRATEGY.md](BUILD-STRATEGY.md) | VS2022, SDK, x64, integração do pjproject, libs e runtimes |
| [DECISIONS.md](DECISIONS.md) | ADRs com contexto, motivos, consequências e alternativas |
