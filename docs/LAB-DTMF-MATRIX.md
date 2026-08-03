# Matriz de validação DTMF — laboratório Asterisk

Status: **não executado**.

Preencha somente com dados fictícios do laboratório. Não cole credenciais, dumps SIP completos,
endereços corporativos ou capturas reais. O container saudável não altera este status.

## Ambiente

| Campo | Valor |
|---|---|
| Commit POLPhone | |
| Imagem local | `polphone-asterisk-lab:20.19.0-local` |
| Asterisk | `20.19.0` |
| Driver principal | `chan_sip` |
| Docker / Compose | |
| Backend Docker Desktop/WSL | |
| Bind usado | `127.0.0.1` ou IP local fictício |
| Data/hora e fuso | |

## Resultados

| Extensão | Método | Codec | Duração | POLPhone enviou | Asterisk recebeu | Quantidade | Retorno falado | Resultado |
|---|---|---|---:|---|---|---:|---|---|
| 600 | eco | PCMU/PCMA | n/a | n/a | n/a | n/a | eco bidirecional | não executado |
| 9991 | RFC 4733 | PCMU/PCMA | 160 ms | | | | | não executado |
| 9991 | RFC 4733 | PCMU/PCMA | 250 ms | | | | | não executado |
| 9992 | SIP INFO | PCMU/PCMA | 160 ms | | | | | não executado |
| 9992 | SIP INFO | PCMU/PCMA | 250 ms | | | | | não executado |
| 9993 | in-band | PCMU/PCMA | 160 ms | | | | | não executado |
| 9993 | in-band | PCMU/PCMA | 250 ms | | | | | não executado |
| 9999 | auto | PCMU/PCMA | 160 ms | | | | | não executado |

Use `sim`, `não` ou `inconclusivo`. `Quantidade` deve ser exatamente `1` para aprovação. Em SIP INFO,
uma resposta 2xx sem `Read()` bem-sucedido permanece inconclusiva.

## Conclusão

- Registro do ramal 1001: **não executado**
- Eco de áudio: **não executado**
- RFC 4733: **não executado**
- SIP INFO: **não executado**
- In-band: **não executado**
- Ausência de duplicidade: **não executado**
- Limitações observadas: **a preencher**
