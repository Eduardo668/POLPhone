# POLPhone Asterisk Lab

Laboratório local e descartável para testar o POLPhone contra Asterisk puro. **Não é um PABX de
produção**, não contém troncos, não possui rota PSTN e não deve ser conectado a VPN, VLAN ou rede
corporativa.

O build usa exclusivamente o repositório oficial `asterisk/asterisk`, tag `20.19.0`, commit
`f66917fe6da487155f80ab50403bf3d7bdc86183`. A imagem é construída localmente; nenhum script faz
push ou publica a imagem.

## Início rápido no WSL

O Bash no WSL é a interface oficial. A partir da raiz do POLPhone:

```bash
./scripts/lab-init.sh
./scripts/lab-up.sh --build
./scripts/lab-status.sh
```

O `lab-init.sh` gera segredos fortes em `.env`, arquivo ignorado pelo Git. Ele não inicia containers
nem exibe credenciais. Nas próximas execuções, `lab-up.sh` pode ser usado sem `--build`; a imagem só
será construída se estiver ausente. Os scripts descobrem a raiz do projeto pela própria localização
e não dependem de `powershell.exe` nem do Docker no PATH do Windows.

Se o Docker for nativo do WSL e o IP virtual mudar, `lab-up.sh` bloqueia a subida. Use
`./scripts/lab-up.sh --no-build --use-wsl-ip` para atualizar explicitamente somente o `.env` e a
configuração local ignorada do POLPhone; nunca há fallback para `0.0.0.0`.

Para acompanhar somente resultados do coletor:

```bash
./scripts/lab-logs.sh --dtmf
```

Para encerrar sem apagar a configuração local:

```bash
./scripts/lab-down.sh
```

Os `.ps1` permanecem como wrappers opcionais que chamam esses mesmos scripts via `wsl.exe`,
preservando argumentos e exit code; não há uma segunda implementação operacional.

O roteiro completo, limitações de NAT e critérios de evidência estão em
[`../../docs/LAB-ASTERISK-GUIDE.md`](../../docs/LAB-ASTERISK-GUIDE.md).

## Superfície do laboratório

| Serviço | Bind padrão no host | Destino no container |
|---|---|---|
| chan_sip UDP | `127.0.0.1:15060/udp` | `5060/udp` |
| chan_pjsip UDP opcional | `127.0.0.1:15061/udp` | `5061/udp` |
| RTP | `127.0.0.1:10000-10100/udp` | `10000-10100/udp` |

Como redes Docker `internal` não publicam portas de forma consistente entre engines, um segundo
container local `polphone-asterisk-lab-gateway` faz somente o relay UDP. O Asterisk permanece ligado
exclusivamente à rede interna sem gateway; a rede de borda do relay tem masquerade e comunicação
inter-container desabilitados.

Ramais fictícios: `1001`, `1002` e o endpoint PJSIP opcional `2001`. Extensões de teste: `600`
(eco), `9991` (RFC 2833/4733), `9992` (SIP INFO), `9993` (in-band) e `9999` (auto).

O healthcheck comprova apenas que o Asterisk, `chan_sip`, UDP e o dialplan estão disponíveis. Ele não
comprova registro do POLPhone, áudio bidirecional nem entrega de DTMF.
