# POLPhone Asterisk Lab

Laboratório local e descartável para testar o POLPhone contra Asterisk puro. **Não é um PABX de
produção**, não contém troncos, não possui rota PSTN e não deve ser conectado a VPN, VLAN ou rede
corporativa.

O build usa exclusivamente o repositório oficial `asterisk/asterisk`, tag `20.19.0`, commit
`f66917fe6da487155f80ab50403bf3d7bdc86183`. A imagem é construída localmente; nenhum script faz
push ou publica a imagem.

## Início rápido

No PowerShell 5.1 ou superior, a partir da raiz do POLPhone:

```powershell
.\scripts\lab-init.ps1
.\scripts\lab-up.ps1
.\scripts\lab-status.ps1
```

O `lab-init.ps1` gera segredos fortes em `.env`, arquivo ignorado pelo Git. Ele não inicia o
container e não exibe os segredos, a menos que `-ShowSecrets` seja informado explicitamente.
Em um host Linux/WSL sem Docker disponível no PATH do Windows PowerShell, use
`./scripts/lab-init.sh` a partir da raiz do repositório.

Para acompanhar somente resultados do coletor:

```powershell
.\scripts\lab-logs.ps1 -DtmfOnly
```

Para encerrar sem apagar a configuração local:

```powershell
.\scripts\lab-down.ps1
```

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
