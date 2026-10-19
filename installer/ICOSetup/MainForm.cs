using System.Diagnostics;
using System.IO.Compression;
using System.Reflection;
using System.Text;

namespace ICOSetup;

sealed class MainForm : Form
{
    readonly string? _sourceOverride;     // --source <repo>: dev runs without a payload
    readonly string? _ps3recompOverride;  // --ps3recomp <dir>

    readonly TextBox _game = new(), _rpcs3 = new(), _elf = new(), _fw = new(), _dest = new();
    readonly RadioButton _auto = new(), _manual = new();
    readonly CheckBox _copy = new(), _shortcut = new();
    readonly Label _vsLabel = new(), _pyLabel = new(), _cpuLabel = new(), _status = new();
    readonly Button _vsFix = new(), _pyFix = new(), _install = new(), _cancel = new(), _logToggle = new();
    readonly ProgressBar _bar = new();
    readonly TextBox _log = new();
    readonly Button _rpcs3Browse = new(), _elfBrowse = new();

    Toolchain.VsInfo? _vs;
    Toolchain.PyInfo? _py;
    Process? _proc;
    bool _running, _cancelRequested, _finished;
    StreamWriter? _logFile;
    string _installDir = "";

    static string Version => Assembly.GetExecutingAssembly().GetName().Version?.ToString(3) ?? "0.0.0";

