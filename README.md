# POLPhone

POLPhone é uma prova técnica de softphone SIP para Windows x64, escrita em C++17 sobre PJSIP/PJSUA2 2.17. O MVP é operado por console e existe para comparar, de forma explícita e auditável, métodos de DTMF em chamadas SIP.

Neste estágio, o repositório contém apenas a infraestrutura inicial de build e um executável mínimo de validação do PJSUA2. Não há conta SIP, transporte, chamada, áudio nem DTMF implementados.

## Fora do escopo

O MVP não inclui interface gráfica, contatos, histórico, gravação, transferência, conferência, vídeo, presença, mensagens, TLS/SRTP, STUN/TURN/ICE, múltiplas contas, múltiplas chamadas, instalador ou atualização automática.

## Pré-requisitos

- Windows 10/11 x64;
- Visual Studio 2022 17.8+ com o workload **Desenvolvimento para desktop com C++**;
- MSVC v143 e Windows SDK 10.0.22621.0 ou compatível (mínimo 10.0.19041.0);
- CMake 3.21+;
- Git 2.30+ com suporte a submodules;
- PowerShell 5.1 ou 7.x.

O build Release usa o runtime `/MD` e, em outra máquina, requer o **Microsoft Visual C++ Redistributable 2015–2022 x64**. Builds Debug usam `/MDd` e não são redistribuíveis.

## Obter o código

```powershell
git clone --recurse-submodules <URL_DO_REPOSITORIO> POLPhone
cd POLPhone
```

Se o clone já foi realizado sem os submodules:

```powershell
git submodule update --init --recursive
```

## pjproject fixado

- Repositório oficial: `https://github.com/pjsip/pjproject.git`
- Versão: tag `2.17`
- Commit: `5a457451fa2712ba18e12b01738e8ff3af2b26fd`
- Integração: Git submodule em `third_party/pjproject`
- Licença upstream: GPL-2.0-or-later ou licença comercial, conforme os termos do pjproject

O submodule nunca acompanha `master`. Para reinicializá-lo no commit registrado pelo POLPhone, use somente:

```powershell
git submodule update --init --recursive
```

Uma atualização futura exige decisão arquitetural e alteração explícita do gitlink; não use `git submodule update --remote`.

## Build no Windows x64

Execute a partir da raiz do repositório:

```powershell
.\scripts\verify-env.ps1
.\scripts\setup-pjproject.ps1 -Config Both
Copy-Item config\polphone.config.example.json config\polphone.config.json
.\scripts\build.ps1 -Config Debug
.\scripts\build.ps1 -Config Release
.\build\Release\polphone.exe --version
.\build\Release\polphone.exe --selftest
```

O pjproject é compilado por sua solução oficial `pjproject-vs14.sln` com MSBuild, configurações `Debug-Dynamic|x64` e `Release-Dynamic|x64`, toolset v143. O CMake gera apenas a solução do POLPhone; o sistema CMake experimental do pjproject não é utilizado.

A solução gerada `build/POLPhone.sln` é descartável. Faça alterações nos arquivos CMake, não nos projetos gerados.

## Configuração local e dados sensíveis

`config/polphone.config.json` é local e ignorado pelo Git. Copie o exemplo e substitua os marcadores apenas na sua máquina. Nunca versione credenciais, logs, dumps ou capturas SIP/RTP.

## Documentação

Arquitetura, ordem de implementação e decisões estão em `docs/`. Esses documentos são normativos para o projeto.

## Licença

POLPhone é distribuído sob a GNU General Public License, versão 2. Consulte `LICENSE`. As dependências mantêm suas próprias licenças e avisos.

## Dependências vendorizadas

- nlohmann/json `v3.12.0`, commit `55f93686c01528224f448c19128836e7df245f72` (MIT), em `third_party/nlohmann`;
- doctest `v2.4.12`, commit `1da23a3e8119ec5cce4f9388e91b065e20bf06f5` (MIT), em `third_party/doctest`.

Os headers e textos de licença são versionados; o build não baixa dependências.
