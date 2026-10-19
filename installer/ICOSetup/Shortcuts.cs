namespace ICOSetup;

static class Shortcuts
{
    /// <summary>Creates a .lnk through the Windows Script Host COM object.</summary>
    public static void Create(string lnk, string target)
    {
        var type = Type.GetTypeFromProgID("WScript.Shell") ?? throw new InvalidOperationException("WScript.Shell is unavailable");
        dynamic shell = Activator.CreateInstance(type)!;
        dynamic s = shell.CreateShortcut(lnk);
        s.TargetPath = target;
        s.WorkingDirectory = Path.GetDirectoryName(target);
        s.Description = "ICO (PC port)";
        s.IconLocation = target + ",0";
        s.Save();
    }
}