    public MainForm(string? source, string? ps3recomp)
    {
        _sourceOverride = source;
        _ps3recompOverride = ps3recomp;
        Text = $"{S.Title} {Version}";
        Font = new Font("Segoe UI", 9f);
        AutoScaleMode = AutoScaleMode.Dpi;
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(720, 640);
        try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }
        BuildUi();
        Prefill();
        RefreshTools();
    }

    // ------------------------------------------------------------------ UI --

    void BuildUi()
    {
        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill, ColumnCount = 1, Padding = new Padding(16, 12, 16, 12), AutoScroll = true,
        };
        Controls.Add(root);

        var header = new Label
        {
            Text = "ICO — PC Port", Font = new Font("Segoe UI Semibold", 16f), AutoSize = true,
            Margin = new Padding(0, 0, 0, 2),
        };
        root.Controls.Add(header);
        root.Controls.Add(new Label { Text = S.Intro, AutoSize = true, MaximumSize = new Size(680, 0), Margin = new Padding(0, 0, 0, 8) });

        // 1. game
        var g1 = Group(S.GameGroup);
        g1.Controls.Add(Hint(S.GameHint), 0, 0); g1.SetColumnSpan(g1.GetControlFromPosition(0, 0)!, 3);
        AddPathRow(g1, 1, "PS3_GAME:", _game, BrowseFolder(_game, S.GameHint));
        root.Controls.Add(g1.Parent!);

        // 2. decrypt
        var g2 = Group(S.ElfGroup);
        _auto.Text = S.ElfAuto; _auto.AutoSize = true; _auto.Checked = true;
        _manual.Text = S.ElfManual; _manual.AutoSize = true;
        _auto.CheckedChanged += (_, _) => UpdateElfMode();
        var help = new LinkLabel { Text = S.ElfHelp, AutoSize = true, Anchor = AnchorStyles.Right };
        help.LinkClicked += (_, _) => OpenUrl("https://github.com/feanor-noldorin/ico-recomp/blob/main/docs/DECRYPTING.md");
        g2.Controls.Add(_auto, 0, 0); g2.SetColumnSpan(_auto, 2); g2.Controls.Add(help, 2, 0);
        AddPathRow(g2, 1, S.Rpcs3Exe, _rpcs3, _rpcs3Browse);
        _rpcs3Browse.Click += (_, _) => PickFile(_rpcs3, "rpcs3.exe|rpcs3.exe", () => AutoFirmware());
        g2.Controls.Add(_manual, 0, 2); g2.SetColumnSpan(_manual, 3);
        AddPathRow(g2, 3, S.ElfFile, _elf, _elfBrowse);
        _elfBrowse.Click += (_, _) => PickFile(_elf, "ICO.elf|*.elf", null);
        root.Controls.Add(g2.Parent!);

        // 3. firmware
        var g3 = Group(S.FwGroup);
        g3.Controls.Add(Hint(S.FwHint), 0, 0); g3.SetColumnSpan(g3.GetControlFromPosition(0, 0)!, 3);
        AddPathRow(g3, 1, "dev_flash:", _fw, BrowseFolder(_fw, S.FwGroup));
        root.Controls.Add(g3.Parent!);

        // 4. destination
        var g4 = Group(S.DestGroup);
        g4.Controls.Add(Hint(S.DestHint), 0, 0); g4.SetColumnSpan(g4.GetControlFromPosition(0, 0)!, 3);
        AddPathRow(g4, 1, "", _dest, BrowseFolder(_dest, S.DestHint));
        _copy.Text = S.CopyGame; _copy.AutoSize = true; _copy.Checked = true;
        _shortcut.Text = S.Shortcut; _shortcut.AutoSize = true; _shortcut.Checked = true;
        g4.Controls.Add(_copy, 0, 2); g4.SetColumnSpan(_copy, 3);
        g4.Controls.Add(_shortcut, 0, 3); g4.SetColumnSpan(_shortcut, 3);
        root.Controls.Add(g4.Parent!);

        // tools
        var g5 = Group(S.ToolsGroup);
        ToolRow(g5, 0, _vsLabel, _vsFix); _vsFix.Click += (_, _) => FixVs();
        ToolRow(g5, 1, _pyLabel, _pyFix); _pyFix.Click += (_, _) => FixPython();
        _cpuLabel.AutoSize = true; g5.Controls.Add(_cpuLabel, 0, 2); g5.SetColumnSpan(_cpuLabel, 3);
        root.Controls.Add(g5.Parent!);

        // progress + buttons
        _bar.Dock = DockStyle.Top; _bar.Height = 18; _bar.Margin = new Padding(0, 8, 0, 4);
        root.Controls.Add(_bar);
        _status.AutoSize = true; _status.MaximumSize = new Size(680, 0);
        root.Controls.Add(_status);

        var buttons = new FlowLayoutPanel { FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Top, AutoSize = true };
        _install.Text = S.Install; _install.AutoSize = true; _install.Padding = new Padding(12, 2, 12, 2);
        _install.Click += async (_, _) => await OnInstall();
        _cancel.Text = S.Close; _cancel.AutoSize = true;
        _cancel.Click += (_, _) => OnCancel();
        _logToggle.Text = S.ShowLog; _logToggle.AutoSize = true;
        _logToggle.Click += (_, _) => { _log.Visible = !_log.Visible; _logToggle.Text = _log.Visible ? S.HideLog : S.ShowLog; };
        buttons.Controls.AddRange(new Control[] { _install, _cancel, _logToggle });
        root.Controls.Add(buttons);

        _log.Multiline = true; _log.ReadOnly = true; _log.ScrollBars = ScrollBars.Vertical;
        _log.Font = new Font("Consolas", 8.5f); _log.Height = 200; _log.Dock = DockStyle.Top; _log.Visible = false;
        _log.WordWrap = false;
        root.Controls.Add(_log);

        AcceptButton = _install;
        FormClosing += (_, e) => { if (_running && !ConfirmStop()) e.Cancel = true; };
    }

    static TableLayoutPanel Group(string title)
    {
        var box = new GroupBox { Text = title, Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(8, 4, 8, 6) };
        var t = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3, AutoSize = true };
        t.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        t.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        t.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        box.Controls.Add(t);
        return t;
    }

    static Label Hint(string text) => new()
    {
        Text = text, AutoSize = true, ForeColor = SystemColors.GrayText, MaximumSize = new Size(640, 0),
        Margin = new Padding(3, 0, 3, 4),
    };

    static void AddPathRow(TableLayoutPanel t, int row, string label, TextBox box, Button browse)
    {
        t.Controls.Add(new Label { Text = label, AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(3, 7, 3, 3) }, 0, row);
        box.Dock = DockStyle.Fill;
        t.Controls.Add(box, 1, row);
        browse.Text = S.Browse; browse.AutoSize = true;
        t.Controls.Add(browse, 2, row);
    }

    static void ToolRow(TableLayoutPanel t, int row, Label l, Button fix)
    {
        l.AutoSize = true; l.Anchor = AnchorStyles.Left; l.Margin = new Padding(3, 7, 3, 3);
        t.Controls.Add(l, 0, row); t.SetColumnSpan(l, 2);
        fix.Text = S.InstallMissing; fix.AutoSize = true;
        t.Controls.Add(fix, 2, row);
    }

    Button BrowseFolder(TextBox target, string description)
    {
        var b = new Button();
        b.Click += (_, _) =>
        {
            using var d = new FolderBrowserDialog { Description = description, UseDescriptionForTitle = true };
            if (Directory.Exists(target.Text)) d.InitialDirectory = target.Text;
            if (d.ShowDialog(this) == DialogResult.OK)
            {
                target.Text = d.SelectedPath;
                if (target == _game) AutoFromGame();
            }
        };
        return b;
    }

    void PickFile(TextBox target, string filter, Action? after)
    {
        using var d = new OpenFileDialog { Filter = filter };
        if (File.Exists(target.Text)) d.InitialDirectory = Path.GetDirectoryName(target.Text);
        if (d.ShowDialog(this) == DialogResult.OK) { target.Text = d.FileName; after?.Invoke(); }
    }

    void UpdateElfMode()
    {
        _rpcs3.Enabled = _rpcs3Browse.Enabled = _auto.Checked;
        _elf.Enabled = _elfBrowse.Enabled = !_auto.Checked;
    }

    void Prefill()
    {
        var r = Toolchain.FindRpcs3();
        if (r != null) { _rpcs3.Text = r; AutoFirmware(); }
        _dest.Text = @"C:\Games";
        UpdateElfMode();
    }

    /// <summary>dev_flash usually sits next to rpcs3.exe.</summary>
    void AutoFirmware()
    {
        if (_fw.Text.Length > 0 || !File.Exists(_rpcs3.Text)) return;
        var df = Path.Combine(Path.GetDirectoryName(_rpcs3.Text)!, "dev_flash");
        if (Directory.Exists(df)) _fw.Text = df;
    }

    /// <summary>A decrypted ICO.elf left next to ICO.self by RPCS3 is picked up.</summary>
    void AutoFromGame()
    {
        var gd = GameDir();
        if (gd == null) return;
        var elf = Path.Combine(gd, "USRDIR", "ICO.elf");
        if (File.Exists(elf) && _elf.Text.Length == 0) _elf.Text = elf;
    }

    string? GameDir()
    {
        var p = _game.Text.Trim();
        if (p.Length == 0) return null;
        if (Directory.Exists(Path.Combine(p, "PS3_GAME"))) p = Path.Combine(p, "PS3_GAME");
        return File.Exists(Path.Combine(p, "USRDIR", "ICO", "ico.psarc")) ? p : null;
    }

    // --------------------------------------------------------------- tools --

    void RefreshTools()
    {
        _vs = Toolchain.FindVs();
        _vsLabel.Text = (_vs?.Complete ?? false) ? "✔ " + S.VsOk(_vs.Version)
                      : _vs != null ? "✖ " + S.VsNoClang(_vs.Version) : "✖ " + S.VsMissing;
        _vsFix.Visible = !(_vs?.Complete ?? false);
        _py = Toolchain.FindPython();
        _pyLabel.Text = _py != null ? "✔ " + S.PyOk(_py.Version) : "✖ " + S.PyMissing;
        _pyFix.Visible = _py == null;
        _cpuLabel.Text = (Toolchain.HasAvx2 ? "✔ " : "• ") + (Toolchain.HasAvx2 ? S.CpuAvx2 : S.CpuNoAvx2);
    }

    void FixVs()
    {
        if (_vs != null && !_vs.Complete)
        {
            if (Ask(S.ConfirmVsModify)) { UseWaitCursor = true; Toolchain.ModifyVs(_vs.Path); UseWaitCursor = false; }
        }
        else
        {
            if (!Toolchain.HasWinget()) { Error(S.NoWinget); return; }
            if (Ask(S.ConfirmVs)) { UseWaitCursor = true; Toolchain.RunVisible("winget", Toolchain.WingetVsArgs()); UseWaitCursor = false; }
        }
        RefreshTools();
    }

    void FixPython()
    {
        if (!Toolchain.HasWinget()) { Error(S.NoWinget); return; }
        if (Ask(S.ConfirmPy)) { UseWaitCursor = true; Toolchain.RunVisible("winget", Toolchain.WingetPythonArgs()); UseWaitCursor = false; }
        RefreshTools();
    }

    // ------------------------------------------------------------- install --

    string? ValidateInputs()
    {
        if (_game.Text.Trim().Length == 0) return S.ErrGame;
        if (GameDir() == null) return S.ErrGameNotIco;
        if (_auto.Checked && !File.Exists(_rpcs3.Text)) return S.ErrRpcs3;
        if (_manual.Checked && !File.Exists(_elf.Text)) return S.ErrElf;
        var fw = _fw.Text.Trim();
        if (Directory.Exists(Path.Combine(fw, "dev_flash"))) fw = Path.Combine(fw, "dev_flash");
        if (!File.Exists(Path.Combine(fw, "sys", "external", "flashATRAC.pic"))) return S.ErrFw;
        if (_dest.Text.Trim().Length == 0) return S.ErrDest;
        var dir = Path.Combine(Path.GetFullPath(_dest.Text.Trim()), "ICO");
        if (dir.Length > 80) return S.ErrDestLong;   // the build nests ~150 more characters below it
        if (Directory.Exists(dir) && !File.Exists(Path.Combine(dir, "install.json"))
            && Directory.EnumerateFileSystemEntries(dir).Any()) return S.ErrDestOther;
        if (!(_vs?.Complete ?? false) || _py == null) return S.ErrTools;
        var need = _copy.Checked ? 5 : 3;
        try
        {
            var drive = new DriveInfo(Path.GetPathRoot(dir)!);
            if (drive.AvailableFreeSpace < need * (1L << 30)) return S.ErrSpace(need);
        }
        catch { }
        _installDir = dir;
        return null;
    }

    async Task OnInstall()
    {
        if (_finished) { Launch(); return; }
        var err = ValidateInputs();
        if (err != null) { Error(err); return; }
        if (!Ask(S.ConfirmPip)) return;

        SetRunning(true);
        var t0 = DateTime.Now;
        int rc;
        try
        {
            Directory.CreateDirectory(Path.Combine(_installDir, "logs"));
            _logFile = new StreamWriter(Path.Combine(_installDir, "logs", "install.log"), false, Encoding.UTF8) { AutoFlush = true };
            Log($"{S.Title} {Version} — {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
            rc = await Task.Run(RunAll);
        }
        catch (Exception ex)
        {
            Log(ex.ToString());
            rc = -1;
            Status(S.Failed(ex.Message));
        }
        SetRunning(false);
        _logFile?.Dispose(); _logFile = null;

        if (rc == 0)
        {
            _bar.Value = 100;
            Status("✔ " + S.Done((int)Math.Ceiling((DateTime.Now - t0).TotalMinutes)));
            _finished = true;
            _install.Text = S.Play;
        }
        else if (_cancelRequested)
        {
            Status(S.Cancelled);
            _cancelRequested = false;
        }
        else
        {
            _log.Visible = true; _logToggle.Text = S.HideLog;
        }
    }

    /// <summary>Everything after validation; runs off the UI thread.</summary>
    int RunAll()
    {
        // payload -> <install>\setup\src
        Status(S.Extracting);
        string src;
        if (_sourceOverride != null) src = _sourceOverride;
        else
        {
            using var payload = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip");
            if (payload == null) throw new InvalidOperationException(S.ErrNoPayload);
            src = Path.Combine(_installDir, "setup", "src");
            if (Directory.Exists(src)) Directory.Delete(src, true);
            ZipFile.ExtractToDirectory(payload, src);
        }
        var ps3recomp = _ps3recompOverride ?? Path.Combine(src, "external", "ps3recomp");
        if (_cancelRequested) return 1;

        // venv with the recompiler's requirements
        Status(S.Venv);
        var venv = Path.Combine(_installDir, "setup", "venv");
        var vpy = Path.Combine(venv, "Scripts", "python.exe");
        if (!File.Exists(vpy) && Run(_py!.Exe, new[] { "-m", "venv", venv }) != 0) return Fail("python -m venv");
        Status(S.Pip);
        if (Run(vpy, new[] { "-m", "pip", "install", "--disable-pip-version-check", "-r",
                Path.Combine(ps3recomp, "requirements.txt") }) != 0) return Fail("pip install");
        if (_cancelRequested) return 1;

        // the pipeline
        var args = new List<string>
        {
            Path.Combine(src, "pipeline", "ico_pipeline.py"),
            "--game-dir", GameDir()!,
            "--dev-flash", _fw.Text.Trim(),
            "--install", _installDir,
            "--ps3recomp", ps3recomp,
            "--port", Path.Combine(src, "port"),
            "--python", vpy,
            "--vs", _vs!.Path,
            "--link-mode", _copy.Checked ? "copy" : "junction",
            "--simd", Toolchain.HasAvx2 ? "avx2" : "sse41",
            "--version", Version,
        };
        if (_auto.Checked) { args.Add("--rpcs3"); args.Add(_rpcs3.Text); }
        else { args.Add("--elf"); args.Add(_elf.Text); }
        var rc = Run(vpy, args, pipeline: true);
        if (rc != 0) return rc;

        if (_shortcut.Checked)
        {
            Status(S.Shortcuts);
            var exe = Path.Combine(_installDir, "ICO.exe");
            Shortcuts.Create(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "ICO.lnk"), exe);
            var menu = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), "ICO PC Port");
            Directory.CreateDirectory(menu);
            Shortcuts.Create(Path.Combine(menu, "ICO.lnk"), exe);
        }
        // the lift and objects are only needed to build; keep the disk use low
        try { Directory.Delete(Path.Combine(_installDir, "build"), true); } catch { }
        return 0;
    }

    int Fail(string what)
    {
        Status(S.Failed(what));
        return 1;
    }

    /// <summary>Runs a child process, streaming its output into the log; parses the pipeline's @@ lines.</summary>
    int Run(string exe, IEnumerable<string> args, bool pipeline = false)
    {
        var psi = new ProcessStartInfo(exe)
        {
            RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false,
            CreateNoWindow = true, StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8,
        };
        psi.Environment["PYTHONUNBUFFERED"] = "1";
        psi.Environment["PYTHONIOENCODING"] = "utf-8";
        foreach (var a in args) psi.ArgumentList.Add(a);
        Log("> " + exe + " " + string.Join(" ", psi.ArgumentList.Select(a => a.Contains(' ') ? $"\"{a}\"" : a)));
        using var p = new Process { StartInfo = psi };
        string? error = null;
        void OnLine(string? line)
        {
            if (line == null) return;
            if (pipeline && line.StartsWith("@@"))
            {
                var (tag, rest) = (line.Split(' ', 2)[0], line.Contains(' ') ? line.Split(' ', 2)[1] : "");
                switch (tag)
                {
                    case "@@STEP":
                        var parts = rest.Split(' ', 2);
                        var n = parts[0].Split('/');
                        Status($"[{parts[0]}] {S.Step(parts.Length > 1 ? parts[1] : "")}");
                        stepIndex = int.Parse(n[0]); stepCount = int.Parse(n[1]);
                        SetBar(0);
                        break;
                    case "@@PROGRESS":
                        if (double.TryParse(rest, System.Globalization.NumberStyles.Float,
                                System.Globalization.CultureInfo.InvariantCulture, out var pct)) SetBar(pct);
                        return;
                    case "@@ERROR": error = rest; break;
                    case "@@WARN": BeginInvoke(() => MessageBox.Show(this, rest, S.Warn, MessageBoxButtons.OK, MessageBoxIcon.Warning)); break;
                }
            }
            Log(line);
        }
        p.OutputDataReceived += (_, e) => OnLine(e.Data);
        p.ErrorDataReceived += (_, e) => OnLine(e.Data);
        p.Start();
        _proc = p;
        p.BeginOutputReadLine(); p.BeginErrorReadLine();
        p.WaitForExit();
        _proc = null;
        if (error != null) Status(S.Failed(error));
        return p.ExitCode;
    }

    int stepIndex, stepCount = 1;

    /// <summary>Overall bar: each pipeline step is one slice; the compile step gets most of it.</summary>
    void SetBar(double stepPct)
    {
        // weights roughly follow measured time per step (compile ~70%)
        double[] w = { 1, 3, 1, 6, 1, 4, 1, 1, 3, 75, 4 };
        if (stepCount != w.Length) w = Enumerable.Repeat(1.0, Math.Max(1, stepCount)).ToArray();
        double before = w.Take(Math.Max(0, stepIndex - 1)).Sum(), total = w.Sum();
        var cur = stepIndex >= 1 && stepIndex <= w.Length ? w[stepIndex - 1] : 0;
        var v = (int)Math.Clamp(100 * (before + cur * stepPct / 100) / total, 0, 100);
        BeginInvoke(() => _bar.Value = v);
    }

    void Status(string s) => BeginInvoke(() => { _status.Text = s; Log("== " + s); });

    void Log(string s)
    {
        _logFile?.WriteLine(s);
        if (IsHandleCreated) BeginInvoke(() =>
        {
            if (_log.TextLength > 2_000_000) _log.Text = _log.Text[^1_000_000..];
            _log.AppendText(s + Environment.NewLine);
        });
    }

    void SetRunning(bool on)
    {
        _running = on;
        foreach (Control c in new Control[] { _game, _rpcs3, _elf, _fw, _dest, _auto, _manual, _copy, _shortcut,
                     _vsFix, _pyFix, _install, _rpcs3Browse, _elfBrowse })
            c.Enabled = !on;
        if (!on) UpdateElfMode();
        _cancel.Text = on ? S.Cancel : S.Close;
    }

    void OnCancel()
    {
        if (!_running) { Close(); return; }
        if (!ConfirmStop()) return;
        _cancelRequested = true;
        try { _proc?.Kill(entireProcessTree: true); } catch { }
    }

    bool ConfirmStop() => MessageBox.Show(this, S.ConfirmCancel, S.Title, MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes;

    void Launch()
    {
        var exe = Path.Combine(_installDir, "ICO.exe");
        Process.Start(new ProcessStartInfo(exe) { UseShellExecute = true, WorkingDirectory = _installDir });
        Close();
    }

    bool Ask(string q) => MessageBox.Show(this, q, S.Title, MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes;
    void Error(string e) => MessageBox.Show(this, e, S.Title, MessageBoxButtons.OK, MessageBoxIcon.Warning);

    static void OpenUrl(string url)
    {
        try { Process.Start(new ProcessStartInfo(url) { UseShellExecute = true }); } catch { }
    }
}
