using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using VideoProcessor.Update;

public static class UpdateHandoffFixture
{
    [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    static extern uint RegisterWindowMessage(string message);

    sealed class FixturePlayerForm : System.Windows.Forms.Form
    {
        readonly uint exitMessage = RegisterWindowMessage("VideoProcessor.UpdateExit.v1");
        public FixturePlayerForm()
        {
            ShowInTaskbar = false;
            FormBorderStyle = System.Windows.Forms.FormBorderStyle.FixedToolWindow;
            Opacity = 0;
            WindowState = System.Windows.Forms.FormWindowState.Minimized;
        }
        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            BeginInvoke(new System.Windows.Forms.MethodInvoker(Hide));
        }
        protected override void WndProc(ref System.Windows.Forms.Message message)
        {
            if (exitMessage != 0 && message.Msg == exitMessage)
            {
                message.Result = new IntPtr(1);
                BeginInvoke(new System.Windows.Forms.MethodInvoker(Close));
                return;
            }
            base.WndProc(ref message);
        }
    }
    static readonly JavaScriptSerializer Json = new JavaScriptSerializer { MaxJsonLength = 16777216, RecursionLimit = 32 };
    static readonly UTF8Encoding Utf8 = new UTF8Encoding(false, true);

    static Dictionary<string, object> ReadObject(string path)
    {
        return Json.Deserialize<Dictionary<string, object>>(File.ReadAllText(path, Utf8));
    }

    static string Text(IDictionary<string, object> value, string key)
    {
        object result;
        return value.TryGetValue(key, out result) && result != null ? Convert.ToString(result) : "";
    }

    static int Number(IDictionary<string, object> value, string key)
    {
        object result;
        return value.TryGetValue(key, out result) && result != null ? Convert.ToInt32(result) : 0;
    }

    static bool Flag(IDictionary<string, object> value, string key)
    {
        object result;
        return value.TryGetValue(key, out result) && result != null && Convert.ToBoolean(result);
    }

    static void ConfigureTestHooks(string specPath)
    {
#if VP_TEST_HANDOFF
        var spec = ReadObject(specPath);
        InstallHandoff.TestHandoffRoot = Text(spec, "handoffRoot");
        InstallHandoff.TestCheckpoint = delegate(string phase, string directory)
        {
            string selectedPhase = Text(spec, "checkpointPhase");
            if (!string.Equals(phase, selectedPhase, StringComparison.Ordinal)) return;
            string action = Text(spec, "checkpointAction");
            if (action == "tamper-setup")
                File.WriteAllText(Path.Combine(directory, "VideoProcessorSetup.exe"), "tampered staged setup");
            else if (action == "throw")
                throw new IOException("Fixture rejected readiness.");
            else if (action == "sleep")
                Thread.Sleep(Number(spec, "checkpointDelayMs"));
        };
#endif
    }

    static string Image(Process process)
    {
        try { return Path.GetFullPath(process.MainModule.FileName); }
        catch { return ""; }
    }

    static Process FindImage(string expected)
    {
        foreach (Process process in Process.GetProcessesByName("VideoProcessorUpdate"))
        {
            try
            {
                if (String.Equals(Image(process), Path.GetFullPath(expected), StringComparison.OrdinalIgnoreCase))
                    return process;
            }
            catch { process.Dispose(); }
            if (!String.Equals(Image(process), Path.GetFullPath(expected), StringComparison.OrdinalIgnoreCase))
                process.Dispose();
        }
        return null;
    }

    static void WriteText(string path, string value)
    {
        string temp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        File.WriteAllText(temp, value, new UTF8Encoding(false));
        if (File.Exists(path)) File.Replace(temp, path, null);
        else File.Move(temp, path);
    }

    static int RunFixturePlayer(string specPath)
    {
        var spec = ReadObject(specPath);
        string countPath = Text(spec, "playerStartCountPath");
        int count = 0;
        if (File.Exists(countPath)) Int32.TryParse(File.ReadAllText(countPath).Trim(), out count);
        WriteText(countPath, (count + 1).ToString());
        System.Windows.Forms.Application.Run(new FixturePlayerForm());
        return 0;
    }

    static int RunParent(string specPath)
    {
        Mutex coordinator = null;
        bool owns = false;
        try
        {
            var spec = ReadObject(specPath);
            ConfigureTestHooks(specPath);
            string root = Path.GetFullPath(Text(spec, "installRoot")).TrimEnd(Path.DirectorySeparatorChar);
            string sid = WindowsIdentity.GetCurrent().User.Value;
            string rootId = UpdateCore.Hash(Encoding.UTF8.GetBytes(sid + "|" + root.ToLowerInvariant()));
            coordinator = new Mutex(false, InstallHandoff.CoordinatorMutexName(rootId));
            try { owns = coordinator.WaitOne(0); }
            catch (AbandonedMutexException) { owns = true; }
            if (!owns) throw new IOException("Fixture could not acquire the updater coordinator mutex.");

            byte[] envelope = File.ReadAllBytes(Text(spec, "envelopePath"));
            Release release = UpdateCore.Verify(Utf8.GetString(envelope), UpdateCore.PublicKey(), DateTime.UtcNow);
            Installed installed = UpdateCore.ReadInstalled(root);
            Package package = release.packages.FirstOrDefault(value => value.flavor == installed.flavor);
            if (package == null) throw new InvalidDataException("Fixture package does not match its installed flavor.");

            InstallHandoff.StartCoordinator(root, installed, envelope, package, Text(spec, "installerPath"), Flag(spec, "unattended"));
            WriteText(Text(spec, "parentReadyPath"), Process.GetCurrentProcess().Id.ToString());
            int delay = Number(spec, "delayAfterReadyMs");
            if (delay > 0) Thread.Sleep(delay);
            return 0;
        }
        catch (Exception ex)
        {
            string path = "";
            try { path = Text(ReadObject(specPath), "parentErrorPath"); } catch { }
            if (!String.IsNullOrEmpty(path)) try { WriteText(path, ex.ToString()); } catch { }
            Console.Error.WriteLine(ex);
            return 21;
        }
        finally
        {
            if (owns && coordinator != null) try { coordinator.ReleaseMutex(); } catch { }
            if (coordinator != null) coordinator.Dispose();
        }
    }

