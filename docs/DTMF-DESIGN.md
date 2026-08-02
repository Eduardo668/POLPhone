# POLPhone — Projeto do subsistema DTMF

> Núcleo da prova técnica. Objetivo: enviar **o mesmo dígito** por **três caminhos independentes e
> explicitamente escolhidos**, com duração e intervalo controlados, e registrar exatamente o que foi
> enviado — para correlacionar com o comportamento da URA de destino.

---

## 1. Panorama dos três métodos

| | **RFC 4733 / RFC 2833** | **In-band** | **SIP INFO** |
|---|---|---|---|
| Onde trafega | RTP, *payload type* dinâmico `telephone-event` | RTP, dentro do áudio (tom senoidal duplo) | Sinalização SIP (requisição `INFO` no diálogo) |
| Precisa de negociação SDP | **Sim** (`a=rtpmap:101 telephone-event/8000`) | Não | Não (mas o peer precisa aceitar) |
| Sobrevive a transcodificação | Sim (é fora do áudio) | **Não** com codecs compressivos | Sim (não é mídia) |
| Sincronismo com o áudio | Bom (mesmo fluxo RTP, com timestamp) | Perfeito (é o áudio) | Ruim (caminho separado, pode chegar fora de ordem em relação ao RTP) |
| Falha típica | `telephone-event` ausente ou PT divergente | destruído por G.729/GSM/Opus/VAD | rejeitado com `415`/`501`, ou aceito e não repassado ao tronco |
| RFC | RFC 4733 (obsoleta RFC 2833) | — | RFC 2976 + `application/dtmf-relay` (de facto) |
| API PJSUA2 | `Call::sendDtmf` com `PJSUA_DTMF_METHOD_RFC2833` | **não existe** — `pjmedia_tonegen` + conference bridge | `Call::sendDtmf` com `PJSUA_DTMF_METHOD_SIP_INFO` |

> Nota de nomenclatura: RFC 2833 foi substituída pela RFC 4733. O PJSIP mantém o nome histórico
> `PJSUA_DTMF_METHOD_RFC2833` na enum. No POLPhone, o método é exposto ao usuário como `rfc4733`
> e aceita `rfc2833` como sinônimo.

---

## 2. Modelo de dados

```cpp
enum class DtmfMethod { Rfc4733, Inband, SipInfo };

struct DtmfSettings {          // vem de config.dtmf, ajustável em runtime
    DtmfMethod defaultMethod = DtmfMethod::Rfc4733;
    unsigned   durationMs    = 160;   // tempo "on" de cada dígito
    unsigned   gapMs         = 100;   // silêncio entre dígitos
    int        volumeDbm0    = -10;   // apenas in-band
    bool       localFeedback = false; // in-band audível localmente
    bool       logDigits     = false; // se false, dígitos são mascarados no log
};

struct DtmfRequest {
    std::string digits;        // já validado: 0-9 * # A-D , (vírgula = pausa)
    DtmfMethod  method;
    unsigned    durationMs;
    unsigned    gapMs;
    int         volumeDbm0;
    std::string correlationId; // ex.: "dtmf-0007" — aparece no log e permite casar com o pcap
};

struct DtmfDigitResult { char digit; bool ok; std::string detail; };
struct DtmfResult { bool ok; std::vector<DtmfDigitResult> perDigit; std::string summary; };
```

`DtmfPlan` (lógica pura, testável sem PJSIP) transforma `DtmfRequest` na sequência temporal:

```
plan("1,2#", duration=160, gap=100)
  → [ {digit:'1', onMs:160, offMs:100},
      {pause: 500},                       // ',' = pausa fixa de 500 ms
      {digit:'2', onMs:160, offMs:100},
      {digit:'#', onMs:160, offMs:100} ]
```

---

## 3. Método 1 — RFC 4733 / RFC 2833

### 3.1 API do PJSUA2

```cpp
pj::CallSendDtmfParam prm;
prm.method   = PJSUA_DTMF_METHOD_RFC2833;
prm.duration = settings.durationMs;   // ms; 0 = PJSUA_CALL_SEND_DTMF_DURATION_DEFAULT
prm.digits   = "5";
call.sendDtmf(prm);                   // lança pj::Error em falha
```

