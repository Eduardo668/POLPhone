# POLPhone — Registro de Decisões Arquiteturais (ADRs)

> Formato: contexto, decisão, motivos, consequências (boas **e** ruins) e alternativas efetivamente
> consideradas. Uma decisão só muda por um novo ADR que **supersede** o anterior — ADRs não são editados
> depois de aceitos, exceto para corrigir erro factual.
>
> Status possíveis: `Proposto` · `Aceito` · `Substituído por ADR-NNN` · `Revogado`.

| ADR | Título | Status |
|---|---|---|
| [ADR-001](#adr-001--c17-nativo-com-pjsippjsua2-em-vez-de-fork-do-microsip) | C++17 nativo com PJSIP/PJSUA2, sem fork do MicroSIP | Aceito |
| [ADR-002](#adr-002--mvp-somente-console-sem-interface-gráfica) | MVP somente console, sem interface gráfica | Aceito |
| [ADR-003](#adr-003--licença-gnu-gpl-v2) | Licença GNU GPL v2 | Aceito |
| [ADR-004](#adr-004--pjproject-como-git-submodule-fixado-na-tag-217) | pjproject como git submodule fixado na tag 2.17 | Aceito |
| [ADR-005](#adr-005--cmake-gerando-solução-do-visual-studio-2022) | CMake gerando solução do Visual Studio 2022 | Aceito |
| [ADR-006](#adr-006--runtime-c-dinâmico-md-em-todo-o-projeto) | Runtime C dinâmico (`/MD`) em todo o projeto | Aceito |
| [ADR-007](#adr-007--config_siteh-mínimo-sem-vídeo-sem-tls-e-sem-codecs-compressivos) | `config_site.h` mínimo: sem vídeo, sem TLS, sem codecs compressivos | Aceito |
| [ADR-008](#adr-008--configuração-em-json-local-ignorado-pelo-git) | Configuração em JSON local ignorado pelo Git | Aceito |
| [ADR-009](#adr-009--apenas-três-dependências-pjproject-nlohmannjson-e-doctest) | Apenas três dependências: pjproject, nlohmann/json e doctest | Aceito |
| [ADR-010](#adr-010--modelo-de-threads-threadcnt--1-com-console-na-thread-principal) | Modelo de threads: `threadCnt = 1` com console na thread principal | Aceito |
| [ADR-011](#adr-011--destruição-diferida-de-objetos-call) | Destruição diferida de objetos `Call` | Aceito |
| [ADR-012](#adr-012--seleção-explícita-do-método-dtmf-sem-fallback-automático) | Seleção explícita do método DTMF, sem fallback automático | Aceito |
| [ADR-013](#adr-013--dtmf-in-band-via-pjmedia_tonegen-encapsulando-a-api-c) | DTMF in-band via `pjmedia_tonegen`, encapsulando a API C | Aceito |
| [ADR-014](#adr-014--logwriter-próprio-com-redaction-obrigatória) | `LogWriter` próprio com redaction obrigatória | Aceito |
| [ADR-015](#adr-015--exceções-apenas-na-borda-do-pjsua2-resultt-no-restante) | Exceções apenas na borda do PJSUA2; `Result<T>` no restante | Aceito |
| [ADR-016](#adr-016--testes-em-quatro-camadas-com-automação-só-do-que-é-puro) | Testes em quatro camadas, com automação só do que é puro | Aceito |
| [ADR-017](#adr-017--apenas-udp-e-windows-x64-no-mvp) | Apenas UDP e Windows x64 no MVP | Aceito |
| [ADR-018](#adr-018--escopo-funcional-fechado) | Escopo funcional fechado | Aceito |
| [ADR-019](#adr-019--descoberta-das-bibliotecas-do-pjproject-nos-diretórios-de-saída-reais) | Descoberta das bibliotecas do pjproject nos diretórios de saída reais | Aceito |
| [ADR-020](#adr-020--wmme-no-windows-desktop-e-build-limitado-às-bibliotecas-consumidas) | WMME no Windows Desktop e build limitado às bibliotecas consumidas | Aceito |

---

## ADR-001 — C++17 nativo com PJSIP/PJSUA2, em vez de fork do MicroSIP

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  O problema a resolver é DTMF não reconhecido por URAs externas em chamadas originadas de ramais
  Windows com MicroSIP, através de um PABX Issabel/Asterisk. O MicroSIP é open source e também usa
  PJSIP, o que torna o fork uma opção aparentemente barata. Porém seu código carrega uma GUI Win32
  extensa, funcionalidades fora do escopo (contatos, histórico, gravação) e um acoplamento entre UI e
  telefonia que dificulta isolar o comportamento de DTMF. O objetivo do projeto é **diagnóstico e
  controle explícito**, não paridade de funcionalidades.

- **Decisão:**
  Desenvolver uma aplicação nova em C++17 sobre PJSIP/PJSUA2 2.17, sem reutilizar código do MicroSIP.
  Usar a API C++ (PJSUA2) como interface principal, descendo para a API C do PJMEDIA/PJSUA apenas
  onde o PJSUA2 não oferece cobertura (caso do DTMF in-band — ver ADR-013).

- **Motivos:**
  - Superfície mínima de código torna o comportamento de DTMF **auditável linha a linha**;
  - PJSUA2 já encapsula o ciclo de vida de endpoint/conta/chamada com RAII e exceções, reduzindo
    código de infraestrutura;
  - Sem herdar decisões de arquitetura tomadas para outro objetivo;
  - C++ nativo mantém o binário único, sem runtime gerenciado, e depurável no Visual Studio;
  - PJSIP 2.17 expõe `Call::sendDtmf` com seleção de método e duração — exatamente o controle que falta.

- **Consequências:**
  - *Positivas:* base de código pequena; nenhuma dívida herdada; total controle do caminho de DTMF;
    licenciamento simples de auditar.
  - *Negativas:* é preciso reimplementar coisas que o MicroSIP já resolveu (enumeração de dispositivos,
    ciclo de vida, tratamento de erros); a curva de aprendizado do PJSUA2 recai sobre o projeto;
    prazo maior até a primeira chamada funcional.
  - *Mitigação:* escopo drasticamente reduzido (ADR-018) e plano em etapas pequenas com validação a
    cada passo.

- **Alternativas consideradas:**
  1. **Fork do MicroSIP** — rejeitada: o custo de entender e desacoplar o código existente supera o de
     escrever ~3.000 linhas focadas; o resultado seria mais difícil de justificar tecnicamente.
  2. **Patch/configuração no MicroSIP atual** — rejeitada: ele não expõe seleção explícita dos três
     métodos com duração ajustável em runtime, que é o requisito central.
  3. **Ferramenta de linha de comando com `pjsua` (o sample do PJSIP)** — rejeitada: útil para testes
     pontuais, mas não é base para um softphone e não permite a instrumentação/log que queremos.
  4. **Reimplementar SIP do zero** — rejeitada sem discussão: RTP, SDP, transações e codecs são anos
     de trabalho.

---

## ADR-002 — MVP somente console, sem interface gráfica

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  O produto final poderá ter interface gráfica, mas a primeira entrega é uma **prova técnica**: provar
  que o mesmo dígito enviado por três métodos distintos produz (ou não) reação na URA de destino.
  Qualquer camada de UI adiciona código, threads de janela, sincronização e problemas de empacotamento
  que não contribuem para essa prova.

- **Decisão:**
  MVP é um executável de console com um laço de comandos textuais (`call`, `dtmf`, `hangup`, `status`,
  `devices`, `dtmfcfg`, ...). Nenhum framework de UI.

- **Motivos:**
  - O console é *scriptável* e reprodutível — um roteiro de teste pode ser colado e repetido;
  - A saída textual é diretamente correlacionável com o log e com a captura de rede;
  - Zero código de UI significa que 100% dos bugs encontrados são de telefonia;
  - Depuração trivial: um único fluxo de execução visível.

- **Consequências:**
  - *Positivas:* entrega rápida; testes reprodutíveis; superfície de bug pequena.
  - *Negativas:* inutilizável por usuário final; a operação do teste de campo exige alguém confortável
    com terminal; a lógica de apresentação precisará ser refeita quando houver GUI.
  - *Mitigação:* separação clara entre `app/` (apresentação) e `sip/`, `audio/`, `dtmf/` (domínio) —
    uma GUI futura substitui apenas `app/`.

- **Alternativas consideradas:**
  1. **Qt** — descartada por instrução explícita e por peso (dependência grande, licenciamento a
     avaliar, build complexo no Windows).
  2. **Win32 nativo / ImGui** — rejeitada para o MVP: mesmo sendo leves, adicionam um laço de mensagens
     e uma thread de UI a sincronizar com as callbacks do PJSIP, sem ganho para a prova técnica.
  3. **Electron / interface web** — rejeitada por instrução explícita e por contradizer o requisito de
     solução nativa e depurável.

---

## ADR-003 — Licença GNU GPL v2

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  O PJSIP é distribuído sob licença dupla: GPL v2 (ou posterior) para uso open source, ou licença
  comercial. O POLPhone será um repositório público, sem licença comercial adquirida. Portanto o
  projeto está vinculado aos termos da versão GPL do PJSIP.

- **Decisão:**
  Licenciar o POLPhone sob **GNU GPL v2**, com o texto integral em `LICENSE` e cabeçalho de licença em
  cada arquivo-fonte. As dependências adicionais devem ser compatíveis (nlohmann/json e doctest são MIT,
  compatível com GPL).

- **Motivos:**
  - É a licença que a versão open source do PJSIP requer para trabalhos derivados/combinados;
  - GPL v2 (em vez de v3) alinha-se exatamente ao que o PJSIP oferece, evitando discussão sobre
    compatibilidade v2-only × v3;
  - Repositório público sem intenção de fechamento comercial — o copyleft não impõe custo prático.

- **Consequências:**
  - *Positivas:* conformidade clara; contribuições da comunidade sob os mesmos termos; nenhum custo de
    licenciamento.
  - *Negativas:* o binário distribuído obriga a disponibilizar o código-fonte correspondente; uso
    interno em produto proprietário fechado exigiria licença comercial do PJSIP;
    **combinar GPL v2 com OpenSSL é problemático** (a licença do OpenSSL 1.x tem cláusula de anúncio
    incompatível), o que reforça a decisão de não habilitar TLS no MVP (ADR-007).
  - *Ação decorrente:* o `README.md` deve declarar explicitamente a licença, a dependência do PJSIP e
    o link para o texto da GPL v2.

- **Alternativas consideradas:**
  1. **MIT/Apache-2.0** — inviável: incompatível com a obrigação de copyleft ao linkar o PJSIP GPL.
  2. **Licença comercial do PJSIP** — rejeitada: custo e desnecessária para um projeto público.
  3. **GPL v3** — rejeitada: sem benefício aqui e introduz atrito de compatibilidade com o "v2 or later"
     do PJSIP em cenários de combinação com outras dependências.

---

## ADR-004 — pjproject como git submodule fixado na tag 2.17

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  O PJSIP precisa ser compilado a partir do fonte no Windows (não há pacote binário oficial usável).
  A forma de trazer esse fonte para o projeto determina reprodutibilidade, tamanho do repositório e
  facilidade de atualização. Acompanhar a branch `master` foi descartado por instrução e por
  princípio: mudanças upstream não anunciadas quebrariam o build e, pior, poderiam alterar
  silenciosamente o comportamento de DTMF que estamos medindo.

- **Decisão:**
  `third_party/pjproject` é um **git submodule** apontando para `https://github.com/pjsip/pjproject.git`,
  com `HEAD` fixado na **tag `2.17`**. O SHA correspondente é registrado no `README.md`.
  `.gitmodules` **não** contém `branch =`. É proibido usar `git submodule update --remote`.

- **Motivos:**
  - Registra um SHA exato no histórico do POLPhone — reprodutibilidade verificável por qualquer pessoa;
  - Mantém o repositório pequeno e os diffs limpos (`git blame` não mistura código de terceiros);
  - Atualização de versão vira um commit de uma linha, revisável;
  - Após o clone inicial, o build funciona offline;
  - A separação física deixa óbvio o que é nosso e o que é upstream — relevante sob GPL.

- **Consequências:**
  - *Positivas:* reprodutibilidade; higiene do repositório; auditoria de origem trivial.
  - *Negativas:* `git clone` sem `--recurse-submodules` produz um diretório vazio e um erro de build
    confuso — a armadilha mais comum de submodules; aplicar um patch local no PJSIP exige um fork.
  - *Mitigação:* `scripts/verify-env.ps1` detecta o submodule vazio ou fora da tag e imprime o comando
    exato a executar; o `README.md` traz o comando de clone correto como primeira instrução.

- **Alternativas consideradas:**
  1. **Código vendorizado** (copiar o pjproject para dentro do repositório) — rejeitada: acrescenta
     dezenas de MB, polui `git blame` e diffs, e dificulta enxergar de qual versão upstream se trata.
     Seria a escolha certa apenas se precisássemos manter patches locais permanentes no PJSIP.
  2. **Download durante o build** (script baixa o tarball/zip da tag) — rejeitada: introduz dependência
     de rede em todo build limpo, quebra em ambiente isolado, e o artefato baixado não fica registrado
     no controle de versão. Aceitável apenas com verificação de hash e cache local — complexidade sem
     ganho frente ao submodule.
  3. **vcpkg / Conan** — rejeitada: o porte disponível pode não corresponder exatamente à 2.17 nem à
     nossa `config_site.h`, e adiciona um gerenciador de pacotes inteiro como dependência de build.
  4. **Acompanhar `master`** — rejeitada por instrução explícita e por risco de mudança silenciosa de
     comportamento de mídia/DTMF entre execuções do experimento.

---

## ADR-005 — CMake gerando solução do Visual Studio 2022

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  O alvo é Windows x64 com Visual Studio 2022. Duas formas de organizar o build do POLPhone:
  `.vcxproj`/`.sln` escritos à mão, ou CMake gerando esses arquivos. O pjproject, por sua vez, tem
  build próprio via MSBuild e não usa CMake no Windows — ou seja, haverá dois passos de build de
  qualquer forma.

- **Decisão:**
  O **pjproject** é compilado pela sua própria solução via MSBuild (`scripts/setup-pjproject.ps1`).
  O **POLPhone** usa CMake ≥ 3.21 com o gerador `Visual Studio 17 2022 -A x64`, consumindo os `.lib`
  produzidos através de `cmake/PJSIP.cmake`. O desenvolvedor abre `build/POLPhone.sln` no VS2022 e
  depura normalmente.

- **Motivos:**
  - O build fica scriptável e reproduzível (`build.ps1`, CI) sem abrir mão da experiência nativa de
    depuração no Visual Studio — a solução gerada é uma solução de verdade;
  - Configurar flags por configuração (`/MD` × `/MDd`, `/O2` × `/Od`, PDB em Release) é declarativo e
    difícil de errar, ao contrário de editar XML de `.vcxproj`;
  - Adicionar o alvo de testes (`polphone_tests`) é trivial;
  - Se um dia o projeto for portado, o CMake já está lá — sem que isso custe nada agora.

- **Consequências:**
  - *Positivas:* build de uma linha; configuração versionada e legível; solução VS gerada para depuração.
  - *Negativas:* uma ferramenta a mais no caminho; a solução em `build/` é gerada e **não** deve ser
    editada pelo VS (mudanças se perdem na regeneração); `cmake/PJSIP.cmake` precisa descobrir nomes de
    `.lib` que variam entre versões do pjproject.
  - *Mitigação:* `PJSIP.cmake` descobre as libs por GLOB e falha com mensagem acionável quando falta
    alguma; o `README.md` avisa que `build/` é gerado.

- **Alternativas consideradas:**
  1. **`.vcxproj` escrito à mão** — rejeitada: XML verboso, propenso a divergência entre Debug e
     Release, difícil de revisar em PR, e ruim para CI. Ganho seria evitar o CMake — insuficiente.
  2. **Adicionar os `.vcxproj` do pjproject à nossa solução com project references** — rejeitada:
     acopla nosso build ao formato de solução do submodule, exige retarget persistente (sujando o
     working tree do submodule) e torna o build completo muito mais lento.
  3. **CMake também para o pjproject** (portar o build) — rejeitada: esforço grande, fora do escopo, e
     divergiria do build oficialmente suportado pelo projeto upstream.
  4. **Ninja em vez do gerador VS** — rejeitada para o MVP: build mais rápido, mas perde a solução
     nativa de depuração, que é um requisito declarado.

---

## ADR-006 — Runtime C dinâmico (`/MD`) em todo o projeto

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  A incompatibilidade entre runtimes do MSVC (`/MT` estático × `/MD` dinâmico) é a causa mais frequente
  de falha de link ao integrar PJSIP no Windows (`LNK2038: mismatch detected for 'RuntimeLibrary'`).
  O pjproject oferece configurações para ambos (`Release` usa `/MT`, `Release-Dynamic` usa `/MD`).
  Um agravante correlato é `_ITERATOR_DEBUG_LEVEL`, que impede linkar binário Debug com libs Release.

- **Decisão:**
  Todo o projeto usa runtime **dinâmico**: pjproject compilado em `Debug-Dynamic|x64` e
  `Release-Dynamic|x64`; POLPhone com `CMAKE_MSVC_RUNTIME_LIBRARY = MultiThreadedDLL[Debug]`.
  `setup-pjproject.ps1` compila **as duas** configurações de uma vez, para impedir o cruzamento
  Debug↔Release.
  Correção factual verificada na T03: a expressão CMake que materializa esses dois valores é
  `MultiThreaded$<$<CONFIG:Debug>:Debug>DLL`, resultando em `MultiThreadedDebugDLL` no Debug e
  `MultiThreadedDLL` no Release.

- **Motivos:**
  - `/MD` é o padrão do MSVC e do CMake — menos atrito com qualquer dependência futura;
  - Evita a classe de bugs de múltiplos heaps do CRT (alocar em uma lib e liberar em outra);
  - Binário menor;
  - Compilar as duas configurações do pjproject de uma vez elimina o erro de `_ITERATOR_DEBUG_LEVEL`
    antes que ele aconteça.

- **Consequências:**
  - *Positivas:* integração previsível; mensagem de erro conhecida e documentada quando alguém desviar.
  - *Negativas:* o executável exige o **Visual C++ 2015-2022 Redistributable x64** na máquina de destino;
    binários Debug não são redistribuíveis (dependem de DLLs que só existem com o VS instalado).
  - *Ação decorrente:* testes de campo com a URA usam o binário **Release** com PDB; o `README.md`
    documenta a necessidade do Redistributable. Migrar para `/MT` no futuro é possível, desde que
    pjproject e POLPhone sejam recompilados **juntos**.

- **Alternativas consideradas:**
  1. **`/MT` (runtime estático)** — considerada seriamente: produziria um `.exe` autocontido, atraente
     para distribuir a estações Windows sem instalar nada. Rejeitada no MVP porque aumenta o risco de
     conflito de heap entre CRTs e porque a distribuição em massa não é objetivo desta fase.
     Reavaliar quando houver empacotamento para produção.
  2. **Misturar** (`/MT` no pjproject, `/MD` no app) — inviável: é exatamente o erro que se quer evitar.

---

## ADR-007 — `config_site.h` mínimo: sem vídeo, sem TLS e sem codecs compressivos

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  O `pjlib/include/pj/config_site.h` é obrigatório (o build falha sem ele) e é o ponto de customização
  da biblioteca. As escolhas feitas ali determinam tempo de compilação, dependências externas e —
  criticamente para este projeto — **quais codecs podem ser negociados**, o que afeta diretamente a
  viabilidade do DTMF in-band.

- **Decisão:**
  Manter um `cmake/config_site.h.in` versionado, copiado para o submodule pelo script de setup, com:
  `PJMEDIA_HAS_VIDEO 0`; `PJSIP_HAS_TLS_TRANSPORT 0` e `PJ_HAS_SSL_SOCK 0`; apenas G.711 e G.722
  habilitados (GSM, Speex, iLBC, G.722.1, Opus e L16 desativados); WMME habilitado (WASAPI disponível
  para comparação); `PJ_LOG_MAX_LEVEL 5` **também em Release**. Cada macro deve ser conferida contra os
  headers reais da tag 2.17 antes de ser adotada.

- **Motivos:**
  - **Codecs compressivos desativados removem uma variável do experimento**: se apenas G.711/G.722
    podem ser negociados, um in-band que falha não pode ser atribuído a transcodificação no primeiro
    salto — o problema estará necessariamente adiante, no PABX ou no tronco;
  - Sem TLS/OpenSSL: elimina uma dependência externa pesada no Windows **e** o conflito de licença
    GPL v2 + OpenSSL (ADR-003);
  - Sem vídeo: reduz drasticamente o tempo de compilação e a superfície de bug;
  - `PJ_LOG_MAX_LEVEL 5` em Release é essencial: o diagnóstico acontece em campo, com o binário Release.

- **Consequências:**
  - *Positivas:* build mais rápido; menos dependências; experimento com menos variáveis; menor superfície
    de ataque.
  - *Negativas:* se o PABX exigir G.729/Opus, a chamada falhará na negociação (nesse caso, habilitar o
    codec é uma decisão consciente, documentada em novo ADR); sem TLS não é possível testar SIPS/SRTP;
    o arquivo copiado para dentro do submodule fica no `.gitignore`, então precisa ser regenerado em
    todo clone novo (o script cuida disso).
  - *Ação decorrente:* `PJMEDIA_HAS_VIDEO 0` não elimina a necessidade de linkar `pjmedia-videodev` e
    `libyuv` (viram stubs) — documentado em BUILD-STRATEGY §5.1 e §8.

- **Alternativas consideradas:**
  1. **`config_site.h` vazio (defaults do PJSIP)** — rejeitada: traria vídeo, todos os codecs e a
     tentativa de TLS, aumentando build, dependências e ruído no experimento.
  2. **Habilitar todos os codecs para "máxima compatibilidade"** — rejeitada: contradiz o objetivo.
     Um in-band destruído por G.729 negociado silenciosamente seria diagnosticado como bug do POLPhone.
  3. **Habilitar TLS desde já** — adiada: não é requisito do MVP (ADR-017) e traz o problema de licença.

---

## ADR-008 — Configuração em JSON local ignorado pelo Git

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  O repositório é público. A aplicação precisa de credenciais SIP reais (usuário, senha, host do PABX)
  e será usada para discar números reais. Qualquer vazamento desses dados no histórico do Git é
  praticamente irreversível.

- **Decisão:**
  Toda a configuração vive em `config/polphone.config.json`, **listado no `.gitignore` desde o primeiro
  commit**. Um `config/polphone.config.example.json` versionado documenta a estrutura com placeholders
  `REPLACE_ME`. A senha nunca é impressa: `AppConfig::redactedDump()` mascara, e o `Redactor` garante
  que ela não apareça em nenhum log (ADR-014). Sem variáveis de ambiente, sem keystore, sem
  criptografia do arquivo no MVP.

- **Motivos:**
  - Um arquivo único e legível é trivial de editar, versionar localmente e comparar entre máquinas de
    teste — o experimento envolve trocar parâmetros com frequência;
  - JSON tem parser maduro de um único header (ADR-009), sem custo de dependência;
  - Ignorar no `.gitignore` desde o commit inicial é a defesa mais simples e mais efetiva;
  - Comentários no `.example` documentam faixas e semântica junto do dado.

- **Consequências:**
  - *Positivas:* zero segredo no repositório; onboarding de uma linha (`Copy-Item` do exemplo);
    configuração inteira auditável de relance.
  - *Negativas:* a senha fica **em claro no disco** da estação — aceitável para uma ferramenta de
    diagnóstico usada por técnicos, inaceitável para um produto distribuído; um usuário desatento pode
    renomear o arquivo e commitá-lo.
  - *Mitigação:* `.gitignore` cobre também `config/*.local.json`; a Etapa 01 inclui verificação por
    `git grep` de padrões de credencial; o `README.md` traz o aviso em destaque. Proteção real da
    senha (DPAPI/Credential Manager) fica para um ADR futuro, se o projeto virar produto.

- **Alternativas consideradas:**
  1. **Variáveis de ambiente** — rejeitada para o MVP: pior ergonomia para 20+ parâmetros; difícil de
     versionar localmente entre cenários de teste; some do histórico do shell de forma imprevisível.
  2. **INI/TOML** — rejeitada: JSON já tem parser sem custo no projeto e suporta a estrutura aninhada
     naturalmente.
  3. **Windows Credential Manager / DPAPI para a senha** — adiada: é a solução correta para produto,
     mas adiciona código e complica o teste em múltiplas máquinas nesta fase.
  4. **Argumentos de linha de comando** — rejeitada como fonte principal: senha em `argv` fica visível
     no gerenciador de tarefas e no histórico do shell.

---

## ADR-009 — Apenas três dependências: pjproject, nlohmann/json e doctest

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  Toda dependência adicional no Windows custa tempo de build, risco de incompatibilidade de runtime
  (ADR-006) e uma decisão de licença a auditar sob GPL (ADR-003).

- **Decisão:**
  Dependências permitidas: `pjproject` (submodule, ADR-004), `nlohmann/json.hpp` (header único,
  vendorizado, MIT) e `doctest.h` (header único, vendorizado, MIT, apenas no alvo de testes).
  Tudo o mais — logging, formatação, parsing de comandos, testes — é escrito à mão sobre a STL do C++17.

- **Motivos:**
  - Headers únicos vendorizados não têm build próprio, não têm problema de runtime e são triviais de
    auditar;
  - MIT é compatível com GPL v2;
  - As funcionalidades que seriam terceirizadas (logger, parser de linha de comando) são pequenas o
    bastante para escrever com qualidade em algumas centenas de linhas — e ficam sob nosso controle,
    o que importa para as regras de redaction (ADR-014).

- **Consequências:**
  - *Positivas:* build simples e rápido; auditoria de licença trivial; nenhum gerenciador de pacotes.
  - *Negativas:* escrevemos e mantemos logger, rotação de arquivo e parser de comandos; sem
    `{fmt}`/`std::format` (o MSVC do VS2022 tem `<format>`, mas o projeto se limita a C++17 —
    a formatação será manual ou via `std::ostringstream`).
  - *Nota:* se a formatação manual se mostrar penosa, elevar o padrão para C++20 e usar `<format>` é
    preferível a adicionar `{fmt}` — mas isso exige novo ADR.

- **Alternativas consideradas:**
  1. **spdlog** — rejeitada: excelente, mas o requisito de redaction obrigatória em toda linha
     (ADR-014) exige um caminho de escrita sob nosso controle, e a integração com `pj::LogWriter` seria
     apenas um adaptador de qualquer forma.
  2. **CLI11 / cxxopts** — rejeitada: o parser do console é pequeno e específico (verbos + flags), e
     precisa de testes próprios de qualquer maneira.
  3. **GoogleTest / Catch2 v3** — rejeitadas: exigem build próprio (não são header único), contra o
     princípio de build simples. doctest entrega o suficiente com um header.
  4. **vcpkg para tudo** — rejeitada junto com ADR-004.

---

## ADR-010 — Modelo de threads: `threadCnt = 1` com console na thread principal

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  O PJSUA2 pode operar de duas formas: com threads de trabalho próprias
  (`UaConfig::threadCnt >= 1`), em que as callbacks chegam nessas threads; ou com `threadCnt = 0`,
  em que a aplicação deve chamar `Endpoint::libHandleEvents()` periodicamente e todas as callbacks
  chegam na thread que faz o polling. O console, por sua vez, é naturalmente bloqueante
  (`std::getline`). Isso cria uma tensão entre determinismo e simplicidade.

- **Decisão:**
  `uaConfig.threadCnt = 1`. A thread principal chama `libCreate()` (ficando automaticamente registrada
  na biblioteca), executa o laço de comandos do console e é a única que destrói objetos. As callbacks
  chegam na thread de trabalho do PJSUA2 e apenas **publicam eventos** numa fila protegida por mutex,
  que a thread principal drena a cada iteração. Nenhuma callback imprime no console nem executa
  operação longa. Qualquer thread futura criada por nós deve chamar `libRegisterThread()`.

- **Motivos:**
  - É o modelo canônico dos exemplos do PJSUA2 — menos chance de esbarrar em comportamento não testado
    upstream;
  - Mantém o SIP responsivo (retransmissões, timers, ACK) mesmo enquanto o console está bloqueado
    esperando o usuário digitar — o que com `threadCnt = 0` seria fatal;
  - A regra "callbacks só publicam eventos" reduz a concorrência real a **uma** fila e **um** mutex de
    estado, com ordem de lock fixa;
  - A thread principal ser a que chama `libCreate()` elimina toda a categoria de bugs de "thread não
    registrada".

- **Consequências:**
  - *Positivas:* SIP nunca trava por causa da UI; modelo alinhado ao upstream; concorrência confinada a
    dois pontos bem definidos.
  - *Negativas:* existe concorrência real — estado compartilhado precisa de mutex, e um bug de
    sincronização é mais difícil de reproduzir que num modelo single-thread; a saída do console é
    assíncrona em relação aos comandos (um evento pode aparecer entre o comando e sua resposta).
  - *Mitigação:* ARCHITECTURE §6 fixa as regras (callbacks `noexcept`, curtas, sem I/O, ordem de lock,
    destruição diferida); `reap()` e drenagem de eventos acontecem só na thread principal.

- **Alternativas consideradas:**
  1. **`threadCnt = 0` com `libHandleEvents()` no laço principal** — tecnicamente o mais determinístico
     e o mais fácil de depurar (todas as callbacks na mesma thread, sem locks). Rejeitada porque o
     console bloqueante teria de ser substituído por polling não-bloqueante do `stdin` no Windows
     (`_kbhit`/`ReadConsoleInput` com timeout), e porque qualquer operação lenta na thread principal
     pararia o processamento de SIP. O ganho de determinismo não compensa o risco de stall no SIP.
     **Reavaliar** se surgirem bugs de concorrência difíceis de reproduzir.
  2. **`threadCnt >= 2`** — rejeitada: mais concorrência sem nenhum benefício para uma única chamada.
  3. **Thread dedicada de entrada + `threadCnt = 0`** — rejeitada: combina as desvantagens das duas
     (precisa de `libRegisterThread`, polling *e* sincronização).

---

## ADR-011 — Destruição diferida de objetos `Call`

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  Em PJSUA2, o objeto `pj::Call` é de propriedade da aplicação. O padrão mais divulgado é destruí-lo
  em `onCallState` quando o estado é `DISCONNECTED` — inclusive com `delete this`. Isso significa
  destruir um objeto de dentro de uma de suas próprias funções-membro, chamada pela biblioteca, com a
  possibilidade de outras callbacks (mídia, transação) ainda em voo para a mesma chamada. É uma fonte
  clássica de *use-after-free* intermitente no encerramento.

- **Decisão:**
  `SipCall` **nunca** se destrói. Em `onCallState(DISCONNECTED)`, a callback chama
  `CallRegistry::retire()`, que move o `unique_ptr` da chamada corrente para uma lista de espera
  (*graveyard*), sob mutex. A destruição efetiva ocorre em `CallRegistry::reap()`, chamado apenas pela
  thread principal, a cada iteração do laço de comandos e na sequência de encerramento.

- **Motivos:**
  - Elimina por construção a destruição reentrante e o *use-after-free* por callback em voo;
  - Concentra toda destruição numa única thread e num único ponto do código, fácil de instrumentar;
  - O custo é desprezível: a destruição atrasa alguns milissegundos, o que é irrelevante para o MVP;
  - Torna verificável um invariante simples: "nenhum `delete` de objeto PJSUA2 fora da thread principal".

- **Consequências:**
  - *Positivas:* encerramento estável; ciclo call/hangup repetível centenas de vezes sem crash;
    regra fácil de revisar em code review ("procure por `delete this`").
  - *Negativas:* objetos vivem um pouco além do necessário; se o laço principal estiver bloqueado
    esperando entrada do usuário, o *graveyard* pode acumular (irrelevante no MVP, com no máximo uma
    chamada por vez); exige disciplina — é uma convenção, não algo imposto pelo compilador.
  - *Mitigação:* a sequência de encerramento (ARCHITECTURE §5.3) chama `reap()` explicitamente antes de
    destruir a conta e o endpoint; o log de shutdown reporta quantos objetos foram coletados.

- **Alternativas consideradas:**
  1. **`delete this` em `onCallState(DISCONNECTED)`** — rejeitada pelos motivos acima, apesar de ser o
     padrão dos samples.
  2. **`shared_ptr` + `weak_ptr` para as chamadas** — rejeitada: resolveria o tempo de vida, mas a
     destruição ainda poderia ocorrer na thread de callback quando a última referência caísse ali,
     que é justamente o que se quer evitar.
  3. **Nunca destruir (pool de objetos reutilizados)** — rejeitada: complica mais do que resolve para
     uma única chamada simultânea.

---

## ADR-012 — Seleção explícita do método DTMF, sem fallback automático

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  Este é o requisito central do projeto. Softphones costumam implementar fallback: tentam RFC 4733 e,
  se `telephone-event` não foi negociado, enviam SIP INFO ou in-band silenciosamente. Esse
  comportamento é conveniente para o usuário final e **desastroso para diagnóstico** — foi
  provavelmente o que impediu identificar a causa do problema com a URA da GoDaddy até aqui: não se
  sabe o que realmente foi enviado.

- **Decisão:**
  O método DTMF é **sempre** explícito: vem de `dtmf.defaultMethod` na configuração, do comando
  `dtmfmode`, ou da flag `--method` da requisição. Se o método escolhido não puder ser executado, a
  operação **falha com erro traduzido** que nomeia a causa e sugere alternativas — nunca substitui o
  método. Uma requisição usa exatamente um método (invariante de DTMF-DESIGN §6). O método efetivo é
  exibido no prompt e registrado em toda linha de log de DTMF, com um `correlationId`.

- **Motivos:**
  - Sem isso, o experimento não tem validade: cada envio precisa ser atribuível a um método específico;
  - Erros explícitos são informação: "o peer não negociou telephone-event" é exatamente o dado que se
    procura, e um fallback silencioso o destruiria;
  - Evita duplicidade por construção — não existe caminho de código que envie o mesmo dígito duas vezes;
  - O prompt mostrando o método corrente elimina o erro humano de "achei que estava em in-band".

- **Consequências:**
  - *Positivas:* resultados do teste de campo são inequívocos; a matriz de DTMF-DESIGN §9.3 tem
    significado; nenhuma duplicação de dígito originada no softphone.
  - *Negativas:* pior experiência para um usuário final (que preferiria "só funcionar"); o operador
    precisa entender os três métodos; um método indisponível exige ação manual.
  - *Ação decorrente:* se o POLPhone evoluir para uso em produção, um modo "automático" pode ser
    adicionado — mas como opção explicitamente ativada, e registrando qual método foi de fato usado.
    Isso exigirá novo ADR.

- **Alternativas consideradas:**
  1. **Fallback automático em cascata** (RFC 4733 → INFO → in-band) — rejeitada: destrói a capacidade
     diagnóstica, que é o propósito do projeto.
  2. **Enviar por todos os métodos simultaneamente** ("um vai funcionar") — rejeitada enfaticamente:
     é a receita para dígitos duplicados na URA, que é um modo de falha pior que o original.
  3. **Método fixo em tempo de compilação** — rejeitada: exigiria três binários e impediria comparar os
     métodos dentro da mesma chamada, que é o roteiro de validação definido.

---

## ADR-013 — DTMF in-band via `pjmedia_tonegen`, encapsulando a API C

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  O PJSUA2 oferece `Call::sendDtmf` para RFC 4733 e SIP INFO, mas **não existe API de envio de DTMF
  in-band** — porque in-band não é sinalização, é áudio. Gerar o tom exige o `pjmedia_tonegen` do
  PJMEDIA (API C) e conectá-lo à *conference bridge* do PJSUA, misturando o tom no fluxo que vai
  para o remoto.

- **Decisão:**
  Implementar a classe `ToneGenerator` que encapsula `pjmedia_tonegen_create2`,
  `pjmedia_tonegen_play_digits`, `pjmedia_tonegen_is_busy` e `pjmedia_tonegen_stop`, com pool próprio,
  registro na conference bridge (via `AudioMedia::registerMediaPort2` se disponível na 2.17, senão via
  `pjsua_conf_add_port`) e conexão **ao slot da chamada, nunca ao dispositivo de reprodução**.
  A API C fica confinada a esse arquivo. As assinaturas exatas devem ser lidas nos headers da tag 2.17
  antes de codificar (checklist C3/C4/C5 em DTMF-DESIGN §10).

- **Motivos:**
  - Não há alternativa dentro do PJSUA2 — é a única forma suportada de gerar DTMF in-band;
  - O `pjmedia_tonegen` já implementa o mapa DTMF padrão e o controle de `on_msec`/`off_msec`/`volume`,
    exatamente os parâmetros que o requisito 13 exige;
  - Conectar ao slot da chamada (e não ao alto-falante) faz o tom não passar pelo microfone, evitando
    tanto o cancelamento de eco quanto a captura do próprio tom de volta;
  - Encapsular numa classe mantém o resto do código em PJSUA2 idiomático.

- **Consequências:**
  - *Positivas:* controle total sobre duração, intervalo e volume do tom; o método fica no mesmo nível
    de controle dos outros dois.
  - *Negativas:* é a parte mais frágil do projeto — gerenciamento manual de pool, porta e slot da
    bridge, com risco de vazamento de porta e de crash se a chamada terminar com o tonegen anexado;
    a reprodução é assíncrona (exige polling de `is_busy()`); o tom passa por reamostragem se o clock
    rate da bridge diferir do codec.
  - *Ação decorrente:* `audio.clockRate = 8000` como padrão para os testes in-band (evita reamostragem
    com G.711); `medConfig.noVad = true` obrigatório (VAD pode cortar o início do tom); teste de
    50 envios consecutivos verificando vazamento de slots (Etapa 15).

- **Alternativas consideradas:**
  1. **Gerar as senoides manualmente e injetar via porta de mídia customizada** — rejeitada:
     reimplementaria o `tonegen` com pior qualidade e mais código.
  2. **Reproduzir arquivos WAV pré-gravados com os tons** (`AudioMediaPlayer`) — considerada: seria
     mais simples de conectar à bridge, mas impede controlar duração e volume em runtime (exigiria um
     WAV por combinação), que é requisito explícito. Rejeitada.
  3. **Não implementar in-band** — rejeitada: é um dos três métodos exigidos, e é justamente o candidato
     mais provável para URAs legadas.

---

## ADR-014 — `LogWriter` próprio com redaction obrigatória

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  O diagnóstico exige o trace SIP completo (nível 4–5), que inclui cabeçalhos `Authorization`,
  URIs com números de telefone completos e, no caso de SIP INFO, o próprio dígito DTMF no corpo da
  mensagem. Ao mesmo tempo, o requisito 14 determina logs técnicos **sem expor senhas ou números
  completos**, e os logs podem ser anexados a chamados ou compartilhados para análise.

- **Decisão:**
  Implementar `PjLogWriter : public pj::LogWriter`, instalado em `EpConfig::logConfig.writer`, com
  `consoleLevel` do PJSIP zerado (todo o log passa pelo nosso caminho). Toda linha — nossa ou do PJSIP —
  atravessa o `Redactor` antes de ser escrita, aplicando as regras de ARCHITECTURE §10.3: senha nunca
  registrada, `response=`/`nonce=` mascarados, números externos com apenas os 4 últimos dígitos
  preservados, dígitos DTMF mascarados por padrão. `Call-ID`, tags, IPs e portas **não** são mascarados,
  por serem essenciais à correlação com a captura de rede. O writer é criado antes do endpoint e
  destruído depois dele.

- **Motivos:**
  - Redaction no ponto de escrita é o único lugar que garante cobertura de **todas** as origens,
    inclusive as linhas geradas internamente pelo PJSIP, que não controlamos;
  - Manter IPs e `Call-ID` legíveis é o que torna o log útil frente ao Wireshark — mascarar tudo
    inviabilizaria o diagnóstico;
  - `dtmf.logDigits = true` existe como escolha consciente do operador, com aviso explícito no início
    da sessão.

- **Consequências:**
  - *Positivas:* logs compartilháveis; requisito 14 atendido por construção; regras de mascaramento
    testáveis unitariamente (são funções puras).
  - *Negativas:* custo de CPU por linha em nível 5 (regex/varredura em cada linha do trace SIP);
    risco de mascarar demais e esconder informação útil; risco de uma regra nova vazar dado se o teste
    correspondente não for escrito.
  - *Mitigação:* testes unitários para cada regra, **incluindo os casos que não devem ser mascarados**;
    verificação no fim de cada etapa buscando padrões de segredo nos logs gerados.

- **Alternativas consideradas:**
  1. **Usar o log em arquivo nativo do PJSIP** (`logConfig.filename`) — rejeitada: escreve direto,
     sem passar por nenhum filtro nosso; a senha não apareceria (o digest não a transmite), mas
     números completos e `response=` sim.
  2. **Redigir depois, com script sobre o arquivo** — rejeitada: o dado sensível chega a existir em
     disco; um crash ou uma cópia apressada do arquivo o vaza.
  3. **Desligar o trace SIP** — rejeitada: sem trace não há diagnóstico, que é o propósito do projeto.

---

## ADR-015 — Exceções apenas na borda do PJSUA2; `Result<T>` no restante

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  A API do PJSUA2 reporta erros lançando `pj::Error`. Já o código do PJSIP que invoca nossas callbacks
  é **C**: uma exceção que atravesse essa fronteira é comportamento indefinido e, na prática,
  `std::terminate`. Misturar os dois estilos livremente pelo código levaria a caminhos de erro
  inconsistentes e a crashes difíceis de rastrear.

- **Decisão:**
  Exceções são capturadas na fronteira: toda chamada à API do PJSUA2 é envolvida por `PJ_TRY`, que
  converte `pj::Error` em `Result<T>`; e toda callback do PJSUA2 é declarada `noexcept` com
  `try/catch` para `pj::Error`, `std::exception` e `(...)`. Dentro do código de aplicação
  (`app/`, `config/`, `dtmf/`, `logging/`, `util/`), o fluxo de erro é por `Result<T>` — sem exceções.

- **Motivos:**
  - Elimina a categoria de crash por exceção atravessando código C;
  - Torna os caminhos de erro visíveis no tipo de retorno das funções de domínio, o que combina com o
    requisito de mensagens de erro traduzidas e acionáveis;
  - Concentra o conhecimento sobre `pj::Error` (status, `title`, `reason`, arquivo/linha) num único
    utilitário, `PjErrors`.

- **Consequências:**
  - *Positivas:* erros previsíveis; sem crash silencioso em callback; mensagens de erro uniformes.
  - *Negativas:* verbosidade — cada chamada ao PJSUA2 ganha um envoltório; `Result<T>` escrito à mão
    (C++17 não tem `std::expected`); risco de alguém esquecer um `try/catch` numa callback nova.
  - *Mitigação:* auditoria explícita de todas as callbacks na Etapa 17; a regra está na lista de
    regras permanentes para o Codex (IMPLEMENTATION_PLAN §5).

- **Alternativas consideradas:**
  1. **Exceções em todo o código** — rejeitada: não resolve a fronteira C e torna mais fácil esquecer
     o `catch` na callback.
  2. **Códigos de retorno `pj_status_t` puros** — rejeitada: perde a mensagem contextual e obrigaria a
     traduzir status em todo lugar.
  3. **C++23 `std::expected`** — indisponível sob a decisão de C++17 (ADR-009).

---

## ADR-016 — Testes em quatro camadas, com automação só do que é puro

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  O MVP é um aplicativo de rede e áudio cujo comportamento crítico só se manifesta contra um PABX real
  e uma URA externa. Automatizar isso exigiria simular SIP, RTP e dispositivos de áudio — um esforço
  desproporcional ao tamanho do projeto e que testaria o simulador, não a realidade.

- **Decisão:**
  Quatro camadas (IMPLEMENTATION_PLAN §3): (1) testes unitários com doctest para **toda** lógica pura —
  parser de comandos, carregamento e validação de configuração, redaction, plano de DTMF, utilitários;
  (2) teste de fumaça automatizável `--selftest`, executando o ciclo de vida completo do PJSUA2 sem rede;
  (3) roteiro manual reprodutível de integração contra o PABX de laboratório (14 casos numerados);
  (4) validação de campo instrumentada contra a URA externa, com matriz de resultados.
  Sem mocks do PJSIP.

- **Motivos:**
  - As funções puras concentram a lógica mais suscetível a erro sutil (mascaramento, faixas de
    validação, parsing) e custam quase nada para testar;
  - `--selftest` captura a maior parte dos bugs de ciclo de vida, que são os mais perigosos, e roda em CI;
  - O roteiro manual numerado é reprodutível por qualquer pessoa e serve como critério de aceitação;
  - Mockar o PJSIP validaria nossas suposições sobre o PJSIP — exatamente o que **não** queremos testar.

- **Consequências:**
  - *Positivas:* CI útil e rápido; nenhum tempo gasto construindo infraestrutura de teste que não
    responde à pergunta do projeto; critérios de aceitação objetivos.
  - *Negativas:* a maior parte da validação depende de execução manual e de um PABX de laboratório;
    regressões em código de integração só aparecem quando alguém roda o roteiro; sem cobertura
    automatizada do caminho de áudio.
  - *Mitigação:* o roteiro está escrito com comandos exatos e resultados esperados
    (IMPLEMENTATION_PLAN §3.3), incluindo dialplan de teste; `--selftest` repetido 10× cobre o ciclo
    de vida; testes de estresse de shutdown na Etapa 17.

- **Alternativas consideradas:**
  1. **Mockar o PJSUA2 atrás de interfaces** — rejeitada: dobraria o tamanho do código, e o valor do
     teste seria baixo (verificaria que chamamos a API que achamos que deveríamos chamar).
  2. **SIPp para testes automatizados de integração** — considerada e adiada: útil para regressão de
     sinalização, mas não valida áudio nem DTMF in-band, e o custo de montar o cenário não se paga no MVP.
  3. **Sem testes automatizados** — rejeitada: as funções puras são baratas demais para deixar sem
     cobertura, e o `Redactor` sem teste é um risco de segurança direto.

---

## ADR-017 — Apenas UDP e Windows x64 no MVP

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  O ambiente de destino é conhecido e homogêneo: estações Windows x64 na mesma rede que um PABX
  Issabel/Asterisk, comunicando por SIP sobre UDP na porta 5060. TCP, TLS, SRTP, IPv6 e outras
  plataformas não fazem parte do cenário do problema a diagnosticar.

- **Decisão:**
  MVP suporta exclusivamente transporte **SIP sobre UDP** e plataforma **Windows x64**. Sem TCP, sem
  TLS/SIPS, sem SRTP, sem STUN/TURN/ICE, sem suporte a múltiplas contas ou múltiplas chamadas
  simultâneas. O campo `network.transport` existe na configuração aceitando apenas `"udp"`, para que a
  extensão futura não quebre o formato do arquivo.

- **Motivos:**
  - É o transporte efetivamente em uso no ambiente do problema — testar outra coisa mudaria o cenário;
  - UDP mantém a captura de rede legível em texto claro, essencial para o diagnóstico;
  - Sem TLS não há dependência de OpenSSL, o que remove uma dependência pesada no Windows e o conflito
    de licença com a GPL v2 (ADR-003, ADR-007);
  - Sem NAT traversal: o PABX está na LAN; STUN/ICE só adicionariam variáveis ao experimento.

- **Consequências:**
  - *Positivas:* superfície mínima; captura de rede legível; build sem dependências criptográficas;
    experimento com menos variáveis.
  - *Negativas:* inutilizável para cenários com PABX na internet exigindo TLS/SRTP; não funciona
    através de NAT sem configuração adicional no PABX; mensagens SIP grandes podem sofrer fragmentação
    UDP (mitigável ativando TCP no futuro).
  - *Ação decorrente:* a arquitetura mantém a criação do transporte isolada em `SipEndpoint`, de modo
    que adicionar TCP/TLS seja localizado — mas isso será um novo ADR, com reavaliação de
    `config_site.h` e de licença.

- **Alternativas consideradas:**
  1. **UDP + TCP desde o início** — rejeitada: sem demanda e adiciona um caminho de código a testar.
  2. **TLS/SRTP no MVP** — rejeitada: dependência de OpenSSL, conflito de licença, e a captura de rede
     ficaria cifrada, prejudicando exatamente o diagnóstico que motiva o projeto.
  3. **Multiplataforma desde o início** — rejeitada: o problema é em estações Windows; portar exigiria
     backend de áudio adicional e dobraria a matriz de teste.

---

## ADR-018 — Escopo funcional fechado

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  Softphones acumulam funcionalidades naturalmente: contatos, histórico, gravação, transferência,
  conferência, vídeo, presença, mensagens. Cada uma parece barata isoladamente e todas competem com o
  único objetivo desta entrega, que é responder a uma pergunta técnica específica sobre DTMF.

- **Decisão:**
  O escopo do MVP é a lista de 15 requisitos declarada no plano, e nada além. Ficam explicitamente
  **fora**: contatos, histórico de chamadas, gravação, transferência, conferência, vídeo, presença,
  mensagens instantâneas, múltiplas contas, múltiplas chamadas simultâneas, hold/retomada, tons de
  chamada, interface gráfica, instalador e atualização automática. Chamada entrante é tratada apenas
  no mínimo necessário para não deixar um INVITE sem resposta — não é funcionalidade.
  Qualquer inclusão exige um novo ADR.

- **Motivos:**
  - Cada funcionalidade extra adiciona código, estado e caminhos de erro que podem mascarar o
    comportamento de DTMF sob investigação;
  - Um escopo declarado e curto é a defesa mais eficaz contra deriva de escopo em um projeto conduzido
    em etapas por um agente de codificação;
  - Tempo até a primeira medição de campo é o que importa nesta fase.

- **Consequências:**
  - *Positivas:* MVP alcançável em poucas etapas; base de código pequena e auditável; foco total na
    prova técnica.
  - *Negativas:* o resultado não é utilizável como softphone de uso diário; a evolução para produto
    exigirá trabalho adicional significativo, possivelmente revisitando decisões (notadamente ADR-002 e
    ADR-010).
  - *Ação decorrente:* a lista de não-funcionalidades vai no `README.md`, para alinhar expectativa de
    quem encontrar o repositório público.

- **Alternativas consideradas:**
  1. **Incluir histórico e contatos "porque é barato"** — rejeitada: nenhum contribui para a validação,
     e ambos introduzem persistência em disco, com implicações de privacidade (números reais).
  2. **Incluir gravação de chamada para análise do DTMF** — considerada com mérito real (facilitaria
     a inspeção espectral do in-band). Rejeitada porque a captura RTP no Wireshark já resolve isso,
     sem código adicional e sem armazenar áudio com dados reais.
  3. **Incluir transferência e conferência para paridade com o MicroSIP** — rejeitada: paridade não é
     objetivo desta fase.

---

## ADR-019 — Descoberta das bibliotecas do pjproject nos diretórios de saída reais

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  A inspeção dos projetos oficiais da tag 2.17 mostrou que cada `.vcxproj` grava sua biblioteca em
  um diretório `lib` relativo ao componente: por exemplo, `pjsip/lib`, `pjmedia/lib`, `pjlib/lib`,
  `pjlib-util/lib`, `pjnath/lib` e `third_party/lib`. A documentação inicial do POLPhone pressupunha
  um único `third_party/pjproject/lib`, o que não corresponde aos elementos `OutputFile` dos projetos
  oficiais nem à documentação upstream, que descreve diretórios de biblioteca por projeto.

- **Decisão:**
  `setup-pjproject.ps1` e `cmake/PJSIP.cmake` descobrem recursivamente arquivos `.lib` somente em
  diretórios cujo nome é `lib`, sob `third_party/pjproject`, filtrando configuração e validando a
  lista fechada de nomes-base de BUILD-STRATEGY §5.1. Nenhum nome completo com sufixo de CPU,
  plataforma, versão do Visual Studio ou configuração é codificado. Nenhuma biblioteca é copiada ou
  movida, e nenhum arquivo do pjproject é alterado.

- **Motivos:**
  - Reflete diretamente os `OutputFile` da tag fixada, sem presumir um layout inexistente;
  - Preserva os projetos oficiais e mantém o submodule limpo;
  - Continua validando o conjunto exato e a configuração, evitando selecionar uma `.lib` de Win32,
    Debug/Release incompatível ou de outro toolset;
  - Tolera o sufixo de arquivo gerado pelo pjproject sem fixá-lo no POLPhone.

- **Consequências:**
  - *Positivas:* o build consome os artefatos onde o upstream realmente os produz; nenhuma etapa de
    cópia cria uma segunda fonte de verdade.
  - *Negativas:* a busca recursiva exige rejeitar duplicidades explicitamente; o comando de validação
    que enumera apenas `third_party/pjproject/lib/*.lib` deve ser substituído por uma busca recursiva.
  - *Ação decorrente:* ambos os scripts falham se faltar uma biblioteca ou se houver mais de uma
    candidata para o mesmo nome-base e configuração.

- **Alternativas consideradas:**
  1. **Copiar todas as bibliotecas para um `lib/` na raiz do submodule** — rejeitada: cria artefatos em
     layout não produzido pelo upstream e exige sincronização/limpeza adicional.
  2. **Codificar os caminhos por componente** — rejeitada: acopla o POLPhone a detalhes que podem
     variar em futura atualização do pjproject.
  3. **Modificar `OutDir`/`OutputFile` nos `.vcxproj`** — rejeitada: sujaria o submodule e violaria a
     decisão de usar os projetos oficiais sem patches locais.

---

## ADR-020 — WMME no Windows Desktop e build limitado às bibliotecas consumidas

- **Status:** Aceito — 2026-07-31

- **Contexto:**
  A primeira compilação efetiva da solução oficial `pjproject-vs14.sln` da tag 2.17, em
  `Debug-Dynamic|x64`, encontrou dois problemas nos alvos que não são consumidos pelo POLPhone. O
  `config_site.h` habilitava `PJMEDIA_AUDIO_DEV_HAS_WASAPI=1`, mas o projeto oficial
  `pjmedia_audiodev.vcxproj` inclui `wasapi_dev.cpp` somente quando
  `$(API_Family) != WinDesktop`; no build Desktop a biblioteca passou a referenciar
  `pjmedia_wasapi_factory` sem compilar sua implementação. Além disso, o alvo padrão da solução
  compila executáveis, testes, samples, wrappers UWP e bindings que não fazem parte do POLPhone;
  alguns falham pelo mapeamento próprio de runtime ou por diferenças de caixa em caminhos quando o
  MSBuild acessa o repositório pelo compartilhamento do WSL. As 21 bibliotecas estáticas requeridas
  por BUILD-STRATEGY §5.1 foram produzidas antes dessas falhas.

- **Decisão:**
  No alvo Windows Desktop do MVP, manter `PJMEDIA_AUDIO_DEV_HAS_WMME=1` e definir
  `PJMEDIA_AUDIO_DEV_HAS_WASAPI=0`. O áudio do Windows continua habilitado pelo WMME, que já era o
  backend padrão escolhido. `setup-pjproject.ps1` continua invocando exclusivamente a solução
  oficial, mas passa `/t:` com a lista fechada dos 21 projetos de biblioteca consumidos pelo
  POLPhone, nas configurações `Debug-Dynamic|x64` e `Release-Dynamic|x64`. Executáveis, testes,
  samples, bindings e projetos UWP do pjproject não fazem parte do alvo de preparação.

- **Motivos:**
  - Evita uma combinação de macro/projeto que a própria tag 2.17 não oferece para WinDesktop;
  - Preserva áudio nativo do Windows através do backend WMME previsto desde o início;
  - Faz o sucesso do script corresponder exatamente aos artefatos que o POLPhone vincula;
  - Não altera nenhum arquivo do submodule e não mascara dependências: a lista fechada de 21
    bibliotecas continua validada após cada build;
  - Mantém o uso da solução oficial do Visual Studio e não introduz o CMake experimental do pjproject.

- **Consequências:**
  - *Positivas:* build reproduzível também a partir do WSL; ausência do símbolo WASAPI não resolvido;
    falhas de utilitários opcionais não bloqueiam bibliotecas válidas; submodule permanece limpo.
  - *Negativas:* WASAPI não fica disponível para comparação no MVP; o script precisa manter a lista
    de projetos alinhada à lista de bibliotecas; o build não valida executáveis/tests upstream.
  - *Ação decorrente:* qualquer reativação de WASAPI exige confirmar um projeto oficial WinDesktop
    que compile `wasapi_dev.cpp`, ou uma versão posterior do pjproject, e deve ser tratada como nova
    decisão arquitetural.

- **Alternativas consideradas:**
  1. **Editar `pjmedia_audiodev.vcxproj` para incluir `wasapi_dev.cpp` em WinDesktop** — rejeitada:
     modifica diretamente o submodule e cria um patch local não previsto.
  2. **Manter o alvo padrão da solução e ignorar seu exit code** — rejeitada: violaria a regra de não
     presumir sucesso e permitiria falhas reais nas bibliotecas passarem despercebidas.
  3. **Migrar o build do pjproject para CMake** — rejeitada: suporte experimental e fora da estratégia.
