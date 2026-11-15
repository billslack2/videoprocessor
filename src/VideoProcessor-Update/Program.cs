// Copyright VideoProcessor contributors. Licensed under GPL-3.0; see LICENSE.txt.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Pipes;
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
            if (InstallHandoff.HasInstallChildArguments(args)) return InstallHandoff.RunChild(args);
            bool service = args.Contains("--service");
            bool control = args.Contains("--control");
            bool controlSession = args.Contains("--control-session");
            bool background = args.Contains("--background") || service || control || controlSession;
            try
            {
                if (control || controlSession) Console.SetOut(new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false, true)) { AutoFlush = true });
                ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
                Application.EnableVisualStyles();
                string root = AppDomain.CurrentDomain.BaseDirectory;
                int index = Array.IndexOf(args, "--root");
                if (index >= 0 && index + 1 < args.Length) root = args[index + 1];
                root = UpdateCore.CanonicalRoot(root);

                string id = UpdateCore.Hash(Encoding.UTF8.GetBytes(WindowsIdentity.GetCurrent().User.Value + "|" + root.ToLowerInvariant()));
                InstallHandoff.EnsureNoInstallInProgress(id);
                InstallHandoff.CleanupTerminalHandoffs();
                if (controlSession) { UpdateCore.ReadInstalled(root); UpdateControl.RunSession(id); return 0; }
                if (control)
                {
                    int requestIndex = Array.IndexOf(args, "--control");
                    if (requestIndex + 1 >= args.Length) throw new ArgumentException("Missing update command.");
                    try { Console.WriteLine(UpdateControl.Send(id, args[requestIndex + 1])); return 0; }
                    catch { UpdateCore.ReadInstalled(root); throw; }
                }
                if (!background)
                {
                    UpdateCore.ReadInstalled(root);
                    Process.Start(new ProcessStartInfo(Path.Combine(root, "config", "VideoProcessorConfig.exe"), "--updates") { UseShellExecute = false, WorkingDirectory = root });
                    return 0;
                }
                string cache = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VideoProcessor", "Updates", id);
                if (!args.Contains("--worker"))
                {
                    StartWorker(id, root, service, args.Contains("--background"));
                    return 0;
                }
                Directory.CreateDirectory(cache);
                var installed = UpdateCore.ReadInstalled(root);
                bool ownsMutex = false;
                using (var mutex = new Mutex(false, "Local\\VideoProcessor.Update." + id))
                using (var show = new EventWaitHandle(false, EventResetMode.AutoReset, "Local\\VideoProcessor.Update.Show." + id))
                {
                    try { ownsMutex = mutex.WaitOne(0); }
                    catch (AbandonedMutexException) { ownsMutex = true; }
                    if (!ownsMutex)
                    {
                        try { UpdateControl.Send(id, service ? "{\"command\":\"state\"}" : "{\"command\":\"background\"}"); } catch { }
                        return 0;
                    }
                    try {
                        using (var window = new UpdateWindow(root, cache, installed, background, show, service))
                        using (var channel = new UpdateControl(id, window)) Application.Run(window);
                    }
                    finally { if (ownsMutex) mutex.ReleaseMutex(); }
                }
                return 0;
            }
            catch (Exception ex)
            {
                if (control || controlSession) Console.WriteLine(UpdateCore.Serialize(new { error = ex.Message, connectionError = ex is IOException || ex is TimeoutException }));
                else if (!background) MessageBox.Show(ex.Message, "VideoProcessor updates", MessageBoxButtons.OK, MessageBoxIcon.Information);
                else Console.Error.WriteLine(ex.ToString());
                return 1;
            }
        }

        static void StartWorker(string id, string root, bool service, bool requestedBackground)
        {
            string command = service ? "{\"command\":\"state\"}" : "{\"command\":\"background\"}";
            if (TrySend(id, command)) return;

            bool ownsStartGate = false;
            using (var startGate = new Mutex(false, "Local\\VideoProcessor.Update.Start." + id))
            {
                try { ownsStartGate = startGate.WaitOne(0); }
                catch (AbandonedMutexException) { ownsStartGate = true; }
                if (!ownsStartGate)
                {
                    DateTime deadline = DateTime.UtcNow.AddSeconds(8);
                    while (DateTime.UtcNow < deadline)
                    {
                        if (TrySend(id, command)) return;
                        Thread.Sleep(100);
                    }
                    throw new IOException("The updater worker is starting but did not become available.");
                }

                try
                {
                    if (TrySend(id, command)) return;
                    InstallHandoff.EnsureNoInstallInProgress(id);
                    string workerArgs = "--worker --root " + Quote(root);
                    if (requestedBackground || service) workerArgs += " --background";
                    if (service) workerArgs += " --service";
                    using (var worker = Process.Start(new ProcessStartInfo(Application.ExecutablePath, workerArgs)
                    { UseShellExecute = false, WorkingDirectory = root, CreateNoWindow = true, RedirectStandardError = true }))
                    {
                        if (worker == null) throw new IOException("Could not start the updater worker.");
                        var workerDiagnostics = new StringBuilder();
                        worker.ErrorDataReceived += (sender, args) => { if (!string.IsNullOrEmpty(args.Data)) lock (workerDiagnostics) workerDiagnostics.AppendLine(args.Data); };
                        worker.BeginErrorReadLine();
                        DateTime deadline = DateTime.UtcNow.AddSeconds(8);
                        while (DateTime.UtcNow < deadline)
                        {
                            if (UpdateControl.Probe(id, 250)) return;
                            if (worker.HasExited)
                            {
                                string diagnostic;
                                lock (workerDiagnostics) diagnostic = workerDiagnostics.ToString().Trim();
                                throw new IOException("The updater worker exited during startup." + (string.IsNullOrWhiteSpace(diagnostic) ? "" : " " + diagnostic));
                            }
                            Thread.Sleep(100);
                        }
                        worker.WaitForExit(250);
                        string details;
                        lock (workerDiagnostics) details = workerDiagnostics.ToString().Trim();
                        throw new IOException(worker.HasExited ? "The updater worker exited during startup." : "The updater worker did not become available." +
                            (string.IsNullOrWhiteSpace(details) ? "" : " " + details));
                    }
                }
                finally { if (ownsStartGate) startGate.ReleaseMutex(); }
            }
        }

        static bool TrySend(string id, string command)
        {
            try { UpdateControl.Send(id, command); return true; }
            catch (IOException) { return false; }
            catch (TimeoutException) { return false; }
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
        public static void RelaunchClosed(List<RunningApp> apps, string root)
        {
            var active = Find(root);
            try
            {
            foreach (var app in apps.OrderBy(a => a.config))
                if (app.process.HasExited && !active.Any(current => string.Equals(current.file, app.file, StringComparison.OrdinalIgnoreCase)))
                    Process.Start(new ProcessStartInfo(app.file, RestartArguments(app)) { UseShellExecute = false, WorkingDirectory = root });
            }
            finally { foreach (var app in active) app.process.Dispose(); }
        }
    }
    public sealed class UpdateWindow : Form
    {
        readonly string root, cache, preferencesPath;
        readonly Installed installed;
        readonly EventWaitHandle showEvent;
        readonly bool background;
        readonly Dictionary<string, NamedPipeServerStream> clientSessions = new Dictionary<string, NamedPipeServerStream>(StringComparer.Ordinal);
        bool hosted { get { return clientSessions.Count != 0; } }
        readonly bool serviceWorker;
        DateTime serviceGraceUntil = DateTime.MinValue;
        Preferences preferences;
        Release release;
        byte[] signedEnvelope;
        Package package;
        string downloaded;
        bool visibleRequested, busy;
        readonly Label status = new Label();
        readonly TextBox notes = new TextBox();
        readonly ComboBox channel = new ComboBox();
        readonly ComboBox updateMode = new ComboBox();
        readonly CheckBox allowRestart = new CheckBox();
        readonly System.Windows.Forms.Timer autoTimer = new System.Windows.Forms.Timer();
        readonly System.Windows.Forms.Timer serviceGrace = new System.Windows.Forms.Timer();
        bool autoPending;
        readonly Button check = new Button(), action = new Button(), skip = new Button();
        readonly ProgressBar progress = new ProgressBar();
        readonly NotifyIcon tray = new NotifyIcon();
        readonly System.Windows.Forms.Timer showTimer = new System.Windows.Forms.Timer();
        public UpdateWindow(string installRoot, string cacheRoot, Installed identity, bool quiet, EventWaitHandle show, bool service = false)
        {
            serviceWorker = service; root = installRoot; cache = cacheRoot; installed = identity; background = quiet; showEvent = show;
            if (serviceWorker) serviceGraceUntil = DateTime.UtcNow.AddSeconds(10);
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
            channel.SelectedIndexChanged += (sender, args) => { autoPending = false; preferences.channel = channel.SelectedIndex == 0 ? "stable" : "beta"; release = null; signedEnvelope = null; package = null; downloaded = null; action.Enabled = skip.Enabled = false; status.Text = "Click Check now for the selected channel."; SavePreferences(); };
            tray.Icon = SystemIcons.Information; tray.Text = "VideoProcessor update available";
            tray.BalloonTipClicked += (sender, args) => Reveal(); tray.DoubleClick += (sender, args) => Reveal();
            var menu = new ContextMenuStrip(); menu.Items.Add("View update", null, (sender, args) => Reveal()); menu.Items.Add("Later", null, (sender, args) => Close()); tray.ContextMenuStrip = menu;
            showTimer.Interval = 500; showTimer.Tick += (sender, args) => { if (showEvent.WaitOne(0)) Reveal(); }; showTimer.Start();
            autoTimer.Interval = 1000; autoTimer.Tick += async (sender, args) => {
                if (autoPending && !busy) await TryAutomatic();
                else if (!hosted && !busy && !autoPending && DateTime.UtcNow >= serviceGraceUntil && !KeepBackgroundNotification()) Close();
            }; autoTimer.Start();
            serviceGrace.Interval = 10000;
            serviceGrace.Tick += (sender, args) => { serviceGrace.Stop(); if (!hosted && !busy && !autoPending && !KeepBackgroundNotification()) Close(); };
            if (serviceWorker) serviceGrace.Start();
            FormClosing += (sender, args) => { if (busy) { args.Cancel = true; status.Text = "Wait for the current operation to finish."; } };
            visibleRequested = false;
            // Start even when SetVisibleCore keeps the background window hidden.
            var startup = new System.Windows.Forms.Timer { Interval = 500 };
            startup.Tick += async (sender, args) => { startup.Stop(); startup.Dispose(); if (!service) await Check(quiet); }; startup.Start();
        }
        protected override void SetVisibleCore(bool value) { base.SetVisibleCore(value && visibleRequested); }
        protected override void Dispose(bool disposing)
        {
            if (disposing) { tray.Dispose(); showTimer.Dispose(); autoTimer.Dispose(); serviceGrace.Dispose(); } base.Dispose(disposing);
        }
        void Reveal()
        {
            string config = Path.Combine(root, "config", "VideoProcessorConfig.exe");
            if (File.Exists(config)) Process.Start(new ProcessStartInfo(config, "--updates") { UseShellExecute = false, WorkingDirectory = root });
        }
        bool KeepBackgroundNotification()
        {
            return background && !serviceWorker && release != null && tray.Visible;
        }
        void ScheduleIdleClose()
        {
            if (!hosted && !busy && !autoPending && !KeepBackgroundNotification() && DateTime.UtcNow >= serviceGraceUntil)
            {
                var exit = new System.Windows.Forms.Timer { Interval = 250 };
                exit.Tick += (sender, args) => { exit.Stop(); exit.Dispose(); if (!hosted && !busy && !autoPending && !KeepBackgroundNotification()) Close(); };
                exit.Start();
            }
        }
        public void ControlSessionDisconnected(string sessionId, NamedPipeServerStream pipe)
        {
            NamedPipeServerStream current;
            if (clientSessions.TryGetValue(sessionId, out current) && ReferenceEquals(current, pipe))
                clientSessions.Remove(sessionId);
            ScheduleIdleClose();
        }
        public string ControlRequest(string json, NamedPipeServerStream pipe)
        {
            var request = UpdateCore.Json<UpdateCommand>(json);
            if (request == null) throw new InvalidDataException("Invalid update request.");
            if (request.command != "state" && request.command != "detach" && request.command != "attach" && request.command != "background" && busy) throw new InvalidOperationException("An update operation is already running.");
            switch (request.command)
            {
                case "state": break;
                case "attach":
                    Guid session;
                    if (!Guid.TryParseExact(request.sessionId, "N", out session)) throw new InvalidDataException("Update control session identity is invalid.");
                    NamedPipeServerStream existing;
                    if (clientSessions.TryGetValue(request.sessionId, out existing) && !ReferenceEquals(existing, pipe))
                        throw new InvalidDataException("Update control session is already attached to another connection.");
                    clientSessions[request.sessionId] = pipe;
                    serviceGrace.Stop(); serviceGraceUntil = DateTime.MinValue;
                    break;
                case "background": if (!busy && !autoPending) RunControl("background"); break;
                case "detach":
                    if (string.IsNullOrEmpty(request.sessionId)) throw new InvalidDataException("Update control session identity is missing.");
                    ControlSessionDisconnected(request.sessionId, pipe);
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
                var discovered = await Task.Run(() => UpdateCore.DiscoverVerified(installed, preferences));
                release = discovered.release; signedEnvelope = discovered.signedEnvelope;
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
                package = null; release = null; signedEnvelope = null; status.Text = ex.Message;
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
                await Task.Run(() => InstallHandoff.StartCoordinator(root, installed, signedEnvelope, package, downloaded, unattended));
                autoPending = false; Busy(false); Close();
            }
            catch (Exception ex) { autoPending = false; status.Text = ex.Message; if (unattended) Notify("Update needs attention", ex.Message); }
            finally { Busy(false); }
        }
    }
}