> A API legada `Call::dialDtmf(const std::string&)` **não** é usada: ela não permite escolher método
> nem duração. `Call::sendDtmf(CallSendDtmfParam&)` está disponível desde o PJSIP 2.9 e é o caminho
> único do POLPhone para RFC 4733 e SIP INFO.
> **Confirmar nomes exatos dos campos em `pjsua2/call.hpp` da tag 2.17 na Etapa 12.**

### 3.2 O que acontece na rede

O PJMEDIA gera pacotes RTP com o *payload type* negociado para `telephone-event`, tipicamente **101**:

```
RTP PT=101  event=5  E=0 volume=10 duration=160    (início, marker bit)
RTP PT=101  event=5  E=0 volume=10 duration=320    (continuação, mesmo timestamp)
RTP PT=101  event=5  E=0 volume=10 duration=480
RTP PT=101  event=5  E=1 volume=10 duration=480    (fim, retransmitido 3x)
```

Características importantes para o diagnóstico:
- Todos os pacotes de um evento compartilham o **mesmo timestamp RTP** (o do início do tom);
- O pacote de fim (`E=1`) é enviado **três vezes** para resistir a perda;
- A `duration` cresce em unidades de *timestamp* (amostras), não em ms.

### 3.3 Pré-condições verificadas antes de enviar

| Verificação | Falha ⇒ |
|---|---|
| Chamada existe e está em `PJSIP_INV_STATE_CONFIRMED` | `"sem chamada ativa"` |
| Mídia de áudio em `PJSUA_CALL_MEDIA_ACTIVE` | `"mídia de áudio não está ativa"` |
| `telephone-event` negociado | **aviso** antes da tentativa; a chamada à API prossegue e o erro real é reportado |

Quando `telephone-event` não foi negociado, o PJMEDIA retorna `PJMEDIA_RTP_EREMNORFC2833`
("remote does not support RFC 2833"). O POLPhone **traduz** esse erro:

```
[ERRO] dtmf rfc4733 falhou: o outro lado não negociou telephone-event nesta chamada.
       Codecs/atributos negociados: PCMU/8000, PCMA/8000 (sem telephone-event)
       Sugestão: testar --method info ou --method inband; verificar dtmfmode no trunk do Asterisk.
```

### 3.4 Como verificar a negociação

Fontes, em ordem de confiabilidade:

1. **Log de mensagens SIP do próprio POLPhone** (`logConfig.msgLogging = true`, nível 4/5):
   procurar no SDP de oferta e de resposta a linha `a=rtpmap:<pt> telephone-event/8000` e
   `a=fmtp:<pt> 0-15`.
2. `Call::dump(true, "")` — despejo textual do estado da chamada, inclui informação de stream.
3. Captura Wireshark filtrando `rtpevent`.

Se o *payload type* diferir entre oferta e resposta (ex.: nós oferecemos 101, o peer responde 96),
o PJSIP usa o do peer — mas gateways mal configurados às vezes não. Registrar o PT efetivo no log.

---

## 4. Método 2 — In-band

### 4.1 Conceito

Não existe API PJSUA2 para "enviar DTMF in-band". In-band **é áudio**: um tom composto por duas
senoides (linha + coluna do teclado DTMF) misturado no fluxo que vai para o remoto.

| Dígito | Baixa (Hz) | Alta (Hz) | | Dígito | Baixa | Alta |
|---|---|---|---|---|---|---|
| 1 | 697 | 1209 | | 7 | 852 | 1209 |
| 2 | 697 | 1336 | | 8 | 852 | 1336 |
| 3 | 697 | 1477 | | 9 | 852 | 1477 |
| 4 | 770 | 1209 | | * | 941 | 1209 |
| 5 | 770 | 1336 | | 0 | 941 | 1336 |
| 6 | 770 | 1477 | | # | 941 | 1477 |
| A–D | 697–941 | 1633 | | | | |

O `pjmedia_tonegen` já implementa o mapa padrão (`pjmedia_tonegen_set_digit_map` permite customizar,
mas o *default* cobre `0-9 * # A-D` e é o que usamos).

### 4.2 Implementação (`ToneGenerator`)

