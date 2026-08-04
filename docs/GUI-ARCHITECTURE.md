# Arquitetura da interface gráfica

## Escopo

A primeira GUI do POLPhone é uma aplicação Windows nativa, unpackaged, em WinUI 3 e C++/WinRT.
Ela preserva a aplicação de console e não acessa objetos PJSIP. O modo de demonstração existe para
validar estados, comandos e transições visuais sem PABX, rede ou credenciais.

## Componentes

```text
polphone_cli.exe ── ConsoleUi ─────┐
                                   ├── Application ── SIP/áudio/DTMF/PJSIP
polphone.exe ── MainWindow ──┐     │
                             └── PolPhoneController ── RealTelephonyBackend ─┘
                                      │
                                      └── MockTelephonyBackend (demo, sem PJSIP/rede)
```

O núcleo lógico está dividido em:

- `config`: modelo JSON, carregamento, validação e gravação atômica usada pela GUI;
- `logging`: logger e sanitização já existentes;
- `sip`, `audio` e `dtmf`: adaptadores PJSIP existentes;
- `app/Application`: dono do ciclo de vida real, sem dependência de `ConsoleUi`;
- `core/TelephonyBackend`: contrato de operações de telefonia;
- `core/RealTelephonyBackend`: converte `ApplicationStatus` em `TelephonySnapshot`;
- `core/MockTelephonyBackend`: máquina de estados determinística e independente de PJSIP;
- `core/PolPhoneController`: fila assíncrona e fachada pública;
- `core/Presentation`: validação de destino, disponibilidade de comandos, máscara, cronômetro,
  proteção contra comandos duplicados e Modo URA;
- `gui`: composição dos controles WinUI, dispatcher e recursos visuais.

O alvo CMake `polphone_core` agrega o motor para consumidores CMake. O alvo compartilhado
`polphone_core_runtime.dll` fornece a fronteira usada pelo projeto MSBuild da GUI. A CLI liga as
mesmas bibliotecas estáticas e mantém todos os comandos e códigos de saída.

## Fachada e snapshots

`PolPhoneController` oferece inicialização, shutdown, registro, chamada, atendimento, rejeição,
desligamento, DTMF, mudo e dispositivos. Cada operação é enfileirada em uma thread de trabalho e
retorna `future<Result<void>>`. A interface permanece livre para processar entrada e desenho.

O snapshot contém estado do aplicativo, registro, chamada e mídia; destino mascarado; duração;
codec; mudo; DTMF; dispositivos; erros amigável/técnico e logs sanitizados. Callbacks PJSIP atualizam
somente o estado do motor. A GUI recebe uma cópia do snapshot e usa `DispatcherQueue::TryEnqueue`
para voltar à thread visual.

As proteções de ciclo de vida são:

- a janela marca `closing` antes de remover o callback e pedir shutdown;
- trabalhos destacados consultam o token compartilhado antes de tocar a interface;
- o controlador destrói a thread antes do backend;
- o backend real delega ao shutdown ordenado de `Application`;
- chamadas e conta são destruídas antes de `Endpoint::libDestroy`;
- os guards de apresentação recusam clique duplo em chamar/desligar e DTMF concorrente.

## Modo de demonstração

`polphone.exe --demo` ou a variável de desenvolvimento `POLPHONE_DEMO=1` seleciona o mock antes de
criar o controlador. O mock não inclui headers PJSIP, não abre rede e avança por eventos agendados
recebidos por `tick`; não há `sleep` na thread visual.

O fluxo normal simula inicialização, registro, chamada de saída, chamando, conectando, chamada
confirmada, áudio, cronômetro, DTMF, mudo e desligamento. Botões próprios disparam chamada recebida,
falha de registro, falha de chamada e perda de conexão. A chamada recebida pode ser atendida ou
rejeitada pelos mesmos comandos públicos. Somente uma chamada existe por vez.

## Apresentação e acessibilidade

Todas as cores estão em `gui/Theme.h`. `Primary` representa exatamente `#0a3b68`; superfícies,
bordas, texto e cores semânticas derivadas também são centralizadas. Não há gradientes nem tema
escuro padrão. Estado nunca depende apenas de cor: registro, chamada, mídia, mudo e Modo URA têm
texto e controles explícitos.

O Modo URA seleciona inicialmente In-band, exibe “Modo URA ativo” e restaura o método anterior ao
ser desativado. Ele não persiste a configuração sem confirmação e jamais envia um dígito por dois
métodos. “Automático” preserva apenas o comportamento já definido no motor; não tenta inferir se a
URA reconheceu o tom.

## Configuração e diagnóstico

A tela de configurações reúne URI do servidor/proxy, registrar, ID URI, usuário, senha, domínio,
UDP, registro automático, dispositivos, DTMF e log. O arquivo só é substituído depois de passar pelo
`ConfigValidator`; gravação inválida preserva o anterior. A senha é um `PasswordBox`, é limpa após
salvar e não integra mensagens de erro.

O diagnóstico mostra apenas snapshots e logs previamente sanitizados. Ele não expõe senha,
Authorization, nonce, números completos ou cabeçalhos SIP brutos; a ação de copiar usa exatamente o
texto já filtrado.

## Build

Pré-requisitos adicionais à CLI:

- Visual Studio 2022 17.8+;
- componente `Microsoft.VisualStudio.ComponentGroup.WindowsAppDevelopment.VC.BuildTools` (“C++ WinUI app
  development tools”);
- acesso ao NuGet para restaurar `Microsoft.WindowsAppSDK` 1.6.250205002 e
  `Microsoft.Windows.CppWinRT` 2.0.240405.15.

```powershell
.\scripts\verify-env.ps1 -Gui
.\scripts\build-gui.ps1 -Config Debug
.\scripts\build-gui.ps1 -Config Release
.\scripts\run-demo.ps1 -Config Debug
```

Quando o repositório está em WSL/UNC, o script usa uma unidade temporária criada por `pushd`, pois
`mt.exe` não aceita o prefixo estendido de um manifesto em UNC. Nenhum mapeamento permanece após o
build.

## Limitações e validação futura

- MSIX e implantação Intune ficam para uma etapa futura;
- a composição visual ainda precisa de inspeção manual em diferentes DPI, resoluções e tecnologias
  assistivas;
- a GUI aceita uma conta e uma chamada, acompanhando o motor atual;
- testes automatizados cobrem apresentação e mock, não pixels nem automação WinUI;
- nenhuma chamada, mídia ou variante DTMF foi validada contra PABX/URA por esta etapa.

Quando houver autorização para ambiente real, seguir `FIELD-TEST-GUIDE.md`: primeiro validar
registro/chamada/áudio em ramal controlado; depois executar a matriz RFC 4733, SIP INFO e In-band,
com captura e observação simultânea do PABX/tronco. O modo demo não substitui essa evidência.
