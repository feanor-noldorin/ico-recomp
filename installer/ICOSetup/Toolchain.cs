using System.Diagnostics;
using System.Runtime.Intrinsics.X86;
using System.Text;

namespace ICOSetup;

/// <summary>Finds (and on request installs) what the recompiler needs on this PC.</summary>
static class Toolchain
{
    // The C++ workload pieces the build uses: MSVC headers/libs + Windows SDK
    // (VCTools), the bundled clang-cl, and CMake/Ninja.
    public static readonly string[] VsComponents =
    {
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "Microsoft.VisualStudio.Component.VC.Llvm.Clang",
        "Microsoft.VisualStudio.Component.VC.CMake.Project",
    };

    public record VsInfo(string Path, string Version, bool Complete);
    public record PyInfo(string Exe, string Version);

    static string VsWhere => System.IO.Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
        "Microsoft Visual Studio", "Installer", "vswhere.exe");

    /// <summary>The fast build needs AVX2 + FMA (Intel Haswell / AMD Zen and newer).</summary>
    public static bool HasAvx2 => System.Runtime.Intrinsics.X86.Avx2.IsSupported && Fma.IsSupported;

    /// <summary>The newest VS that has every component, else the newest VS at all.</summary>
    public static VsInfo? FindVs()
    {
        if (!File.Exists(VsWhere)) return null;
        var req = new List<string> { "-latest", "-products", "*", "-requires" };
        req.AddRange(VsComponents);
        req.AddRange(new[] { "-format", "value", "-property", "installationPath" });
        var full = Capture(VsWhere, req).Trim();
        if (full.Length > 0)
        {
            var p = full.Split('\n')[0].Trim();
            return new VsInfo(p, VsVersion(p), true);
        }
        var any = Capture(VsWhere, new[] { "-latest", "-products", "*", "-format", "value", "-property", "installationPath" }).Trim();
        if (any.Length > 0)
        {
            var p = any.Split('\n')[0].Trim();
            return new VsInfo(p, VsVersion(p), false);
        }
        return null;
    }

    static string VsVersion(string path) =>
        Capture(VsWhere, new[] { "-path", path, "-format", "value", "-property", "catalog_productDisplayVersion" }).Trim() is { Length: > 0 } v
            ? v : "?";

    public static PyInfo? FindPython()
    {
        var candidates = new List<(string exe, string[] pre)>
        {
            ("py", new[] { "-3" }),
            ("python", Array.Empty<string>()),
        };
        var local = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "Python");
        if (Directory.Exists(local))
            foreach (var d in Directory.GetDirectories(local, "Python3*").OrderByDescending(x => x))
                candidates.Add((System.IO.Path.Combine(d, "python.exe"), Array.Empty<string>()));

        foreach (var (exe, pre) in candidates)
        {
            var args = new List<string>(pre) { "-c", "import sys;print(sys.version_info[0],sys.version_info[1],sys.executable)" };
            var o = Capture(exe, args).Trim().Split(' ', 3);
            if (o.Length == 3 && int.TryParse(o[0], out var maj) && int.TryParse(o[1], out var min)
                && maj == 3 && min >= 10 && File.Exists(o[2]) && !o[2].Contains("WindowsApps"))
                return new PyInfo(o[2], $"{maj}.{min}");
        }
        return null;
    }

    public static bool HasWinget() => Capture("winget", new[] { "--version" }).StartsWith("v");

    public static string WingetVsArgs()
    {
        var sb = new StringBuilder("--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended");
        foreach (var c in VsComponents) sb.Append(" --add ").Append(c);
        return "install --id Microsoft.VisualStudio.BuildTools -e --source winget " +
               "--accept-package-agreements --accept-source-agreements --override \"" + sb + "\"";
    }

    public static string WingetPythonArgs() =>
        "install --id Python.Python.3.12 -e --source winget --scope user --silent " +
        "--accept-package-agreements --accept-source-agreements";

    /// <summary>Adds the missing components to an existing VS through its own installer (elevated).</summary>
    public static int ModifyVs(string installPath)
    {
        var setup = System.IO.Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
            "Microsoft Visual Studio", "Installer", "setup.exe");
        var args = new StringBuilder($"modify --installPath \"{installPath}\" --passive --norestart");
        foreach (var c in VsComponents) args.Append(" --add ").Append(c);
        return RunElevated(setup, args.ToString());
    }

    public static int RunElevated(string exe, string args)
    {
        var psi = new ProcessStartInfo(exe, args) { UseShellExecute = true, Verb = "runas" };
        using var p = Process.Start(psi)!;
        p.WaitForExit();
        return p.ExitCode;
    }

    public static int RunVisible(string exe, string args)
    {
        var psi = new ProcessStartInfo(exe, args) { UseShellExecute = true };
        using var p = Process.Start(psi)!;
        p.WaitForExit();
        return p.ExitCode;
    }

    /// <summary>Run a tool and return its stdout; "" when it cannot start.</summary>
    public static string Capture(string exe, IEnumerable<string> args)
    {
        try
        {
            var psi = new ProcessStartInfo(exe)
            {
                RedirectStandardOutput = true, RedirectStandardError = true,
                UseShellExecute = false, CreateNoWindow = true,
                StandardOutputEncoding = Encoding.UTF8,
            };
            foreach (var a in args) psi.ArgumentList.Add(a);
            using var p = Process.Start(psi)!;
            var o = p.StandardOutput.ReadToEnd();
            p.WaitForExit(15000);
            return o;
        }
        catch
        {
            return "";
        }
    }

    /// <summary>Common RPCS3 locations, then the folder the user last used.</summary>
    public static string? FindRpcs3()
    {
        var c = new[]
        {
            @"C:\RPCS3\rpcs3.exe",
            System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "RPCS3", "rpcs3.exe"),
            System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "RPCS3", "rpcs3.exe"),
            System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "RPCS3", "rpcs3.exe"),
            System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Desktop), "RPCS3", "rpcs3.exe"),
        };
        return c.FirstOrDefault(File.Exists);
    }
}