```
Criação (uma vez, na primeira utilização in-band):
  pool = pjsua_pool_create("polphone-tonegen", 1024, 1024);
  samplesPerFrame = clockRate * ptimeMs / 1000 * channelCount;
  pjmedia_tonegen_create2(pool, &name,
                          clockRate,          // = medConfig.clockRate da bridge
                          channelCount,       // 1
                          samplesPerFrame,    // = clockRate * 20 / 1000
                          16,                 // bits per sample
                          PJMEDIA_TONEGEN_LOOP off,
                          &tonegenPort);

Registro na bridge (duas alternativas — escolher na Etapa 14 conforme o header de 2.17):
  (a) PJSUA2:  class ToneMedia : public pj::AudioMedia {
                   void register(pjmedia_port* p, pj_pool_t* pool) {
                       registerMediaPort2((pj::MediaPort)p, pool);   // confirmar assinatura
                   }
               };
  (b) API C:   pjsua_conf_add_port(pool, tonegenPort, &toneSlot);

Conexão:
  (a) toneMedia.startTransmit(callAudioMedia);
  (b) pjsua_conf_connect(toneSlot, pjsua_call_get_conf_port(callId));
  opcional (feedback local): startTransmit(playbackDevMedia) / pjsua_conf_connect(toneSlot, 0)

Reprodução:
  pjmedia_tone_digit d[N];
  d[i].digit    = '5';
  d[i].on_msec  = settings.durationMs;
  d[i].off_msec = settings.gapMs;
  d[i].volume   = dbm0ToPcmAmplitude(settings.volumeDbm0); // pjmedia espera amplitude 1..32767
  pjmedia_tonegen_play_digits(tonegenPort, N, d, 0);

Conclusão:
pjmedia_tonegen_is_busy(tonegenPort) == 0   → sequência terminou
  pjmedia_tonegen_stop(tonegenPort)           → aborta

Destruição:
  desconectar da bridge → pjsua_conf_remove_port(toneSlot)
  pjmedia_port_destroy(tonegenPort)
  pj_pool_release(pool)
```

> **Confirmação na tag 2.17:** apesar do nome da configuração, o campo `volume` de
> `pjmedia_tone_digit` não recebe dBm0; ele recebe amplitude PCM entre 1 e 32767 (`0` seleciona
> `PJMEDIA_TONEGEN_VOLUME`). O POLPhone converte `volumeDbm0` por `32767 × 10^(dBm0/20)` antes de
> chamar o PJMEDIA.

> **Ponto de atenção:** `pjmedia_tonegen_play_digits` recebe a sequência inteira e a reproduz de
> forma assíncrona. O `DtmfSender` **não** deve dormir a duração total na thread do console; ele
> registra a requisição como "em voo" e a considera concluída quando `is_busy()` retorna 0
> (verificado no laço principal) ou após `duração_total + 500 ms` de segurança.

> **Ponto de atenção 2:** o tonegen é criado com o *clock rate da bridge*. Se `clockRate = 16000` e o
> codec negociado for PCMU (8000), o tom passa por reamostragem antes de virar RTP. Funciona, mas
> introduz distorção. Para os testes in-band, usar `audio.clockRate = 8000`.

> **Ponto de atenção 3:** o tonegen é conectado **ao slot da chamada**, não ao dispositivo de
> reprodução. Assim o tom vai para o remoto sem passar pelo microfone (portanto sem sofrer
> cancelamento de eco). O feedback local é uma conexão *adicional* e opcional.

### 4.3 Compatibilidade com codecs

| Codec | In-band funciona? | Observação |
|---|---|---|
| **PCMU (G.711 µ-law)** | **Sim** | Referência. Sem compressão perceptual. |
| **PCMA (G.711 A-law)** | **Sim** | Equivalente. |
| G.722 | Sim, com ressalvas | Wideband; a URA a 8 kHz recebe após transcodificação no PABX. |
| G.729 / G.723.1 | **Não** | Codecs CELP modelam voz, não tons puros. Dígito chega distorcido. |
| GSM | **Não** | Mesmo motivo. |
| iLBC | **Não** | Mesmo motivo. |
| Opus | **Não confiável** | Pode passar em bitrate alto, falha em baixo/DTX. |
| Speex | **Não** | Mesmo motivo. |

Consequências de projeto:

