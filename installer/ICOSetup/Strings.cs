namespace ICOSetup;

/// <summary>UI text. The installer is English-only.</summary>
static class S
{
    public static string Title => "ICO PC Port Setup";
    public static string Intro => "Select your own copy of ICO. Setup recompiles it into a native Windows game on this PC. No game files are included.";

    public static string GameGroup => "1. Your game (PS3 disc dump)";
    public static string GameHint => "Select the PS3_GAME folder of \"ICO & Shadow of the Colossus Collection\" (USA, BCUS98259).";
    public static string Browse => "Browse...";

    public static string ElfGroup => "2. Decrypt ICO.self";
    public static string ElfAuto => "Decrypt automatically with my RPCS3";
    public static string ElfManual => "I already decrypted it (ICO.elf)";
    public static string ElfHelp => "How to decrypt by hand";
    public static string Rpcs3Exe => "rpcs3.exe:";
    public static string ElfFile => "ICO.elf:";

    public static string FwGroup => "3. PS3 firmware (RPCS3 dev_flash)";
    public static string FwHint => "The game's audio codec is recompiled from your installed firmware (RPCS3: File > Install Firmware).";

    public static string DestGroup => "4. Installation folder";
    public static string DestHint => "Setup creates a new ICO folder inside it.";
    public static string CopyGame => "Copy the game data (about 1.7 GB) instead of linking to the dump";
    public static string Shortcut => "Create desktop shortcut";

    public static string ToolsGroup => "Build tools";
    public static string Install => "Install";
    public static string InstallMissing => "Install...";
    public static string Cancel => "Cancel";
    public static string Close => "Close";
    public static string Play => "Play";
    public static string ShowLog => "Show details";
    public static string HideLog => "Hide details";

    public static string VsOk(string v) => $"Visual Studio C++ + Clang: {v}";
    public static string VsNoClang(string v) => $"Visual Studio {v} is missing the C++ Clang component";
    public static string VsMissing => "Visual Studio Build Tools (C++ and Clang): not installed";
    public static string PyOk(string v) => $"Python {v}";
    public static string PyMissing => "Python 3.10 or newer: not installed";
    public static string CpuAvx2 => "CPU: AVX2 build";
    public static string CpuNoAvx2 => "CPU: no AVX2, using the SSE4.1 build (slower)";

    public static string ConfirmVs => "Setup will install Microsoft Visual Studio Build Tools 2026 with the C++ and Clang components through winget (about 6 GB). Windows will ask for permission. Continue?";
    public static string ConfirmVsModify => "Setup will add the C++ Clang component to your Visual Studio installation. Windows will ask for permission. Continue?";
    public static string ConfirmPy => "Setup will install Python 3.12 for your user through winget. Continue?";
    public static string ConfirmPip => "Setup downloads the Python packages the recompiler needs (capstone, pycryptodome, construct, tabulate, tqdm, about 15 MB) from PyPI into the install folder. Continue?";
    public static string NoWinget => "winget was not found. Install the tool by hand (see the README), then reopen Setup.";

    public static string ErrGame => "Select your game folder.";
    public static string ErrGameNotIco => "No ICO data was found here. Select the PS3_GAME folder that holds USRDIR\\ICO\\ico.psarc.";
    public static string ErrElf => "Select your decrypted ICO.elf, or choose automatic decryption.";
    public static string ErrRpcs3 => "Select rpcs3.exe.";
    public static string ErrFw => "dev_flash\\sys\\external\\flashATRAC.pic was not found. Install the PS3 firmware in RPCS3 and select its folder.";
    public static string ErrDest => "Choose an installation folder.";
    public static string ErrDestLong => "This path is too long for the build. Choose a shorter folder (for example C:\\Games).";
    public static string ErrDestOther => "This folder holds something else. Choose a new folder, or one with an ICO install to update it.";
    public static string ErrTools => "Install the missing build tools first.";
    public static string ErrSpace(int gb) => $"At least {gb} GB of free space is required.";
    public static string ErrNoPayload => "This setup has no payload. Run it with --source <repository folder>, or download the release build.";

    public static string Extracting => "Unpacking setup files...";
    public static string Venv => "Preparing Python...";
    public static string Pip => "Downloading Python packages...";
    public static string Shortcuts => "Creating shortcuts...";
    public static string Done(int min) => $"Installed in {min} min. Launch ICO from the shortcut or ico.exe.";
    public static string Failed(string why) => $"Installation failed: {why}";
    public static string Cancelled => "Installation cancelled. Your game files were not changed.";
    public static string ConfirmCancel => "Stop the installation?";
    public static string Warn => "Warning";

    public static string Step(string en) => en;
}
