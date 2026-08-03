# Guia de validação de campo DTMF

Este roteiro valida, de ponta a ponta, os três métodos DTMF do POLPhone contra um ramal interno de
teste e uma URA externa. Execute-o somente em ambiente autorizado e com o binário **Release**.

> **Dados sensíveis:** credenciais, números reais, logs SIP/RTP, dumps e capturas `.pcap`/`.pcapng`
> não podem ser commitados. Guarde-os em uma pasta externa ao clone, com acesso restrito. Registre em
> `docs/TEST-MATRIX.md` apenas aliases como `URA-A` e `ECO-INTERNO`, nunca os números reais.

## 1. Pré-requisitos

- acesso autorizado ao ramal SIP, ao destino interno de eco/`Read()` e à URA externa;
- acesso de observação ao Asterisk/PABX e à interface do tronco;
- Wireshark ou `tcpdump` no ponto POLPhone ↔ PABX e no ponto PABX ↔ tronco;
- relógios do Windows e do PABX sincronizados;
- headset ou dispositivos de áudio já conferidos com `--list-devices`;
- uma pasta fora do repositório, por exemplo `C:\POLPhone-field\<data-hora>`.

Não execute captura em interfaces, chamadas ou destinos sem autorização. Restrinja a retenção dos
artefatos ao tempo necessário para o diagnóstico.

## 2. Preparar o binário e a configuração

Na raiz do repositório, em PowerShell:

```powershell
.\scripts\verify-env.ps1
.\scripts\build.ps1 -Config Release
.\build\Release\polphone_cli.exe --version
Copy-Item .\config\polphone.config.example.json .\config\polphone.config.json
```

Edite apenas o arquivo local ignorado `config\polphone.config.json`:

- preencha a conta SIP e o domínio reais;
- mantenha `audio.clockRate=8000`, `audio.noVad=true` e priorize PCMU/PCMA;
- mantenha `dtmf.logDigits=false`;
- use `logging.fileLevel=5` e `logging.sipMessageTrace=true`;
- altere `logging.directory` para a pasta externa criada para o ensaio.

Valide o bootstrap antes de usar credenciais reais:

```powershell
.\build\Release\polphone_cli.exe --config .\config\polphone.config.json --selftest
```

O comando deve retornar `0` e terminar o log com `encerramento concluído`.

## 3. Iniciar a instrumentação

Comece todas as fontes antes da primeira chamada e anote o horário inicial. Exemplos, que devem ser
adaptados às interfaces e à tecnologia do PABX:

| Ponto | Instrumentação | Evidência esperada |
|---|---|---|
| POLPhone | log de arquivo nível 5 | SDP, codec, `correlationId` e timestamps |
| POLPhone ↔ PABX | Wireshark ou `tcpdump -i <interface> -s0 -w <pasta-externa>/polphone.pcap udp` | RTP `telephone-event`, INFO e áudio RTP |
| Asterisk PJSIP | `pjsip set logger on` | INFO recebido e resposta SIP |
| Asterisk chan_sip | `sip set debug on` | equivalente para instalações legadas |
| Asterisk RTP | `rtp set debug on` | eventos RFC 4733 recebidos |
| Dialplan interno | `core set verbose 5` e aplicação `Read()` | dígito decodificado pelo PABX |
| PABX ↔ tronco | captura na interface autorizada do tronco | método que o PABX efetivamente regenerou |

Não habilite simultaneamente `pjsip set logger on` e `sip set debug on` se apenas um dos canais for
usado. Ao final, desabilite o debug para não coletar tráfego além do ensaio.

## 4. Teste interno de controle

Inicie o console:

```powershell
.\build\Release\polphone_cli.exe --config .\config\polphone.config.json
```

Faça uma chamada ao destino interno de eco/`Read()` e, após `CONFIRMED` com mídia ativa, execute
separadamente:

```text
dtmf 1 --method rfc4733 --duration 160
dtmf 1 --method info --duration 160
dtmf 1 --method inband --duration 160
```

Aguarde a conclusão e a confirmação do receptor entre os envios. Preencha as linhas 1–3 da matriz e
encerre com `hangup`. Se nenhum método for observado no PABX, corrija primeiro registro, mídia,
instrumentação ou dialplan; o teste externo ainda não será conclusivo.

## 5. Teste da URA externa

Use uma única chamada para comparar os métodos sob as mesmas condições:

```text
call <alias-da-URA>
```

Após a URA atender e o menu começar, execute um comando por repetição completa do menu:

```text
dtmf 1 --method rfc4733 --duration 160
dtmf 1 --method info --duration 160
dtmf 1 --method inband --duration 160
dtmf 1 --method rfc4733 --duration 250 --gap 150
dtmf 1 --method info --duration 250 --gap 150
dtmf 1 --method inband --duration 250 --gap 150
hangup
```

Para cada envio:

1. aguarde o menu voltar ao mesmo ponto antes do próximo método;
2. anote apenas o `correlationId`, o codec, a resposta da URA e os horários necessários à correlação;
3. confirme separadamente se o PABX recebeu o evento e se o tronco o enviou/regenerou;
4. marque `inconclusivo` se a URA não repetir o menu ou se alguma captura estiver ausente;
5. não reenvie por outro método enquanto houver uma requisição DTMF em andamento.

Preencha as linhas 4–9 de `docs/TEST-MATRIX.md`. Repita uma linha somente quando necessário e
documente a repetição na observação, sem substituir um resultado desfavorável.

## 6. Encerrar e conferir

1. Use `hangup` e depois `quit`; em interrupção operacional, use `Ctrl+C` uma única vez.
2. Confirme o código de saída (`0` normal ou `130` por `Ctrl+C`).
3. Confirme `encerramento concluído` como última mensagem funcional do log.
4. Desabilite os debugs do PABX e finalize as capturas.
5. Mantenha logs e capturas fora do repositório; não os anexe a commits ou issues públicas.
6. Execute `git status --short` e confirme que nenhum artefato de campo aparece.

## 7. Interpretar e concluir

| Padrão observado | Conclusão provável | Próxima ação |
|---|---|---|
| Todos funcionam internamente, nenhum funciona na URA | falha no tronco/operadora | ajustar `dtmfmode` do trunk |
| Só RFC 4733 funciona | cadeia suporta eventos RTP | usar `rfc4733` no perfil de produção |
| Só in-band funciona | tronco não repassa `telephone-event` | forçar G.711 e revisar transcodificação |
| INFO recebe 2xx, mas a URA não reage | PABX aceita INFO sem regenerar | configurar tradução INFO → método do tronco |
| 160 ms falha e 250 ms funciona | janela de detecção maior | usar 250 ms no perfil validado |
| A URA recebe dígito duplicado | detecção/regeneração redundante | revisar `relaxdtmf`/`dtmfmode=auto` |

Uma conclusão só é válida quando a matriz distingue as três camadas: POLPhone → PABX, PABX →
tronco e reação da URA. `200 OK` no SIP INFO, isoladamente, não comprova entrega à URA.