1. `config.codecs.priority` deve colocar **PCMU e PCMA no topo** e zerar os compressivos por padrão.
2. Antes de um envio in-band, o `DtmfSender` verifica o codec negociado. Se não for G.711/G.722:

   ```
   [AVISO] dtmf inband: codec negociado é G729/8000. Tons DTMF in-band provavelmente
           não serão reconhecidos. Prosseguindo mesmo assim (registro do experimento).
   ```

   Não bloqueia — o experimento negativo também é um resultado.
3. `medConfig.noVad = true` é **obrigatório**: com VAD ativo, o início do tom pode ser classificado
   como ruído e suprimido.

### 4.4 Efeito da transcodificação no PABX

Mesmo com PCMU no trecho softphone→Asterisk, se o tronco de saída usar G.729, o Asterisk transcodifica
e o tom in-band é destruído **depois** de sair do POLPhone. Isso é invisível no log do softphone —
por isso a validação de campo precisa de instrumentação no lado do Asterisk (ver §9).

---

## 5. Método 3 — SIP INFO

### 5.1 API do PJSUA2

```cpp
pj::CallSendDtmfParam prm;
prm.method   = PJSUA_DTMF_METHOD_SIP_INFO;
prm.duration = settings.durationMs;
prm.digits   = "5";
call.sendDtmf(prm);
```

### 5.2 O que vai na rede

O PJSIP envia, dentro do diálogo estabelecido:

```
INFO sip:9911234567@pabx.exemplo SIP/2.0
...
Content-Type: application/dtmf-relay
Content-Length: 26

Signal=5
Duration=160
```

Existe também o formato mais antigo `application/dtmf` (corpo apenas `5`). O PJSIP usa
`application/dtmf-relay`; o Asterisk com `dtmfmode=info` aceita ambos.

### 5.3 Respostas possíveis e o que significam

| Resposta ao INFO | Significado | Ação do POLPhone |
|---|---|---|
| `200 OK` | Aceito pelo próximo salto | log `OK` — **não garante** que chegou à URA |
| `415 Unsupported Media Type` | O peer não aceita `application/dtmf-relay` | erro traduzido; sugerir outro método |
| `481 Call/Transaction Does Not Exist` | Diálogo perdido | erro; a chamada provavelmente caiu |
| `501 Not Implemented` | Peer não implementa INFO | erro traduzido |
| sem resposta / `408` | Perda de sinalização | erro |

O `SipCall::onCallTsxState` é usado para capturar essa resposta e registrá-la no log com o
`correlationId` do envio. **Esse é o único método dos três em que existe confirmação de recebimento
no primeiro salto** — o que o torna a melhor ferramenta de diagnóstico do trecho softphone→PABX.

### 5.4 Limitação estrutural

SIP INFO é **sinalização**, não mídia. Ele chega ao PABX, mas **para chegar à URA da GoDaddy o
Asterisk precisa regenerá-lo** no método que o tronco aceita (normalmente RFC 4733 no RTP).
Se o trunk estiver com `dtmfmode=inband` ou `auto`, o INFO pode ser aceito com `200 OK` e ainda
assim não produzir nenhum dígito no destino. Documentar isso é parte do experimento.

---

## 6. Prevenção de duplicidade

Dígito duplicado (URA recebe "55" quando o usuário digitou "5") é uma das causas mais comuns de
falha de URA. Fontes de duplicidade e defesas:

| Fonte | Defesa no POLPhone |
|---|---|
| **Dois métodos ativos ao mesmo tempo** (ex.: tonegen tocando *e* `sendDtmf` RFC 4733) | O `DtmfSender` é **exclusivo por design**: um único `method` por requisição, nunca em paralelo. Não existe "enviar por todos". |
| **Requisições concorrentes** (usuário digita `dtmf 5` duas vezes rápido; ou comando repetido) | Flag `inFlight` + mutex. Nova requisição enquanto há uma em voo é **enfileirada** (fila máx. 1) ou **recusada** com mensagem clara. |
| **Tonegen ainda ocupado** | Checagem `pjmedia_tonegen_is_busy()` antes de `play_digits`; se ocupado, recusa. |
| **Auto-repeat do terminal / linha colada** | O `CommandParser` trata a linha inteira como **uma** requisição; `dtmf 555` é uma sequência de 3, não 3 comandos. |
| **PABX detectando in-band e regenerando RFC 4733** (`relaxdtmf`/`dtmfmode=auto` no Asterisk) | Fora do controle do softphone. **Documentado** como hipótese a testar: se o modo in-band produzir dígito duplo na URA, a causa é essa. O log do POLPhone prova que enviamos apenas uma vez. |
| **Retransmissão do pacote `E=1`** do RFC 4733 | Comportamento correto do protocolo (3 cópias do pacote final, mesmo timestamp). Receptores conformes deduplicam por timestamp. Não é bug. |
| **Eco do próprio tom in-band voltando pelo microfone** | O tonegen é conectado ao slot da chamada, **não** ao alto-falante (feedback local desligado por padrão). Sem alto-falante tocando o tom, não há eco a capturar. |

