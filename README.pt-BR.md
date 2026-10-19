# 🏰 ICO — Port para PC (Recompilação Estática)

[English](README.md) · **Português (Brasil)**

> **Status — v0.1.0 (alfa):** inicia até o menu principal, roda a cinemática
> de abertura e chega ao gameplay no Windows a ~48–60 fps nas cenas 3D (PC de
> desenvolvimento). Espere bugs. Veja [Estado atual](#-estado-atual) e
> [Problemas conhecidos](#-problemas-conhecidos).

Um build nativo de Windows do **ICO** (PS3, *ICO & Shadow of the Colossus
Collection*), feito recompilando estaticamente o código PowerPC (PPU) e SPU do
jogo para C/C++ com o [ps3recomp](https://github.com/sp00nznet/ps3recomp).

**Nenhum arquivo do jogo está incluído.** Você seleciona o seu próprio dump do
disco e o instalador o recompila como jogo de PC **na sua máquina**.

---

## 📺 O que vem no pacote

| Componente | O que é |
|---|---|
| **`ICO.PC.Port.Setup.exe`** | O instalador ([Releases](../../releases)). Um único exe: você seleciona o dump e ele verifica, decripta (pelo seu RPCS3), recompila e instala. |
| `pipeline/` | `ico_pipeline.py`, as etapas de recompilação que o instalador roda, e `ico_profile.json`, com os hashes testados e os pontos de corte do código. |
| `port/` | O runner específico do ICO (`main.cpp`), os overrides HLE (`stubs.cpp`) e o projeto CMake. |
| `external/ps3recomp` | Submódulo: o ps3recomp, no branch com as correções de runtime deste port. |
| `installer/` | O código do instalador (C#, .NET, WinForms). |
| `docs/` | [Decriptação](docs/DECRYPTING.md), [configurações](docs/ENVIRONMENT.md), [build a partir do código](docs/BUILDING.md) (em inglês). |

---

## 🚦 Estado atual

| Área | Estado | Observações |
|---|---|---|
| Boot → menu principal | ✅ Funciona | Fibers, tasksets SPURS v2, LFQueue e overlays SPU estáveis. |
| Cinemática de abertura | ✅ Funciona | Pós-processamento completo: névoa por profundidade, sépia, bloom, letterbox. |
| Gameplay | 🟡 Alcançado | Sessões longas ainda não foram testadas. |
| Gráficos (D3D12) | 🟡 Bom | Sem bugs de geometria conhecidos; sem anti-aliasing (o MLAA é pulado). |
| Áudio | 🟡 Quase todo | Música e efeitos tocam; pode haver falhas curtas no início de uma faixa. |
| Controle | 🟡 Funciona nos menus | Controles XInput (compatíveis com Xbox). O controle do personagem no gameplay ainda precisa ser confirmado. |
| Teclado | 🟡 Básico | Mapa fixo quando não há controle conectado (veja [Controles](#-controles)). Teclado + mouse completos estão planejados. |
| Desempenho | 🟢 ~48–60 fps | Cenas 3D no PC de desenvolvimento, com o build AVX2. |
| Saves | ⚪ Não testado | Ficam em `<instalação>\vfs`. |
| Menu de gráficos | ❌ Ainda não | Janela fixa em 1280×720. Planejado: resolução, vsync, filtro anisotrópico, limite de FPS, upscaling. |

Legenda: ✅ funciona · 🟢 bom · 🟡 parcial · ⚪ não testado · ❌ falta

---

## ⬇️ Instalação

### Requisitos

| Você precisa de | Para quê | Observações |
|---|---|---|
| **Windows 10/11 x64** | | |
| **GPU com Direct3D 12** | Renderização | |
| **CPU com SSE4.1** | | Com AVX2 + FMA (Intel Haswell / AMD Zen ou mais novo) o build é mais rápido. |
| **O seu dump do disco do ICO** | Dados do jogo | *ICO & Shadow of the Colossus Collection*, **EUA (BCUS98259)**, a pasta `PS3_GAME`. |
| **[RPCS3](https://rpcs3.net) com o firmware do PS3 instalado** | Decripta o `ICO.self`; o `dev_flash` dele fornece o codec de áudio ATRAC | **File → Install Firmware** no RPCS3. Testado com o firmware 4.91. |
| Visual Studio Build Tools (C++, Clang, CMake) | Compila o jogo | O instalador oferece a instalação (≈6 GB, pelo winget). |
| Python 3.10+ | Roda o recompilador | O instalador oferece a instalação. |
| ~5 GB livres | Build + dados copiados | ~3 GB se você usar um link para o dump em vez de copiar. |

### Passo a passo

1. **Faça o dump do seu disco** com o seu próprio PS3 (por exemplo com o
   multiMAN, ou com um leitor de Blu-ray que leia discos de PS3). Você precisa
   da pasta `PS3_GAME`.
2. **Instale o RPCS3** e o **firmware do PS3** (File → Install Firmware).
3. **Baixe o `ICO.PC.Port.Setup.exe`** em [Releases](../../releases) e execute-o.
4. No instalador:
   1. **Seu jogo**: selecione a pasta `PS3_GAME` (ou a pasta do dump que a contém).
   2. **Decriptar o ICO.self**: mantenha *"Decriptar automaticamente com o meu RPCS3"*
      e aponte para o `rpcs3.exe`, **ou** escolha *"Eu já decriptei"* e selecione o seu
      `ICO.elf` ([como decriptar manualmente](docs/DECRYPTING.md)).
   3. **Firmware do PS3**: a pasta `dev_flash` do RPCS3 (preenchida sozinha
      quando está ao lado do `rpcs3.exe`).
   4. **Pasta de instalação**: um caminho curto, por exemplo `C:\Games`. O
      instalador cria a pasta `ICO` dentro dela.
   5. **Ferramentas de compilação**: se algo aparecer com ✖, clique em
      **Instalar...** e confirme.
5. Clique em **Instalar**. A primeira instalação leva de **6 a 30 minutos**,
   conforme o PC, e quase todo esse tempo é compilação. *Mostrar detalhes*
   exibe o log completo, que também fica salvo em `<instalação>\logs\install.log`.
6. Clique em **Jogar**, ou use o atalho **ICO** na área de trabalho ou no menu Iniciar.

### Decriptar manualmente pelo RPCS3

1. Abra o RPCS3.
2. **Utilities → Decrypt PS3 Binaries**.
3. Selecione `PS3_GAME\USRDIR\ICO.self` do seu dump.
4. O RPCS3 grava o `ICO.elf` na mesma pasta. Selecione-o no instalador.

Detalhes e hashes de conferência estão em [docs/DECRYPTING.md](docs/DECRYPTING.md).

Para **atualizar**, rode o instalador novo e selecione a mesma pasta de
instalação. As suas edições no `ico.ini` e os saves são mantidos.
Para **desinstalar**, apague a pasta de instalação e os atalhos.

---

## 🎮 Controles

Um controle XInput (compatível com Xbox) funciona direto. Sem controle, o
teclado funciona assim:

| PS3 | Teclado | | PS3 | Teclado |
|---|---|---|---|---|
| Direcional / analógico esquerdo | Setas | | L1 / R1 | Q / W |
| ✕ Xis | Z | | L2 / R2 | 1 / 2 |
| ◯ Círculo | X | | Start | Enter |
| ▢ Quadrado | A | | Select | Tab |
| △ Triângulo | S | | | |

As configurações ficam no `ico.ini`, ao lado do `ICO.exe` (veja
[docs/ENVIRONMENT.md](docs/ENVIRONMENT.md)). O log do jogo é
`<instalação>\logs\ico.log`: anexe-o aos relatos de bug.

---

## 🐞 Problemas conhecidos

- **Fechar a janela pode não encerrar o processo.** Se o `ICO.exe` continuar
  no Gerenciador de Tarefas depois de fechado, finalize-o lá antes de abrir de novo.
- **Só a versão dos EUA** (BCUS98259) é suportada. Outras regiões falham na
  verificação de hash, porque os pontos de corte do código são específicos desse executável.
- **Falhas curtas de áudio** podem ocorrer quando uma faixa começa.
- **Sem anti-aliasing**: o Edge MLAA do jogo (SPU) é pulado para ganhar
  desempenho (`ICO_SKIP_MLAA=1`). Um AA/upscaler de PC está planejado.
- **Janela fixa em 1280×720**, ainda sem menu de gráficos.
- **Um núcleo da CPU fica ocupado** enquanto o jogo roda, porque o renderizador SPU fica consultando se há trabalho.
- **A primeira instalação é longa** e exige o Visual Studio Build Tools (~6 GB).
- **Só Windows** (Direct3D 12).

Achou outro problema? [Abra uma issue](../../issues) com o seu
`logs\ico.log` e o `logs\install.log`.

---

## 🆕 Novidades da 0.1.0

Primeiro build público. Destaques (lista completa em [CHANGELOG.md](CHANGELOG.md)):

- **Instalador** no estilo de outros ports de recomp: escolha do dump,
  decriptação automática pelo RPCS3 ou seu próprio `ICO.elf`, verificação
  das ferramentas, barra de progresso e atalhos.
- **Gráficos**: backend Direct3D 12 do motor de desenho RSX compartilhado. Acabaram a tela
  preta, os flashes brancos, a geometria faltando e as cores lavadas.
- **Desempenho**: de < 1 fps para ~48–60 fps no 3D (caches de shader, re-lift
  da PPU com `--code-end`, matemática SPU em AVX2 e MLAA pulado).
- **Áudio**: codec ATRAC do firmware e o overlay DSP da MultiStream recompilados.

---

## 🛠️ Como funciona

```
 seu PS3_GAME ──► ICO.self ──(seu RPCS3 --decrypt)──► ICO.elf
                                                        │
             ┌──────────────────────────────────────────┤
             ▼                                          ▼
   ppu_loader + ppu_lifter                      extract_spu_images
   (--code-end 0x3BC7B4)                        spu_lifter × 6 (--code-end por imagem)
   gen_hle_nids --all                           + overlay DSP da MultiStream (+0x30000)
             │                                  + flashATRAC.pic do SEU dev_flash (+0x33100)
             │                                  build_spu_workloads
             └──────────────┬───────────────────────────┘
                            ▼
         CMake + Ninja + clang-cl  ◄── port/ (main.cpp, stubs.cpp) + runtime ps3recomp
                            ▼
              <instalação>\ICO.exe + ico.ini + vfs\PS3_GAME (seus dados)
```

Tudo o que o pipeline precisa saber sobre a versão suportada fica em
[`pipeline/ico_profile.json`](pipeline/ico_profile.json): os hashes dos
arquivos, os fins de código da PPU e das SPUs, as bases dos overlays e as
configurações de execução.

---

## 📦 Build a partir do código

Para desenvolvedores: veja [docs/BUILDING.md](docs/BUILDING.md).

---

## 🗺️ Próximos passos

1. Confirmar o gameplay de ponta a ponta (controles, saves, sessões longas).
2. Teclado + mouse, junto com o controle.
3. Menu de gráficos: resolução, vsync, filtro anisotrópico, limitador de FPS.
4. AA/upscaling moderno (FSR, DLSS).
5. Enviar as correções de runtime para o ps3recomp oficial.

---

## ⚖️ Aspectos legais

Este repositório e os releases **não contêm código do jogo, assets, firmware
do PS3 nem chaves**. Você precisa ter o jogo e fazer o dump você mesmo. O jogo
é recompilado no seu PC a partir dos seus arquivos. O resultado é para uso
pessoal e não deve ser redistribuído. A decriptação é feita pela sua própria
cópia do RPCS3.

ICO é © Sony Interactive Entertainment. Este projeto não tem vínculo nem aval
da Sony nem da Team ICO / genDESIGN. O código deste repositório está sob a
[licença MIT](LICENSE).

---

## 🔗 Projetos relacionados

- [ps3recomp](https://github.com/sp00nznet/ps3recomp): o toolkit de recompilação estática de PS3 que este port usa.
- [shadowofthecolossus](https://github.com/sp00nznet/shadowofthecolossus): port irmão, da mesma coletânea.
- [saintsrow2](https://github.com/sp00nznet/saintsrow2): outro port com ps3recomp.
- [RPCS3](https://rpcs3.net): emulador de PS3, usado aqui para decriptar o executável e fornecer o firmware.
