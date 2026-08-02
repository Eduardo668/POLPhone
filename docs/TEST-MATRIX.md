# Matriz de validação de campo DTMF

Status do ensaio: **não executado**.

Preencha esta matriz somente durante um ensaio autorizado seguindo `FIELD-TEST-GUIDE.md`. Use aliases
para destinos e ambiente; não registre números, credenciais, IPs públicos ou caminhos para artefatos
sensíveis.

> **Não commitar dados de campo:** logs SIP/RTP e capturas `.pcap`/`.pcapng` contêm números reais e
> sinalização da chamada. Eles devem permanecer fora do repositório, com acesso restrito.

## Identificação não sensível do ensaio

| Campo | Valor |
|---|---|
| Data/hora e fuso | |
| Commit testado | |
| Versão exibida por `--version` | |
| Windows / arquitetura | |
| PABX e versão | |
| Alias do tronco/operadora | |
| Alias do destino interno | `ECO-INTERNO` |
| Alias da URA externa | `URA-A` |
| Codec negociado | |
| Operador/revisor | |

Use `sim`, `não`, `n/a` ou `inconclusivo` nas três colunas de resultado.

## Resultados

| # | Método | Duração (ms) | Destino | PABX recebeu? | Tronco enviou? | URA/receptor reagiu? | `correlationId` | Observação |
|---|---|---:|---|---|---|---|---|---|
| 1 | rfc4733 | 160 | ECO-INTERNO | | n/a | | | |
| 2 | info | 160 | ECO-INTERNO | | n/a | | | |
| 3 | inband | 160 | ECO-INTERNO | | n/a | | | |
| 4 | rfc4733 | 160 | URA-A | | | | | |
| 5 | info | 160 | URA-A | | | | | |
| 6 | inband | 160 | URA-A | | | | | |
| 7 | rfc4733 | 250 | URA-A | | | | | `gap=150 ms` |
| 8 | info | 250 | URA-A | | | | | `gap=150 ms` |
| 9 | inband | 250 | URA-A | | | | | `gap=150 ms` |

## Conclusão do ensaio

- Método recomendado: **a preencher**
- Duração/intervalo recomendados: **a preencher**
- Codec e condições necessárias: **a preencher**
- Método(s) rejeitado(s) e evidência: **a preencher**
- Limitações ou resultados inconclusivos: **a preencher**
- Revisão da conclusão por: **a preencher**

A conclusão deve citar as linhas da matriz, sem incorporar conteúdo bruto de logs ou capturas. Se
nenhum método tiver evidência nas três camadas aplicáveis, mantenha o status como `inconclusivo`.