**Invariante formal do `DtmfSender`:**

> Em qualquer instante, no máximo **uma** `DtmfRequest` está em execução, e ela usa exatamente
> **um** dos três métodos. Nenhum caminho de código envia o mesmo dígito por mais de um método.

Log obrigatório para auditoria de duplicidade — antes e depois de **cada** dígito:

```
[DTMF] id=dtmf-0007 method=rfc4733 idx=1/1 digit=* duration=160ms gap=100ms  → enviando
[DTMF] id=dtmf-0007 method=rfc4733 idx=1/1 digit=* status=OK elapsed=163ms
```

---

## 7. Duração e intervalo

### 7.1 Parâmetros

| Parâmetro | Config | Faixa aceita | Default | Efeito |
|---|---|---|---|---|
| Duração ("on") | `dtmf.durationMs` | 40–2000 | **160** | Tempo do evento/tom. |
| Intervalo ("off") | `dtmf.gapMs` | 20–2000 | **100** | Silêncio entre dígitos consecutivos. |
| Volume | `dtmf.volumeDbm0` | -30–0 | **-10** | Só in-band. dBm0. |
| Pausa explícita | caractere `,` na string | — | 500 ms | Espera entre blocos (ex.: ramal após menu). |

### 7.2 Como cada método usa esses valores

| Método | Duração | Intervalo |
|---|---|---|
| RFC 4733 | `CallSendDtmfParam::duration` → duração do evento RTP | aplicado pelo `DtmfSender` entre chamadas sucessivas a `sendDtmf` |
| In-band | `pjmedia_tone_digit::on_msec` | `pjmedia_tone_digit::off_msec` — aplicado pelo próprio tonegen |
| SIP INFO | campo `Duration=` no corpo | aplicado pelo `DtmfSender` entre requisições INFO |

Portanto, para RFC 4733 e SIP INFO o intervalo é responsabilidade da aplicação; para in-band é do
tonegen. O `DtmfPlan` unifica isso para que os três produzam a mesma temporização observável.

### 7.3 Recomendações práticas (ponto de partida do experimento)

| Cenário | Duração | Intervalo | Justificativa |
|---|---|---|---|
| Padrão | 160 ms | 100 ms | Valor clássico do PJSIP; aceito pela maioria dos detectores |
| URA que não reconhece (1ª tentativa) | **250 ms** | **150 ms** | Detectores lentos ou com janela de integração maior |
| URA muito tolerante / testes rápidos | 100 ms | 60 ms | Mínimo prático; abaixo de ~80 ms a taxa de erro sobe |
| Nunca usar | < 40 ms | < 20 ms | Abaixo do mínimo da ITU-T Q.24 |

A ITU-T Q.24 especifica tom mínimo de 40 ms e intervalo mínimo de 40 ms; gateways reais costumam
exigir mais. **A hipótese principal para o caso GoDaddy é duração insuficiente ou método errado** —
por isso o parâmetro é ajustável em runtime (`dtmfcfg duration 250`) sem reiniciar a chamada.

---

## 8. Comportamento esperado

### 8.1 Fluxo de execução de `dtmf <digits>`

