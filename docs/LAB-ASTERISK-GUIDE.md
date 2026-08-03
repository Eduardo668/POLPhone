# Laboratório Asterisk isolado para o POLPhone

Este laboratório reproduz somente a superfície SIP/RTP/DTMF necessária para comparar o POLPhone com
um ambiente legado que utiliza `chan_sip`. Ele não reproduz Issabel, FreePBX, banco de dados,
interface web, troncos, rotas externas, PSTN, voicemail, filas, conferência ou gravação.

> **Não use em produção.** Não conecte este Compose a VPN, VLAN, rede do PABX real ou interface
> corporativa. Use somente ramais, segredos, IPs e evidências fictícios do laboratório.

## 1. Versões e origem

| Componente | Valor fixado |
|---|---|
| Repositório | `https://github.com/asterisk/asterisk.git` |
| Série | Asterisk 20 |
| Tag | `20.19.0` |
| Commit da tag | `f66917fe6da487155f80ab50403bf3d7bdc86183` |
| Tag anotada | `daf83c5e57de5df8d9321faff31287a450af9641` |
| Base | Debian `12.12-slim`, manifesto linux/amd64 `sha256:a1363ada3b45cb3ebc74c78943558f8b0c2b59aaa194d8224e1b02cfd5d78583` |

A release `20.19.0` existe oficialmente e o Dockerfile falha se a tag resolver para outro commit. O
`chan_sip` ainda existe na série 20 e foi removido a partir do Asterisk 21. A aplicação
`SIPDtmfMode()` dessa tag declara oficialmente os modos `rfc2833`, `info` e `inband`.

Referências oficiais:

