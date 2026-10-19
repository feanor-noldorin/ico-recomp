using System.Globalization;

namespace ICOSetup;

/// <summary>UI text in English and Brazilian Portuguese, picked from the Windows UI language.</summary>
static class S
{
    static readonly bool Pt = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName == "pt";

    static string T(string en, string pt) => Pt ? pt : en;

    public static string Title => T("ICO PC Port Setup", "Instalador do ICO PC Port");
    public static string Intro => T(
        "Select your own copy of ICO. Setup recompiles it into a native Windows game on this PC. No game files are included.",
        "Selecione a sua própria cópia do ICO. O instalador a recompila como um jogo nativo de Windows neste PC. Nenhum arquivo do jogo está incluído.");

    public static string GameGroup => T("1. Your game (PS3 disc dump)", "1. Seu jogo (dump do disco de PS3)");
    public static string GameHint => T(
        "Select the PS3_GAME folder of \"ICO & Shadow of the Colossus Collection\" (USA, BCUS98259).",
        "Selecione a pasta PS3_GAME de \"ICO & Shadow of the Colossus Collection\" (EUA, BCUS98259).");
    public static string Browse => T("Browse...", "Procurar...");

    public static string ElfGroup => T("2. Decrypt ICO.self", "2. Decriptar o ICO.self");
    public static string ElfAuto => T("Decrypt automatically with my RPCS3", "Decriptar automaticamente com o meu RPCS3");
    public static string ElfManual => T("I already decrypted it (ICO.elf)", "Eu já decriptei (ICO.elf)");
    public static string ElfHelp => T("How to decrypt by hand", "Como decriptar manualmente");
    public static string Rpcs3Exe => T("rpcs3.exe:", "rpcs3.exe:");
    public static string ElfFile => T("ICO.elf:", "ICO.elf:");

    public static string FwGroup => T("3. PS3 firmware (RPCS3 dev_flash)", "3. Firmware do PS3 (dev_flash do RPCS3)");
    public static string FwHint => T(
        "The game's audio codec is recompiled from your installed firmware (RPCS3: File > Install Firmware).",
        "O codec de áudio do jogo é recompilado a partir do seu firmware instalado (RPCS3: File > Install Firmware).");

    public static string DestGroup => T("4. Installation folder", "4. Pasta de instalação");
    public static string DestHint => T("Setup creates a new ICO folder inside it.", "O instalador cria uma pasta ICO dentro dela.");
    public static string CopyGame => T("Copy the game data (about 1.7 GB) instead of linking to the dump",
        "Copiar os dados do jogo (cerca de 1,7 GB) em vez de usar um link para o dump");
    public static string Shortcut => T("Create desktop shortcut", "Criar atalho na área de trabalho");

    public static string ToolsGroup => T("Build tools", "Ferramentas de compilação");
    public static string Install => T("Install", "Instalar");
    public static string InstallMissing => T("Install...", "Instalar...");
    public static string Cancel => T("Cancel", "Cancelar");
    public static string Close => T("Close", "Fechar");
    public static string Play => T("Play", "Jogar");
    public static string ShowLog => T("Show details", "Mostrar detalhes");
    public static string HideLog => T("Hide details", "Ocultar detalhes");

    public static string VsOk(string v) => T($"Visual Studio C++ + Clang: {v}", $"Visual Studio C++ + Clang: {v}");
    public static string VsNoClang(string v) => T($"Visual Studio {v} is missing the C++ Clang component",
        $"O Visual Studio {v} não tem o componente C++ Clang");
    public static string VsMissing => T("Visual Studio Build Tools (C++ and Clang): not installed",
        "Visual Studio Build Tools (C++ e Clang): não instalado");
    public static string PyOk(string v) => T($"Python {v}", $"Python {v}");
    public static string PyMissing => T("Python 3.10 or newer: not installed", "Python 3.10 ou mais novo: não instalado");
    public static string CpuAvx2 => T("CPU: AVX2 build", "CPU: build com AVX2");
    public static string CpuNoAvx2 => T("CPU: no AVX2, using the SSE4.1 build (slower)", "CPU: sem AVX2, usando o build SSE4.1 (mais lento)");