```
1. Parse e validação
     - dígitos ∈ [0-9 * # A-D ,]     → senão: erro, nada é enviado
     - método resolvido (--method ou modo da sessão)
     - duração/intervalo dentro da faixa
2. Guards
     - existe chamada?                → senão: "sem chamada ativa"
     - estado == CONFIRMED?           → senão: "chamada não está estabelecida (estado: EARLY)"
     - mídia == ACTIVE?               → senão: "mídia de áudio não está ativa"
     - DtmfSender livre?              → senão: "envio em andamento (id=dtmf-0006)"
3. Pré-checagens específicas do método (avisos, não bloqueios)
     - rfc4733: telephone-event negociado?
     - inband : codec é G.711/G.722?  + tonegen livre?
     - info   : nenhuma
4. Execução dígito a dígito, com correlationId
5. Relatório final
```

### 8.2 Saída esperada no console — caminho feliz

```
POLPhone [reg:OK][call:CONFIRMED][dtmf:rfc4733]> dtmf 5
[DTMF] id=dtmf-0001 method=rfc4733 digits=1 duration=160ms gap=100ms
[DTMF] id=dtmf-0001 1/1 digit=5 → OK (163 ms)
[DTMF] id=dtmf-0001 concluído: 1/1 enviado(s) com sucesso

POLPhone [reg:OK][call:CONFIRMED][dtmf:rfc4733]> dtmf 5 --method info
[DTMF] id=dtmf-0002 method=info digits=1 duration=160ms gap=100ms
[DTMF] id=dtmf-0002 1/1 digit=5 → INFO enviado
[SIP ] id=dtmf-0002 INFO respondido: 200 OK
[DTMF] id=dtmf-0002 concluído: 1/1 enviado(s) com sucesso

POLPhone [reg:OK][call:CONFIRMED][dtmf:rfc4733]> dtmf 5 --method inband --duration 250
[DTMF] id=dtmf-0003 method=inband digits=1 duration=250ms gap=100ms volume=-10dBm0
[AVISO] codec negociado: PCMU/8000 — compatível com in-band
[DTMF] id=dtmf-0003 1/1 digit=5 → tom gerado (250 ms)
[DTMF] id=dtmf-0003 concluído: 1/1 enviado(s) com sucesso
```

### 8.3 Saída esperada — caminhos de erro

```
> dtmf 5
[ERRO] dtmf: sem chamada ativa.

> dtmf 5 --method rfc4733
[ERRO] dtmf rfc4733: o outro lado não negociou telephone-event nesta chamada
       (status PJMEDIA_RTP_EREMNORFC2833). Codecs negociados: PCMU/8000.
       Alternativas: --method info | --method inband

> dtmf 5 --method info
[ERRO] dtmf info: PABX respondeu 415 Unsupported Media Type para application/dtmf-relay.

> dtmf 5 --method inband
[AVISO] codec negociado: G729/8000 — tons in-band provavelmente não serão reconhecidos.
[DTMF] id=dtmf-0009 1/1 digit=5 → tom gerado (160 ms)

> dtmf 5x9
[ERRO] dtmf: caractere inválido 'x'. Permitidos: 0-9 * # A-D e ',' (pausa 500 ms).

> dtmf 5 --duration 10
[ERRO] dtmf: duração 10 ms fora da faixa permitida (40–2000 ms).
```

### 8.4 Recepção de DTMF

O MVP apenas registra dígitos recebidos, com máscara:

```
[DTMF-RX] digit=* method=rfc4733 duration=160ms
```

`onDtmfEvent` (PJSIP ≥ 2.12) fornece método e duração; `onDtmfDigit` (legado) fornece só o dígito.
Preferir `onDtmfEvent`; manter `onDtmfDigit` apenas se a assinatura de 2.17 exigir.
**Não** há tratamento funcional de DTMF recebido — não é escopo.

---

## 9. Protocolo de validação de campo

Este é o teste que dá sentido ao projeto. Executar **na mesma chamada**, sequencialmente.

### 9.1 Roteiro

```
1. call <número da URA externa>
2. aguardar a URA atender e o menu começar
3. dtmf 1 --method rfc4733        → observar reação da URA
4. (aguardar o menu repetir)
5. dtmf 1 --method info           → observar reação da URA
6. (aguardar o menu repetir)
7. dtmf 1 --method inband         → observar reação da URA
8. repetir 3-7 com --duration 250 --gap 150
9. hangup
```

### 9.2 Instrumentação simultânea obrigatória