- [release Asterisk 20.19.0](https://github.com/asterisk/asterisk/releases/tag/20.19.0);
- [código da tag 20.19.0](https://github.com/asterisk/asterisk/tree/20.19.0);
- [documentação de SIPDtmfMode no Asterisk 20](https://docs.asterisk.org/Asterisk_20_Documentation/API_Documentation/Dialplan_Applications/SIPDtmfMode/);
- [situação do chan_sip](https://docs.asterisk.org/Configuration/Channel-Drivers/SIP/Configuring-chan_sip/).

Os sons vêm do pacote oficial `asterisk-core-sounds-en` versão `1.6.1`, selecionado pelo próprio
build da tag para ulaw e alaw e verificado pelo mecanismo de checksum do Asterisk. `Read()` usa
`beep`; `SayDigits()` usa os arquivos de dígitos oficiais. Se um símbolo não possuir anúncio no
pacote, o marcador `POLPHONE_LAB_DTMF` no log continua sendo a evidência autoritativa.

## 2. Isolamento

O Compose aplica:

- rede bridge exclusiva `polphone-asterisk-lab-net` com `internal: true`;
- Asterisk conectado somente a essa rede, sem gateway ou rota externa;
- relay UDP não-root separado, com portas publicadas somente no IP indicado por `LAB_BIND_IP`, que
  é `127.0.0.1` por padrão;
- rede de borda do relay com IP masquerade e comunicação inter-container desabilitados;
- nenhum `privileged`, `network_mode: host` ou montagem de `docker.sock`;
- todas as capabilities removidas e `no-new-privileges` habilitado;
- root filesystem somente leitura e diretórios transitórios em `tmpfs`;
- processo Asterisk como UID/GID não-root `10001`;
- `restart: "no"` e shutdown por SIGTERM entregue diretamente ao Asterisk;
- dialplan sem curingas, troncos, `register =>`, rotas externas ou aplicações PSTN.

A rede interna impede o container Asterisk de criar conexões externas. O relay de borda não contém
destinos configuráveis: encaminha somente 5060, 5061 e a faixa RTP ao nome interno `asterisk`; sua
rede não usa masquerade. O acesso durante o **build** é usado somente para obter Debian, o
repositório oficial do Asterisk, o pjproject empacotado pelo Asterisk e os sons oficiais. A imagem
final não contém Git, compilador ou ferramentas de build.

## 3. Inicializar no WSL

O caminho oficial é Bash dentro do WSL, com Docker Engine/Desktop e Docker Compose v2 acessíveis
diretamente nessa distribuição. A partir de qualquer diretório, invoque o script pelo caminho do
checkout; ele descobre a raiz pela própria localização e executa os comandos a partir dela.

```bash
./scripts/lab-init.sh
```

O script cria `lab/asterisk/.env`, gera segredos criptograficamente aleatórios para `1001`, `1002` e
`2001`, confirma que o arquivo está ignorado e não inicia containers. Um `.env` existente é
preservado; para substituí-lo conscientemente:

```bash
./scripts/lab-init.sh --force
```

Para exibir localmente as credenciais recém-geradas, acrescente `--show-secrets`. Essa opção é
explícita; não grave sua saída em logs ou capturas.

## 4. Construir e subir

```bash
./scripts/lab-up.sh --build
```

O script executa `docker compose config --quiet`, inicia os serviços e espera ambos os healthchecks.
Sem opção de build, o Compose constrói somente se a imagem estiver ausente. Use `--build` para forçar
ou `--no-build` para impedir qualquer construção; `--timeout 300` altera o limite de espera. Em caso
de falha, o script mostra `compose ps`, as últimas linhas dos logs e retorna código diferente de zero.

Em Docker nativo no WSL, o script compara `LAB_BIND_IP` e `LAB_ADVERTISED_IP` com o IPv4 de origem
da rota padrão. Se o IP virtual mudou, a subida é bloqueada com uma mensagem clara. A atualização
somente dos dois arquivos locais ignorados exige opção explícita:

```bash
./scripts/lab-up.sh --no-build --use-wsl-ip
```

Essa opção nunca utiliza `0.0.0.0` e não modifica configuração global do WSL, Windows ou firewall.

Comandos equivalentes para diagnóstico manual:

```bash
./scripts/lab-status.sh
./scripts/lab-logs.sh --asterisk --tail 120 --no-follow
```

O healthcheck confirma:

1. resposta da CLI do Asterisk 20.19.0;
2. `chan_sip.so` em estado `Running`;
3. `9999@from-lab` carregada;
4. socket UDP de `chan_sip` na porta interna 5060.

Isso **não** valida registro, mídia ou DTMF.

## 5. NAT, SIP e RTP

Fluxo padrão:

```text
POLPhone no Windows
  -> 127.0.0.1:15060/udp
  -> publicação de porta do Docker
  -> chan_sip no container:5060/udp
```

`sip.conf` usa `externaddr=${LAB_ADVERTISED_IP}:${LAB_CHAN_SIP_HOST_PORT}`, `directmedia=no`,
`force_rport` e `comedia`. Um relay UDP local atravessa a fronteira da rede Docker sem fornecer rota
externa ao Asterisk. Assim, sinalização e RTP permanecem ancorados no Asterisk. A faixa RTP interna e
externa é idêntica (`10000-10100` por padrão). PJSIP fica separado em 5061/15061.

No Docker Desktop/WSL, o encaminhamento UDP localhost e a aprendizagem simétrica de RTP podem variar
conforme versão, backend e firewall. Primeiro teste com `127.0.0.1`. Se houver registro mas não áudio:

1. descubra um IP local dedicado do Windows que **não** pertença a VPN/VLAN corporativa;
2. restrinja o Windows Firewall ao computador/rede de teste;
3. defina explicitamente no `.env`:

   ```dotenv
   LAB_BIND_IP=<IP_LOCAL_WINDOWS>
   LAB_ADVERTISED_IP=<IP_LOCAL_WINDOWS>
   ```

4. execute `./scripts/lab-up.sh --allow-lan-exposure` somente após revisar o bind;
5. ajuste `idUri`, `registrarUri` e `domain` na configuração local do POLPhone.

O script nunca troca automaticamente para `0.0.0.0`; esse valor é rejeitado. Não use uma interface
de VPN, uma VLAN corporativa ou o endereço do PABX real.

## 6. Configurar o POLPhone

Copie o exemplo sem segredos:

```bash
cp config/polphone.config.lab.example.json config/polphone.config.lab.json
```

No arquivo local ignorado, substitua apenas `REPLACE_WITH_LAB_SIP_1001_SECRET` pelo valor local de
`LAB_SIP_1001_SECRET`. Com o bind padrão, mantenha:

```text
idUri       = sip:1001@127.0.0.1
registrar   = sip:127.0.0.1:15060
realm       = *
username    = 1001
domain      = 127.0.0.1
proxy       = vazio
transport   = UDP
```

O exemplo prioriza PCMU e PCMA, desabilita G722, usa 8 kHz, mono, ptime de 20 ms e `noVad=true`.
Nunca versione o arquivo local gerado.

Inicie o POLPhone:

```powershell
.\build\Release\polphone.exe --config .\config\polphone.config.lab.json
```

## 7. Dialplan

| Extensão | Finalidade | Modo aplicado |
|---|---|---|
| `600` | eco de áudio bidirecional | não aplicável |
| `9999` | coletor genérico | `auto`, vindo do peer |
| `9991` | coletor controlado RFC 2833/4733 | `SIPDtmfMode(rfc2833)` |
| `9992` | coletor controlado SIP INFO | `SIPDtmfMode(info)` |
| `9993` | coletor controlado in-band | `SIPDtmfMode(inband)` |
| `1001`, `1002` | chamada interna opcional entre peers fictícios | configuração do peer |

Os peers aceitam somente ulaw e alaw, usam `directmedia=no`, `nat=force_rport,comedia`, `qualify=yes`
e transporte UDP. Não existe rota genérica; somente as extensões exatas acima são discáveis.

## 8. Roteiro de teste

Mantenha `./scripts/lab-logs.sh --dtmf` aberto em outro terminal e preencha
[`LAB-DTMF-MATRIX.md`](LAB-DTMF-MATRIX.md).

1. Rode `./scripts/lab-init.sh` e `./scripts/lab-up.sh --build`.
2. Confirme `healthy` com `./scripts/lab-status.sh`.
3. Inicie o POLPhone com a configuração local de laboratório.
4. Execute `status` e confirme o registro fictício `1001`.
5. Execute `call 600`, fale e confirme o eco; depois `hangup`.
6. Execute `call 9991`, aguarde o beep e envie:

   ```text
   dtmf 1 --method rfc4733 --duration 160
   ```

7. Confirme exatamente uma linha aceita para o dígito e nenhum recebimento duplicado; encerre.
8. Execute `call 9992`, aguarde o beep e envie:

   ```text
   dtmf 2 --method info --duration 160
   ```

9. Confirme exatamente um dígito; encerre.
10. Execute `call 9993`, confirme PCMU/PCMA, aguarde o beep e envie:

    ```text
    dtmf 3 --method inband --duration 160
    ```

11. Confirme exatamente um dígito; encerre.
12. Repita os três testes com `--duration 250 --gap 150`.
13. Verifique que cada requisição produz exatamente um dígito no coletor.
14. Use `quit` no POLPhone e `./scripts/lab-down.sh` no laboratório.

Uma linha `POLPHONE_LAB_DTMF` com `readstatus=OK` e o dígito esperado é evidência de recepção pelo
Asterisk. O retorno falado é evidência adicional. Inicialização, healthcheck, SIP `200 OK` isolado ou
presença de pacote RTP não comprovam entrega DTMF por si só.

## 9. CLI e debug manual

Dentro de `lab/asterisk`:

```bash
docker compose exec asterisk asterisk -rx "core show version"
docker compose exec asterisk asterisk -rx "module show like chan_sip"
docker compose exec asterisk asterisk -rx "module show like chan_pjsip"
docker compose exec asterisk asterisk -rx "sip show peers"
docker compose exec asterisk asterisk -rx "sip show peer 1001"
docker compose exec asterisk asterisk -rx "dialplan show 9999@from-lab"
docker compose exec asterisk asterisk -rx "core show channels"
docker compose exec asterisk asterisk -rx "rtp set debug on"
docker compose exec asterisk asterisk -rx "sip set debug on"
```

Ative os dois últimos somente durante uma investigação curta. Desative depois:

```bash
docker compose exec asterisk asterisk -rx "rtp set debug off"
docker compose exec asterisk asterisk -rx "sip set debug off"
```

Não versione logs ou capturas. O laboratório não exige `.pcap` para sua validação básica.

## 10. Parar e resetar

Parar preservando `.env`:

```bash
./scripts/lab-down.sh
```

Remover também volumes transitórios:

```bash
./scripts/lab-down.sh --volumes
```

Reset completo com confirmação explícita:

```bash
./scripts/lab-reset.sh
```

O reset exige `RESETAR-LAB`, executa `down --volumes`, remove somente dados transitórios dentro do
laboratório e preserva `.env`. Use `--yes` para automação explícita e `--regenerate-env` para gerar
novas credenciais sem exibi-las. Imagens só são removidas quando
`./scripts/lab-down.sh --remove-images` é solicitado.

## 11. PowerShell opcional

Os arquivos `scripts/lab-*.ps1` são wrappers finos. Eles localizam `wsl.exe`, convertem o caminho do
checkout e repassam argumentos e exit code ao script Bash correspondente; não implementam Docker
separadamente. Exemplo opcional no Windows:

```powershell
.\scripts\lab-up.ps1 --build --timeout 300
```

Se WSL não estiver disponível, o wrapper encerra com mensagem clara. Docker no PATH do Windows não é
necessário.

## 12. Validação automatizada

Em Bash/WSL:

```bash
./lab/asterisk/scripts/validate-lab.sh
```

O script valida estrutura, `docker compose config`, bind localhost, rede interna, restrições de
segurança, versão/commit, `chan_sip`, peers, dialplan, ausência de padrões de trunk/rota externa,
renderização sem placeholders e regras do Git. Build, healthcheck e chamadas são níveis de evidência
separados.