    public static string ConfirmVs => T(
        "Setup will install Microsoft Visual Studio Build Tools 2026 with the C++ and Clang components through winget (about 6 GB). Windows will ask for permission. Continue?",
        "O instalador vai instalar o Microsoft Visual Studio Build Tools 2026 com os componentes C++ e Clang pelo winget (cerca de 6 GB). O Windows vai pedir permissão. Continuar?");
    public static string ConfirmVsModify => T(
        "Setup will add the C++ Clang component to your Visual Studio installation. Windows will ask for permission. Continue?",
        "O instalador vai adicionar o componente C++ Clang à sua instalação do Visual Studio. O Windows vai pedir permissão. Continuar?");
    public static string ConfirmPy => T(
        "Setup will install Python 3.12 for your user through winget. Continue?",
        "O instalador vai instalar o Python 3.12 para o seu usuário pelo winget. Continuar?");
    public static string ConfirmPip => T(
        "Setup downloads the Python packages the recompiler needs (capstone, pycryptodome, construct, tabulate, tqdm, about 15 MB) from PyPI into the install folder. Continue?",
        "O instalador baixa os pacotes Python de que o recompilador precisa (capstone, pycryptodome, construct, tabulate, tqdm, cerca de 15 MB) do PyPI para a pasta de instalação. Continuar?");
    public static string NoWinget => T("winget was not found. Install the tool by hand (see the README), then reopen Setup.",
        "O winget não foi encontrado. Instale a ferramenta manualmente (veja o README) e abra o instalador de novo.");

    public static string ErrGame => T("Select your game folder.", "Selecione a pasta do jogo.");
    public static string ErrGameNotIco => T("No ICO data was found here. Select the PS3_GAME folder that holds USRDIR\\ICO\\ico.psarc.",
        "Nenhum dado do ICO foi encontrado aqui. Selecione a pasta PS3_GAME que contém USRDIR\\ICO\\ico.psarc.");
    public static string ErrElf => T("Select your decrypted ICO.elf, or choose automatic decryption.",
        "Selecione o seu ICO.elf decriptado, ou escolha a decriptação automática.");
    public static string ErrRpcs3 => T("Select rpcs3.exe.", "Selecione o rpcs3.exe.");
    public static string ErrFw => T("dev_flash\\sys\\external\\flashATRAC.pic was not found. Install the PS3 firmware in RPCS3 and select its folder.",
        "dev_flash\\sys\\external\\flashATRAC.pic não foi encontrado. Instale o firmware do PS3 no RPCS3 e selecione a pasta dele.");
    public static string ErrDest => T("Choose an installation folder.", "Escolha uma pasta de instalação.");
    public static string ErrDestLong => T("This path is too long for the build. Choose a shorter folder (for example C:\\Games).",
        "Este caminho é longo demais para o build. Escolha uma pasta mais curta (por exemplo C:\\Games).");
    public static string ErrDestOther => T("This folder holds something else. Choose a new folder, or one with an ICO install to update it.",
        "Esta pasta contém outra coisa. Escolha uma pasta nova, ou uma com uma instalação do ICO para atualizá-la.");
    public static string ErrTools => T("Install the missing build tools first.", "Instale primeiro as ferramentas de compilação que faltam.");
    public static string ErrSpace(int gb) => T($"At least {gb} GB of free space is required.", $"São necessários pelo menos {gb} GB livres.");
    public static string ErrNoPayload => T("This setup has no payload. Run it with --source <repository folder>, or download the release build.",
        "Este instalador não tem payload. Rode com --source <pasta do repositório>, ou baixe o build do release.");

    public static string Extracting => T("Unpacking setup files...", "Descompactando os arquivos do instalador...");
    public static string Venv => T("Preparing Python...", "Preparando o Python...");
    public static string Pip => T("Downloading Python packages...", "Baixando pacotes Python...");
    public static string Shortcuts => T("Creating shortcuts...", "Criando atalhos...");
    public static string Done(int min) => T($"Installed in {min} min. Launch ICO from the shortcut or ICO.exe.",
        $"Instalado em {min} min. Abra o ICO pelo atalho ou pelo ICO.exe.");
    public static string Failed(string why) => T($"Installation failed: {why}", $"A instalação falhou: {why}");
    public static string Cancelled => T("Installation cancelled. Your game files were not changed.",
        "Instalação cancelada. Os arquivos do seu jogo não foram alterados.");
    public static string ConfirmCancel => T("Stop the installation?", "Parar a instalação?");
    public static string Warn => T("Warning", "Aviso");

    public static string Step(string en) => en switch
    {
        "Checking your files" => T(en, "Verificando seus arquivos"),
        "Preparing game data" => T(en, "Preparando os dados do jogo"),
        "Loading ICO.elf" => T(en, "Carregando o ICO.elf"),
        "Recompiling PPU code" => T(en, "Recompilando o código da PPU"),
        "Generating HLE table" => T(en, "Gerando a tabela HLE"),
        "Recompiling SPU programs" => T(en, "Recompilando os programas SPU"),
        "Recompiling SPU overlays" => T(en, "Recompilando os overlays SPU"),
        "Registering SPU programs" => T(en, "Registrando os programas SPU"),
        "Configuring build" => T(en, "Configurando o build"),
        "Compiling (this is the long part)" => T(en, "Compilando (esta é a parte demorada)"),
        "Installing" => T(en, "Instalando"),
        _ => en,
    };
}