| Ponto | Ferramenta | O que capturar |
|---|---|---|
| POLPhone | log de arquivo nível 5 | SDP negociado, `correlationId`, timestamps de envio |
| POLPhone ↔ Asterisk | Wireshark / `tcpdump -i any -s0 -w polphone.pcap udp` | RTP `rtpevent`, INFO, RTP payload |
| Asterisk | `pjsip set logger on` / `sip set debug on` | recepção do INFO, resposta enviada |
| Asterisk | `rtp set debug on` | eventos RFC 4733 recebidos |
| Asterisk ↔ tronco | captura na interface do tronco | **o que o PABX regenerou** — decisivo |
| Asterisk | `core set verbose 5` + dialplan de teste com `Read()` | dígito efetivamente decodificado |

### 9.3 Matriz de resultados (preencher em `docs/TEST-MATRIX.md`)

| # | Método | Duração | Destino | PABX recebeu? | Tronco enviou? | URA reagiu? | Observação |
|---|---|---|---|---|---|---|---|
| 1 | rfc4733 | 160 | ramal interno eco | | | | |
| 2 | info | 160 | ramal interno eco | | | | |
| 3 | inband | 160 | ramal interno eco | | | | |
| 4 | rfc4733 | 160 | URA externa | | | | |
| 5 | info | 160 | URA externa | | | | |
| 6 | inband | 160 | URA externa | | | | |
| 7 | rfc4733 | 250 | URA externa | | | | |
| 8 | info | 250 | URA externa | | | | |
| 9 | inband | 250 | URA externa | | | | |

### 9.4 Interpretação dos resultados

| Padrão observado | Conclusão provável | Próxima ação |
|---|---|---|
| Nenhum método funciona na URA, todos funcionam no ramal interno | Problema está no tronco/operadora | Ajustar `dtmfmode` do trunk no Asterisk |
| Só rfc4733 funciona | Cadeia end-to-end suporta RFC 4733 | Fixar rfc4733 no softphone de produção |
| Só in-band funciona | Tronco não repassa telephone-event | Forçar G.711 e in-band; verificar transcodificação |
| INFO retorna 200 OK mas URA não reage | Asterisk aceita e não regenera | Configurar tradução INFO→RFC4733 no trunk |
| 160 ms falha, 250 ms funciona | Janela de detecção da URA | Aumentar duração no perfil de produção |
| URA recebe dígito duplicado | `relaxdtmf`/`dtmfmode=auto` regenerando | Desativar detecção redundante no Asterisk |

---

## 10. Itens a confirmar contra o código-fonte da tag 2.17

Estes pontos devem ser verificados no header real durante as Etapas 12–14, **antes** de escrever o código:

| # | Item | Onde verificar |
|---|---|---|
| C1 | Assinatura e campos de `pj::CallSendDtmfParam` | `pjsip/include/pjsua2/call.hpp` |
| C2 | Valores da enum `pjsua_dtmf_method` | `pjsip/include/pjsua-lib/pjsua.h` |
| C3 | `Call::getAudioMedia(int)` existe? senão usar `typecastFromMedia(getMedia(i))` | `pjsua2/call.hpp`, `pjsua2/media.hpp` |
| C4 | `AudioMedia::registerMediaPort2(MediaPort, pj_pool_t*)` disponível? | `pjsua2/media.hpp` |
| C5 | Assinatura de `pjmedia_tonegen_create2` e `pjmedia_tone_digit` | `pjmedia/include/pjmedia/tonegen.h` |
| C6 | `onDtmfEvent(OnDtmfEventParam&)` vs `onDtmfDigit` | `pjsua2/call.hpp` |
| C7 | Constante do erro "remoto sem RFC 2833" (`PJMEDIA_RTP_EREMNORFC2833`) | `pjmedia/include/pjmedia/errno.h` |
| C8 | Existe knob para **não ofertar** `telephone-event` no SDP? | `pjmedia/include/pjmedia/config.h` — **não é necessário no MVP**: basta não enviar RFC 4733 |
| C9 | `MediaConfig::noVad` e nome exato de `ecTailLen` | `pjsua2/endpoint.hpp` |

Regra: **nenhuma dessas assinaturas deve ser assumida no código sem leitura prévia do header da tag 2.17.**