    static string InstallRootArgument(string[] args)
    {
        foreach (string argument in args)
            if (argument.StartsWith("/DIR=", StringComparison.OrdinalIgnoreCase))
                return argument.Substring(5).Trim().Trim('"');
        throw new InvalidDataException("Fake setup did not receive /DIR.");
    }

    static int RunFakeSetup(string[] args)
    {
        string specPath = Environment.GetEnvironmentVariable("VP_HANDOFF_FIXTURE_SPEC");
        if (String.IsNullOrEmpty(specPath)) throw new InvalidDataException("Fake setup has no isolated fixture spec.");
        var spec = ReadObject(specPath);
        string root = Path.GetFullPath(InstallRootArgument(args)).TrimEnd(Path.DirectorySeparatorChar);
        if (!String.Equals(root, Path.GetFullPath(Text(spec, "installRoot")).TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Fake setup received a different installation folder.");

        string countPath = Text(spec, "setupCountPath");
        Directory.CreateDirectory(Path.GetDirectoryName(countPath));
        int count = 0;
        if (File.Exists(countPath)) Int32.TryParse(File.ReadAllText(countPath).Trim(), out count);
        WriteText(countPath, (count + 1).ToString());

        string stagedUpdater = Path.Combine(Environment.CurrentDirectory, "VideoProcessorUpdate.exe");
        Process child = FindImage(stagedUpdater);
        if (child == null) throw new IOException("The staged updater child was not alive while setup ran.");
        int childPid = child.Id;
        long childStart = child.StartTime.ToUniversalTime().Ticks;
        string childImage = Image(child);

        string logPath = "";
        foreach (string argument in args)
            if (argument.StartsWith("/LOG=", StringComparison.OrdinalIgnoreCase)) logPath = argument.Substring(5).Trim().Trim('"');
        if (!String.IsNullOrEmpty(logPath)) File.WriteAllText(logPath, "fixture setup completed" + Environment.NewLine, Utf8);

        string rootUpdater = Path.Combine(root, "VideoProcessorUpdate.exe");
        string replacement = Path.Combine(root, "VideoProcessorUpdate.exe.fixture-replacement");
        int setupExitCode = Number(spec, "setupExitCode");
        if (setupExitCode == 0)
        {
            File.WriteAllBytes(replacement, Convert.FromBase64String(Text(spec, "replacementUpdaterBase64")));
            File.Replace(replacement, rootUpdater, null);
            WriteText(Path.Combine(root, "INSTALL-MANIFEST.json"),
                Utf8.GetString(Convert.FromBase64String(Text(spec, "newManifestBase64"))));
        }

        child.Refresh();
        bool stillAlive = !child.HasExited && child.StartTime.ToUniversalTime().Ticks == childStart &&
            String.Equals(Image(child), childImage, StringComparison.OrdinalIgnoreCase) &&
            String.Equals(childImage, Path.GetFullPath(stagedUpdater), StringComparison.OrdinalIgnoreCase);
        string observation = Json.Serialize(new
        {
            setupPid = Process.GetCurrentProcess().Id,
            setupImage = Image(Process.GetCurrentProcess()),
            childPid,
            childStart,
            childImage,
            childAliveAfterInstalledUpdaterReplacement = stillAlive,
            replacementHash = UpdateCore.FileHash(rootUpdater),
            setupCount = count + 1
        });
        WriteText(Text(spec, "setupObservationPath"), observation);
        Thread.Sleep(2500);
        child.Dispose();
        return !stillAlive ? 23 : setupExitCode;
    }

    [STAThread] public static int Main(string[] args)
    {
        string image = Path.GetFileName(Process.GetCurrentProcess().MainModule.FileName);
        if (String.Equals(image, "VideoProcessorSetup.exe", StringComparison.OrdinalIgnoreCase))
            return RunFakeSetup(args);
        if (String.Equals(image, "VideoProcessor.exe", StringComparison.OrdinalIgnoreCase) && args.Length == 2 && args[0] == "--fixture-player")
            return RunFixturePlayer(args[1]);
        if (args.Length == 2 && args[0] == "--fixture-cleanup")
        {
            ConfigureTestHooks(args[1]);
            InstallHandoff.CleanupTerminalHandoffs();
            var spec = ReadObject(args[1]);
            WriteText(Text(spec, "cleanupCompletePath"), "complete");
            return 0;
        }
        if (args.Contains("--install-child"))
        {
            string specPath = Environment.GetEnvironmentVariable("VP_HANDOFF_FIXTURE_SPEC");
            if (!String.IsNullOrEmpty(specPath)) ConfigureTestHooks(specPath);
            return Program.Main(args);
        }
        if (args.Length == 2 && args[0] == "--fixture-parent")
            return RunParent(args[1]);
        Console.Error.WriteLine("Unexpected handoff fixture entry point.");
        return 2;
    }
}
