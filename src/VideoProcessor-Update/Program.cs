// Copyright VideoProcessor contributors. Licensed under GPL-3.0; see LICENSE.txt.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Management;
using System.Net;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace VideoProcessor.Update
{
    public static class Program
    {
        [STAThread] public static int Main(string[] args)
        {
            bool service = args.Contains("--service");
            bool control = args.Contains("--control");
            bool background = args.Contains("--background") || service || control;
            try
            {
                if (control) Console.SetOut(new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false)) { AutoFlush = true });
                ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
                Application.EnableVisualStyles();
                string root = AppDomain.CurrentDomain.BaseDirectory;
                int index = Array.IndexOf(args, "--root");
                if (index >= 0 && index + 1 < args.Length) root = args[index + 1];
                root = UpdateCore.CanonicalRoot(root);

                string id = UpdateCore.Hash(Encoding.UTF8.GetBytes(WindowsIdentity.GetCurrent().User.Value + "|" + root.ToLowerInvariant()));
                if (control)
                {
                    int requestIndex = Array.IndexOf(args, "--control");
                    if (requestIndex + 1 >= args.Length) throw new ArgumentException("Missing update command.");
                    UpdateCore.ReadInstalled(root);
                    Console.WriteLine(UpdateControl.Send(id, args[requestIndex + 1])); return 0;
                }
                var installed = UpdateCore.ReadInstalled(root);
                if (!background)
                {
                    Process.Start(new ProcessStartInfo(Path.Combine(root, "config", "VideoProcessorConfig.exe"), "--updates") { UseShellExecute = false, WorkingDirectory = root });
                    return 0;
                }
                string cache = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VideoProcessor", "Updates", id);
                Directory.CreateDirectory(cache);
                if (!args.Contains("--worker"))
                {
                    string runner = Path.Combine(cache, "runner", Guid.NewGuid().ToString("N"));
                    Directory.CreateDirectory(runner);
                    string copy = Path.Combine(runner, "VideoProcessorUpdate.exe");
                    File.Copy(Application.ExecutablePath, copy);
                    Process.Start(new ProcessStartInfo(copy, "--worker --root " + Quote(root) + (background ? " --background" : "") + (service ? " --service" : "")) { UseShellExecute = false, WorkingDirectory = runner });
                    return 0;
                }
                bool created;
                using (var mutex = new Mutex(true, "Local\\VideoProcessor.Update." + id, out created))
                using (var show = new EventWaitHandle(false, EventResetMode.AutoReset, "Local\\VideoProcessor.Update.Show." + id))
                {
                    if (!created)
                    {
                        if (!service) { try { UpdateControl.Send(id, "{\"command\":\"background\"}"); } catch { } }
                        return 0;
                    }
                    try {
                        using (var window = new UpdateWindow(root, cache, installed, background, show, service))
                        using (var channel = new UpdateControl(id, window)) Application.Run(window);
                    }
                    finally { mutex.ReleaseMutex(); }
                }
                return 0;
            }
            catch (Exception ex)
            {
                if (control) Console.WriteLine(UpdateCore.Serialize(new { error = ex.Message, connectionError = ex is IOException || ex is TimeoutException }));
                else if (!background) MessageBox.Show(ex.Message, "VideoProcessor updates", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return 1;
            }
        }
        public static string Quote(string argument)
        {
            var result = new StringBuilder("\""); int slashes = 0;
            foreach (char c in argument)
            {
                if (c == '\\') { slashes++; continue; }
                if (c == '"') { result.Append('\\', slashes * 2 + 1); result.Append(c); }
                else { result.Append('\\', slashes); result.Append(c); }
                slashes = 0;
            }
            return result.Append('\\', slashes * 2).Append('"').ToString();
        }
    }
    public sealed class RunningApp
    {
        public Process process; public string file, arguments; public bool config, restoreInTray;
    }
    public static class AppLifecycle
    {
        delegate bool EnumWindow(IntPtr window, IntPtr parameter);
        [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindow callback, IntPtr parameter);
        [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern uint RegisterWindowMessage(string name);
        [DllImport("user32.dll", SetLastError = true)] static extern IntPtr SendMessageTimeout(IntPtr window, uint message, UIntPtr wparam, IntPtr lparam, uint flags, uint timeout, out UIntPtr result);
        public static List<RunningApp> Find(string root)
        {
            var result = new List<RunningApp>();
            string player = Path.Combine(root, "VideoProcessor.exe");
            string config = Path.Combine(root, "config", "VideoProcessorConfig.exe");
            foreach (var name in new[] { "VideoProcessor", "VideoProcessor-GUI", "VideoProcessorConfig" })
                foreach (var process in Process.GetProcessesByName(name))
                {
                    try
                    {
                        string file = process.MainModule.FileName;
                        if (!string.Equals(file, player, StringComparison.OrdinalIgnoreCase) && !string.Equals(file, config, StringComparison.OrdinalIgnoreCase)) { process.Dispose(); continue; }
                        string arguments = null;
                        using (var item = new ManagementObject("Win32_Process.Handle='" + process.Id + "'"))
                        {
                            string command = item["CommandLine"] as string;
                            if (string.IsNullOrEmpty(command)) throw new IOException("Could not preserve launch arguments for " + name);
                            command = command.TrimStart();
                            int end = command.StartsWith("\"") ? command.IndexOf('"', 1) + 1 : command.IndexOf(' ');
                            arguments = end > 0 ? command.Substring(end).TrimStart() : "";
                        }
                        result.Add(new RunningApp { process = process, file = file, arguments = arguments, config = name == "VideoProcessorConfig" });
                    }
                    catch (InvalidOperationException) { process.Dispose(); }
                    catch (System.ComponentModel.Win32Exception) { process.Dispose(); throw new IOException("Could not inspect a running VP/Config process. Close it and retry the update."); }
                }
            return result;
        }
        public static void Close(List<RunningApp> apps)
        {
            uint message = RegisterWindowMessage("VideoProcessor.UpdateExit.v1");
            if (message == 0) throw new IOException("Could not request application shutdown.");
            foreach (var app in apps.OrderByDescending(a => a.config))
            {
                if (app.process.HasExited) continue;
                bool accepted = false;
                EnumWindows((window, unused) =>
                {
                    uint pid; GetWindowThreadProcessId(window, out pid);
                    if (pid != app.process.Id) return true;
                    UIntPtr result;
                    IntPtr sent = SendMessageTimeout(window, message, app.config ? new UIntPtr(1) : UIntPtr.Zero, IntPtr.Zero, 2, 300000, out result);
                    if (sent != IntPtr.Zero && (result.ToUInt64() == 1 || (app.config && result.ToUInt64() == 3))) { app.restoreInTray = app.config && result.ToUInt64() == 3; accepted = true; return false; }
                    if (sent != IntPtr.Zero && result.ToUInt64() == 2) return false; // User cancelled.
                    return true;
                }, IntPtr.Zero);
                if (!accepted || !app.process.WaitForExit(30000))
                    throw new IOException("Applications did not exit: choose Exit in Config, close VP, then try again. No files were replaced.");
            }
        }
        public static string RestartArguments(RunningApp app)
        {
            return app.arguments + (app.config ? (app.restoreInTray ? " --restore-tray" : " --restore-visible") : "");
        }
        public static void Relaunch(List<RunningApp> apps, string root)
        {
            foreach (var app in apps.OrderBy(a => a.config))
                Process.Start(new ProcessStartInfo(app.file, RestartArguments(app)) { UseShellExecute = false, WorkingDirectory = root });
        }
    }
    public sealed class UpdateWindow : Form
    {
        readonly string root, cache, preferencesPath;
        readonly Installed installed;
        readonly EventWaitHandle showEvent;
        readonly bool background;
        bool hosted;
        Preferences preferences;
        Release release;
        Package package;
        string downloaded;
        bool visibleRequested, busy;
        readonly Label status = new Label();
        readonly TextBox notes = new TextBox();
        readonly ComboBox channel = new ComboBox();
        readonly ComboBox updateMode = new ComboBox();
        readonly CheckBox allowRestart = new CheckBox();
        readonly System.Windows.Forms.Timer autoTimer = new System.Windows.Forms.Timer();
        bool autoPending;
        readonly Button check = new Button(), action = new Button(), skip = new Button();
        readonly ProgressBar progress = new ProgressBar();
        readonly NotifyIcon tray = new NotifyIcon();
        readonly System.Windows.Forms.Timer showTimer = new System.Windows.Forms.Timer();
        public UpdateWindow(string installRoot, string cacheRoot, Installed identity, bool quiet, EventWaitHandle show, bool service = false)
        {
            hosted = service; root = installRoot; cache = cacheRoot; installed = identity; background = quiet; showEvent = show;
            preferencesPath = Path.Combine(cache, "preferences.json");
            try { preferences = File.Exists(preferencesPath) ? UpdateCore.Json<Preferences>(File.ReadAllText(preferencesPath)) : null; } catch { preferences = null; }
            if (preferences == null) preferences = new Preferences { channel = installed.updateChannel == "stable" ? "stable" : "beta" };
            if (preferences.highest == null) preferences.highest = new Dictionary<string, long>();
            if (preferences.channel != "stable" && preferences.channel != "beta") preferences.channel = "beta";
            Text = "VideoProcessor updates — this computer";
            Font = new Font("Segoe UI", 9F);
            ClientSize = new Size(580, 390); MinimumSize = new Size(580, 420);
            StartPosition = FormStartPosition.CenterScreen;
            var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(18), ColumnCount = 1, RowCount = 6 };
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize)); layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 55)); layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 20)); layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            layout.Controls.Add(new Label { Text = (installed.flavor == "full" ? "VideoProcessor and Config" : "VideoProcessor Config") + "  ·  " + installed.coreVersion + "\n" + root, AutoSize = true }, 0, 0);
            var settings = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill };
            settings.Controls.Add(new Label { Text = "Update channel:", AutoSize = true, Padding = new Padding(0, 6, 0, 0) });
            channel.DropDownStyle = ComboBoxStyle.DropDownList; channel.Items.AddRange(new object[] { "Stable", "Beta" }); channel.SelectedIndex = preferences.channel == "stable" ? 0 : 1;
            updateMode.DropDownStyle = ComboBoxStyle.DropDownList; updateMode.Width = 285;
            updateMode.Items.AddRange(new object[] { "Manual checks only", "Notify in tray (no automatic installation)", "Install automatically without prompts" });
            string selectedMode = UpdateCore.UpdateMode(preferences);
            updateMode.SelectedIndex = selectedMode == "manual" ? 0 : selectedMode == "auto" ? 2 : 1;
            allowRestart.Text = "Allow automatic updates to stop playback and restart VP";
            allowRestart.AutoSize = true; allowRestart.Checked = preferences.allowPlaybackRestart;
            allowRestart.Visible = selectedMode == "auto";
            settings.FlowDirection = FlowDirection.TopDown; settings.WrapContents = false;
            settings.Controls.Add(channel); settings.Controls.Add(updateMode); settings.Controls.Add(allowRestart);
            settings.Controls.Add(new Label { AutoSize = true, Text = "Automatic installs close Config and discard unsaved edits.\nOtherwise, automatic installs wait until VP is closed." }); layout.Controls.Add(settings, 0, 1);
            status.Dock = DockStyle.Fill; status.Text = "Ready to check for updates."; status.Padding = new Padding(0, 8, 0, 0); layout.Controls.Add(status, 0, 2);
            notes.Multiline = true; notes.ReadOnly = true; notes.ScrollBars = ScrollBars.Vertical; notes.Dock = DockStyle.Fill; layout.Controls.Add(notes, 0, 3);
            progress.Dock = DockStyle.Fill; layout.Controls.Add(progress, 0, 4);
            var buttons = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill };
            check.Text = "Check now"; check.AutoSize = true; action.Text = "Download update"; action.AutoSize = true; action.Enabled = false;
            skip.Text = "Skip this release"; skip.AutoSize = true; skip.Enabled = false;
            buttons.Controls.Add(check); buttons.Controls.Add(action); buttons.Controls.Add(skip); layout.Controls.Add(buttons, 0, 5); Controls.Add(layout);
            check.Click += async (sender, args) => await Check(false);
            action.Click += async (sender, args) => { if (downloaded == null) await Download(); else await Install(); };
            skip.Click += (sender, args) => { if (release != null) { preferences.skippedSequence = release.sequence; preferences.skippedChannel = release.channel; SavePreferences(); Close(); } };
            updateMode.SelectedIndexChanged += (sender, args) => {
                preferences.mode = updateMode.SelectedIndex == 0 ? "manual" : updateMode.SelectedIndex == 2 ? "auto" : "notify";
                preferences.automatic = preferences.mode != "manual";
                allowRestart.Visible = preferences.mode == "auto";
                autoPending = false; SavePreferences();
                status.Text = "Preference saved. Click Check now to apply it to available updates.";
            };
            allowRestart.CheckedChanged += (sender, args) => { preferences.allowPlaybackRestart = allowRestart.Checked; SavePreferences(); };
            channel.SelectedIndexChanged += (sender, args) => { autoPending = false; preferences.channel = channel.SelectedIndex == 0 ? "stable" : "beta"; release = null; package = null; downloaded = null; action.Enabled = skip.Enabled = false; status.Text = "Click Check now for the selected channel."; SavePreferences(); };
            tray.Icon = SystemIcons.Information; tray.Text = "VideoProcessor update available";
            tray.BalloonTipClicked += (sender, args) => Reveal(); tray.DoubleClick += (sender, args) => Reveal();
            var menu = new ContextMenuStrip(); menu.Items.Add("View update", null, (sender, args) => Reveal()); menu.Items.Add("Later", null, (sender, args) => Close()); tray.ContextMenuStrip = menu;
            showTimer.Interval = 500; showTimer.Tick += (sender, args) => { if (showEvent.WaitOne(0)) Reveal(); }; showTimer.Start();
            autoTimer.Interval = 60000; autoTimer.Tick += async (sender, args) => { if (autoPending && !busy) await TryAutomatic(); else if (!hosted && !busy && release == null) Close(); }; autoTimer.Start();
            FormClosing += (sender, args) => { if (busy) { args.Cancel = true; status.Text = "Wait for the current operation to finish."; } };
            visibleRequested = false;
            // Start even when SetVisibleCore keeps the background window hidden.
            var startup = new System.Windows.Forms.Timer { Interval = 500 };
            startup.Tick += async (sender, args) => { startup.Stop(); startup.Dispose(); if (!service) await Check(quiet); }; startup.Start();
        }
        protected override void SetVisibleCore(bool value) { base.SetVisibleCore(value && visibleRequested); }
        protected override void Dispose(bool disposing)
        {
            if (disposing) { tray.Dispose(); showTimer.Dispose(); autoTimer.Dispose(); } base.Dispose(disposing);
        }
        void Reveal()
        {
            string config = Path.Combine(root, "config", "VideoProcessorConfig.exe");
            if (File.Exists(config)) Process.Start(new ProcessStartInfo(config, "--updates") { UseShellExecute = false, WorkingDirectory = root });
        }
        public string ControlRequest(string json)
        {
            var request = UpdateCore.Json<UpdateCommand>(json);
            if (request == null) throw new InvalidDataException("Invalid update request.");
            if (request.command != "detach" && request.command != "background") hosted = true;
            if (request.command != "state" && request.command != "detach" && request.command != "background" && busy) throw new InvalidOperationException("An update operation is already running.");
            switch (request.command)
            {
                case "state": break;
                case "background": if (!busy && !autoPending) RunControl("background"); break;
                case "detach":
                    hosted = false;
                    if (!busy && !autoPending && release == null)
                    {
                        var exit = new System.Windows.Forms.Timer { Interval = 200 };
                        exit.Tick += (sender, args) => { exit.Stop(); exit.Dispose(); if (!hosted && !busy && !autoPending) Close(); }; exit.Start();
                    }
                    break;
                case "settings":
                    if ((request.mode != "manual" && request.mode != "notify" && request.mode != "auto") ||
                        (request.channel != "stable" && request.channel != "beta")) throw new InvalidDataException("Invalid update preferences.");
                    bool channelChanged = preferences.channel != request.channel;
                    preferences.mode = request.mode; preferences.automatic = request.mode != "manual";
                    preferences.channel = request.channel; preferences.allowPlaybackRestart = request.allowPlaybackRestart;
                    autoPending = false;
                    if (channelChanged) { release = null; package = null; downloaded = null; }
                    SavePreferences(); status.Text = "Update preferences saved."; break;
                case "check": RunControl("check"); break;
                case "download":
                    if (package == null || downloaded != null) throw new InvalidOperationException("Check for an available update first.");
                    RunControl("download"); break;
                case "install":
                    if (downloaded == null) throw new InvalidOperationException("Download the update first.");
                    RunControl("install"); break;
                case "skip":
                    if (release == null) throw new InvalidOperationException("No release to skip.");
                    autoPending = false; preferences.skippedSequence = release.sequence; preferences.skippedChannel = release.channel;
                    SavePreferences(); status.Text = "Release skipped."; break;
                default: throw new InvalidDataException("Unknown update command.");
            }
            return UpdateCore.Serialize(new { status = status.Text, busy, mode = UpdateCore.UpdateMode(preferences),
                channel = preferences.channel, allowPlaybackRestart = preferences.allowPlaybackRestart,
                flavor = installed.flavor, version = installed.coreVersion, availableVersion = release == null ? null : release.version,
                notes = notes.Text, progress = progress.Value, canDownload = !busy && package != null && downloaded == null,
                canInstall = !busy && downloaded != null, canSkip = !busy && release != null, root });
        }
        async void RunControl(string command)
        {
            try {
                if (command == "background") await Check(true);
                else if (command == "check") await Check(false);
                else if (command == "download") await Download();
                else if (command == "install") await Install(false, true);
            } catch (Exception ex) { status.Text = ex.Message; Busy(false); }
        }
        void SavePreferences() { UpdateCore.Save(preferencesPath, preferences); }
        void Busy(bool value)
        {
            if (IsDisposed) return;
            busy = value; check.Enabled = channel.Enabled = updateMode.Enabled = allowRestart.Enabled = !value;
            action.Enabled = !value && package != null; skip.Enabled = !value && release != null;
        }
        async Task Check(bool quiet)
        {
            if (busy) return;
            DateTime last;
            if (quiet && (UpdateCore.UpdateMode(preferences) == "manual" || (DateTime.TryParse(preferences.lastCheckUtc, out last) && DateTime.UtcNow - last.ToUniversalTime() < TimeSpan.FromDays(1)))) { if (!hosted) Close(); return; }
            autoPending = false;
            Busy(true); status.Text = "Checking GitHub for signed releases…";
            try
            {
                UpdateCore.IsPortableRoot(root, installed.flavor);
                preferences.lastCheckUtc = DateTime.UtcNow.ToString("o"); SavePreferences();
                release = await Task.Run(() => UpdateCore.Discover(installed, preferences));
                SavePreferences(); package = release == null ? null : release.packages.First(p => p.flavor == installed.flavor);
                downloaded = null; action.Text = "Download update";
                if (release == null) { status.Text = "No newer signed release is available on this channel."; }
                else
                {
                    status.Text = "Available: " + release.version + "  ·  build " + release.sequence + "\nInstalling will close the local applications and restart them.";
                    notes.Text = release.notes + "\r\n\r\nRemote computers update separately.";
                    if (release.rpcVersion != installed.rpcVersion || release.configurationVersion != installed.configurationVersion)
                        notes.Text += "\r\nThis release changes remote compatibility. Update VP and Config on your other computers to the same release before connecting them.";
                }
                if (quiet && !hosted && !visibleRequested)
                {
                    if (release == null || (preferences.skippedSequence == release.sequence && preferences.skippedChannel == release.channel)) { Busy(false); Close(); return; }
                    tray.Visible = true; tray.ShowBalloonTip(10000, "VideoProcessor update available", release.version + (UpdateCore.UpdateMode(preferences) == "auto" ? " is queued for automatic installation." : " is ready to download. Click to review."), ToolTipIcon.Info);
                }
            }
            catch (Exception ex)
            {
                package = null; release = null; status.Text = ex.Message;
                if (quiet && !hosted && !visibleRequested) { File.WriteAllText(Path.Combine(cache, "last-error.txt"), DateTime.UtcNow.ToString("o") + " " + ex.Message); Busy(false); Close(); return; }
            }
            finally { Busy(false); }
            if (!IsDisposed && release != null && UpdateCore.UpdateMode(preferences) == "auto")
            { autoPending = true; await TryAutomatic(); }
        }
        void Notify(string title, string message)
        {
            tray.Visible = true; tray.ShowBalloonTip(10000, title, message, ToolTipIcon.Info);
        }
        async Task TryAutomatic()
        {
            if (busy || !autoPending) return;
            Busy(true);
            try
            {
                bool playerRunning = await Task.Run(() => {
                    var apps = AppLifecycle.Find(root);
                    try { return apps.Any(a => !a.config && !a.process.HasExited); }
                    finally { foreach (var app in apps) app.process.Dispose(); }
                });
                if (!UpdateCore.CanAutoInstall(preferences, release, installed, playerRunning))
                {
                    if (release == null || release.rpcVersion != installed.rpcVersion || release.configurationVersion != installed.configurationVersion) autoPending = false;
                    status.Text = playerRunning && !preferences.allowPlaybackRestart
                        ? "Update queued. Waiting for VP to close."
                        : "This release requires manual review of remote compatibility.";
                    return;
                }
                if (downloaded == null) await Download();
                if (downloaded == null) { autoPending = false; Notify("Update download failed", status.Text); return; }
                Notify("Installing VideoProcessor update", "Config will close and restart. Unsaved edits are discarded.");
                await Install(true);
            }
            catch (Exception ex) { autoPending = false; status.Text = ex.Message; Notify("Update needs attention", ex.Message); }
            finally { Busy(false); }
        }
        async Task Download()
        {
            Busy(true); status.Text = "Downloading verified update…"; progress.Value = 0;
            try
            {
                string directory = Path.Combine(cache, "downloads", Guid.NewGuid().ToString("N")); Directory.CreateDirectory(directory);
                string destination = Path.Combine(directory, "VideoProcessorSetup.exe");
                await Task.Run(() => UpdateCore.Download(package, destination, percent => BeginInvoke((Action)(() => progress.Value = percent))));
                downloaded = destination; action.Text = "Install and restart";
                status.Text = "Download verified. Ready to install on this computer.";
            }
            catch (Exception ex) { status.Text = ex.Message; }
            finally { Busy(false); }
        }
        async Task Install(bool unattended = false, bool confirmed = false)
        {
            if (!unattended && !confirmed && MessageBox.Show(this, "Install " + release.version + " on this computer?\n\nPlayback will stop. Config will close and discard unsaved edits. The local applications will restart after installation. Remote computers are not updated.", "Install update", MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return;
            Busy(true); status.Text = "Preparing installation. Closing Config and VP.";
            try
            {
                await Task.Run(() =>
                {
                    bool portable = UpdateCore.IsPortableRoot(root, installed.flavor);
                    // Keep a non-write-sharing handle open through execution, including app shutdown.
                    using (var locked = new FileStream(downloaded, FileMode.Open, FileAccess.Read, FileShare.Read))
                    {
                        if (locked.Length != package.size || !string.Equals(UpdateCore.FileHash(downloaded), package.sha256, StringComparison.OrdinalIgnoreCase))
                            throw new InvalidDataException("Downloaded installer changed. Download it again.");
                        using (var gate = new FileStream(UpdateCore.SafeFile(root, ".vp-update.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.Read))
                        {
                        var apps = AppLifecycle.Find(root);
                        try
                        {
                            if (unattended && !UpdateCore.CanAutoInstall(preferences, release, installed, apps.Any(a => !a.config && !a.process.HasExited)))
                                throw new InvalidOperationException("Automatic update deferred because VP started or the update preference changed.");
                            AppLifecycle.Close(apps);
                            var remaining = AppLifecycle.Find(root);
                            try { if (remaining.Count != 0) throw new IOException("An application restarted before setup. Close it and retry."); }
                            finally { foreach (var app in remaining) app.process.Dispose(); }
                            string log = Path.Combine(Path.GetDirectoryName(downloaded), "setup.log");
                            string args = (portable ? "/PORTABLEUPDATE=1 " : "") + (unattended ? "/VERYSILENT /SUPPRESSMSGBOXES " : "/SILENT ") + "/SP- /NORESTART /NORESTARTAPPLICATIONS /NOCLOSEAPPLICATIONS /DIR=" + Program.Quote(root) + " /LOG=" + Program.Quote(log);
                            using (var setup = Process.Start(new ProcessStartInfo(downloaded, args) { UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(downloaded) }))
                            {
                                setup.WaitForExit();
                                if (setup.ExitCode != 0) throw new IOException("Setup did not complete (" + setup.ExitCode + "). Check " + log + ". Rerun setup to recover before starting VP.");
                            }
                            UpdateCore.VerifyInstalled(root, release, package);
                            gate.Dispose();
                            AppLifecycle.Relaunch(apps, root);
                        }
                        finally { foreach (var app in apps) app.process.Dispose(); }
                        }
                    }
                });
                autoPending = false; Busy(false); Close();
            }
            catch (Exception ex) { autoPending = false; status.Text = ex.Message; if (unattended) Notify("Update needs attention", ex.Message); }
            finally { Busy(false); }
        }
    }
}
