namespace ICOSetup;

static class Program
{
    /// <summary>
    /// ICO.PC.Port.Setup.exe [--source &lt;repo checkout&gt;] [--ps3recomp &lt;dir&gt;]
    /// The options are for development: a release build carries its own payload.
    /// </summary>
    [STAThread]
    static void Main(string[] args)
    {
        string? source = null, ps3recomp = null;
        for (int i = 0; i + 1 < args.Length; i++)
        {
            if (args[i] == "--source") source = Path.GetFullPath(args[++i]);
            else if (args[i] == "--ps3recomp") ps3recomp = Path.GetFullPath(args[++i]);
        }
        ApplicationConfiguration.Initialize();
        Application.Run(new MainForm(source, ps3recomp));
    }
}
