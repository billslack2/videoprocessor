// Copyright VideoProcessor contributors. Licensed under GPL-3.0; see LICENSE.txt.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using Microsoft.Win32.SafeHandles;
using System.Runtime.InteropServices;

namespace VideoProcessor.Update
{
    public sealed class InstallHandoffRecord
    {
        public int schemaVersion;
        public string root, rootSha256, installedManifestSha256, updaterSha256, envelopeSha256, installerSha256;
        public string nonce, createdUtc;
        public long sequence, installerSize, parentPid, parentStartFileTime;
        public bool unattended;
    }

    public static class InstallHandoff
    {
        const int HandshakeLimit = 1024;
#if VP_TEST_HANDOFF
        const int HandshakeTimeoutMs = 5000;
        const int ParentExitTimeoutMs = 3000;
        const int GateTimeoutMs = 3000;
        public static string TestHandoffRoot;
        public static Action<string, string> TestCheckpoint;
#else
        const int HandshakeTimeoutMs = 10000;
        const int ParentExitTimeoutMs = 30000;
        const int GateTimeoutMs = 10000;
#endif
        static readonly UTF8Encoding StrictUtf8 = new UTF8Encoding(false, true);

        static void Checkpoint(string name, string directory)
        {
#if VP_TEST_HANDOFF
            var callback = TestCheckpoint;
            if (callback != null) callback(name, directory);
#endif
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool GetNamedPipeClientProcessId(SafePipeHandle pipe, out uint clientProcessId);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool GetProcessTimes(IntPtr process, out System.Runtime.InteropServices.ComTypes.FILETIME creation,
            out System.Runtime.InteropServices.ComTypes.FILETIME exit, out System.Runtime.InteropServices.ComTypes.FILETIME kernel,
            out System.Runtime.InteropServices.ComTypes.FILETIME user);

        public static string InstallMutexName(string id) { return "Local\\VideoProcessor.Update.Install." + id; }
        public static string CoordinatorMutexName(string id) { return "Local\\VideoProcessor.Update." + id; }
        public static string StartMutexName(string id) { return "Local\\VideoProcessor.Update.Start." + id; }

        static string CacheRoot(string id)
        {
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VideoProcessor", "Updates", id);
        }
        static string HandoffRoot()
        {
#if VP_TEST_HANDOFF
            if (!string.IsNullOrWhiteSpace(TestHandoffRoot)) return Path.GetFullPath(TestHandoffRoot);
#endif
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VideoProcessor", "Updates", "handoffs");
        }
        static string HandoffPath(string handoffId) { return Path.Combine(HandoffRoot(), handoffId); }
        static string PipeName(string handoffId, string nonce) { return "VideoProcessor.Update.Handoff." + handoffId + "." + nonce; }

        public static bool HasInstallChildArguments(string[] args) { return Array.IndexOf(args, "--install-child") >= 0; }

        public static void EnsureNoInstallInProgress(string id)
        {
            Mutex gate;
            try { gate = Mutex.OpenExisting(InstallMutexName(id)); }
            catch (WaitHandleCannotBeOpenedException) { return; }
            using (gate)
            {
                bool owns = false;
                try { owns = gate.WaitOne(0); }
                catch (AbandonedMutexException) { owns = true; }
                if (!owns) throw new IOException("An update is installing. Wait for setup to finish, then reconnect.");
                gate.ReleaseMutex();
            }
        }

        static long FileTime(Process process)
        {
            System.Runtime.InteropServices.ComTypes.FILETIME created, exited, kernel, user;
            if (!GetProcessTimes(process.Handle, out created, out exited, out kernel, out user))
                throw new IOException("Could not verify an updater process identity.", Marshal.GetExceptionForHR(Marshal.GetHRForLastWin32Error()));
            return ((long)(uint)created.dwHighDateTime << 32) | (uint)created.dwLowDateTime;
        }

        static string ProcessImage(Process process)
        {
            try { return Path.GetFullPath(process.MainModule.FileName); }
            catch (Exception ex) { throw new IOException("Could not verify the updater process image.", ex); }
        }

        static string CurrentSid() { return WindowsIdentity.GetCurrent().User.Value; }

        static string CurrentRootId(string root)
        {
            return UpdateCore.Hash(Encoding.UTF8.GetBytes(CurrentSid() + "|" + root.ToLowerInvariant()));
        }

        static string ContentId(string root, string updater, string envelope, string installer, long sequence)
        {
            string identity = root.ToLowerInvariant() + "|" + updater.ToLowerInvariant() + "|" + envelope.ToLowerInvariant() +
                "|" + installer.ToLowerInvariant() + "|" + sequence.ToString(System.Globalization.CultureInfo.InvariantCulture);
            return UpdateCore.Hash(Encoding.UTF8.GetBytes(identity));
        }

        static void EnsureNoReparseAncestors(string path)
        {
            string full = Path.GetFullPath(path);
            for (DirectoryInfo current = new DirectoryInfo(full); current != null; current = current.Parent)
                if (current.Exists && (current.Attributes & FileAttributes.ReparsePoint) != 0)
                    throw new InvalidDataException("Update handoff cannot use a linked directory.");
        }

        static void EnsureOrdinaryFile(string path)
        {
            if (!File.Exists(path) || (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException("Update handoff file is missing or linked.");
            EnsureNoReparseAncestors(Path.GetDirectoryName(Path.GetFullPath(path)));
        }

        static void VerifyFileOwnerAndAcl(string path, string sid, bool allowSecureInheritedAcl = false)
        {
            var security = File.GetAccessControl(path, AccessControlSections.Owner | AccessControlSections.Access);
            if (!security.GetOwner(typeof(SecurityIdentifier)).Value.Equals(sid, StringComparison.OrdinalIgnoreCase) ||
                (!security.AreAccessRulesProtected && !allowSecureInheritedAcl))
                throw new UnauthorizedAccessException("Update handoff file ownership or permissions changed.");
            var user = new SecurityIdentifier(sid);
            var system = new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null);
            foreach (FileSystemAccessRule rule in security.GetAccessRules(true, true, typeof(SecurityIdentifier)))
            {
                var ruleSid = (SecurityIdentifier)rule.IdentityReference;
                if (rule.AccessControlType == AccessControlType.Allow && !ruleSid.Equals(user) && !ruleSid.Equals(system))
                    throw new UnauthorizedAccessException("Update handoff file is accessible to another account.");
            }
        }

        static void VerifyInventory(string directory, string sid, params string[] allowedFiles)
        {
            var allowed = new HashSet<string>(allowedFiles, StringComparer.OrdinalIgnoreCase);
            foreach (string entry in Directory.GetFileSystemEntries(directory))
            {
                string name = Path.GetFileName(entry);
                if (!allowed.Contains(name)) throw new InvalidDataException("Update handoff contains an unrecognized file.");
                if ((File.GetAttributes(entry) & FileAttributes.ReparsePoint) != 0)
                    throw new InvalidDataException("Update handoff contains a linked file.");
                if (!File.Exists(entry) || Directory.Exists(entry)) throw new InvalidDataException("Update handoff inventory must contain regular files only.");
                // Inno creates setup.log itself, so it inherits the already
                // protected handoff directory ACL instead of applying a
                // protected file ACL. Its owner and every effective allow ACE
                // are still checked against the current user and SYSTEM.
                VerifyFileOwnerAndAcl(entry, sid, string.Equals(name, "setup.log", StringComparison.OrdinalIgnoreCase));
            }
        }

        static string HashStream(Stream stream)
        {
            long position = stream.CanSeek ? stream.Position : 0;
            if (stream.CanSeek) stream.Position = 0;
            using (var sha = SHA256.Create())
                try { return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant(); }
                finally { if (stream.CanSeek) stream.Position = position; }
        }

        static byte[] ReadStreamBounded(Stream stream, int limit)
        {
            if (stream.CanSeek)
            {
                if (stream.Length > limit) throw new InvalidDataException("Update handoff file is too large.");
                stream.Position = 0;
            }
            using (var output = new MemoryStream())
            {
                byte[] buffer = new byte[8192]; int read;
                while ((read = stream.Read(buffer, 0, buffer.Length)) > 0)
                {
                    if (output.Length + read > limit) throw new InvalidDataException("Update handoff file is too large.");
                    output.Write(buffer, 0, read);
                }
                if (stream.CanSeek) stream.Position = 0;
                return output.ToArray();
            }
        }

        static DirectorySecurity SecureDirectory(string sid)
        {
            var security = new DirectorySecurity();
            security.SetAccessRuleProtection(true, false);
            var user = new SecurityIdentifier(sid);
            var system = new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null);
            security.SetOwner(user);
            security.AddAccessRule(new FileSystemAccessRule(user, FileSystemRights.FullControl,
                InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit, PropagationFlags.None, AccessControlType.Allow));
            security.AddAccessRule(new FileSystemAccessRule(system, FileSystemRights.FullControl,
                InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit, PropagationFlags.None, AccessControlType.Allow));
            return security;
        }

        static FileSecurity SecureFile(string sid)
        {
            var security = new FileSecurity();
            security.SetAccessRuleProtection(true, false);
            var user = new SecurityIdentifier(sid);
            var system = new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null);
            security.SetOwner(user);
            security.AddAccessRule(new FileSystemAccessRule(user, FileSystemRights.FullControl, AccessControlType.Allow));
            security.AddAccessRule(new FileSystemAccessRule(system, FileSystemRights.FullControl, AccessControlType.Allow));
            return security;
        }

        static void VerifyOwnerAndAcl(DirectoryInfo directory, string sid)
        {
            var security = directory.GetAccessControl(AccessControlSections.Owner | AccessControlSections.Access);
            if (!security.GetOwner(typeof(SecurityIdentifier)).Value.Equals(sid, StringComparison.OrdinalIgnoreCase) || !security.AreAccessRulesProtected)
                throw new UnauthorizedAccessException("Update handoff folder ownership or permissions changed.");
            var system = new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null);
            foreach (FileSystemAccessRule rule in security.GetAccessRules(true, true, typeof(SecurityIdentifier)))
            {
                var ruleSid = (SecurityIdentifier)rule.IdentityReference;
                if (rule.AccessControlType == AccessControlType.Allow && !ruleSid.Equals(new SecurityIdentifier(sid)) && !ruleSid.Equals(system))
                    throw new UnauthorizedAccessException("Update handoff folder is accessible to another account.");
            }
        }

        static void PrepareHandoffDirectory(string directory, string sid)
        {
            EnsureNoReparseAncestors(directory);
            string parent = Path.GetDirectoryName(directory);
            Directory.CreateDirectory(parent);
            var parentInfo = new DirectoryInfo(parent);
            parentInfo.SetAccessControl(SecureDirectory(sid));
            VerifyOwnerAndAcl(parentInfo, sid);
            Directory.CreateDirectory(directory);
            EnsureNoReparseAncestors(directory);
            var info = new DirectoryInfo(directory);
            info.SetAccessControl(SecureDirectory(sid));
            VerifyOwnerAndAcl(info, sid);
        }

        static void VerifyHandoffDirectory(string directory, string sid)
        {
            EnsureNoReparseAncestors(directory);
            VerifyOwnerAndAcl(new DirectoryInfo(Path.GetDirectoryName(directory)), sid);
            VerifyOwnerAndAcl(new DirectoryInfo(directory), sid);
        }

        static void WriteNew(string path, byte[] bytes, string sid)
        {
            using (var output = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.Read))
            {
                output.Write(bytes, 0, bytes.Length);
                output.Flush(true);
            }
            File.SetAccessControl(path, SecureFile(sid));
        }

        static byte[] ReadBounded(string path, int limit)
        {
            using (var input = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
            {
                if (input.Length > limit) throw new InvalidDataException("Update handoff file is too large.");
                byte[] bytes = new byte[checked((int)input.Length)];
                int offset = 0;
                while (offset < bytes.Length)
                {
                    int read = input.Read(bytes, offset, bytes.Length - offset);
                    if (read == 0) throw new IOException("Update handoff file ended unexpectedly.");
                    offset += read;
                }
                return bytes;
            }
        }

        static void DeleteStagedDirectory(string path)
        {
            if (!Directory.Exists(path)) return;
            EnsureNoReparseAncestors(path);
            var known = new HashSet<string>(new[] { "VideoProcessorUpdate.exe", "vp-update.json", "VideoProcessorSetup.exe", "handoff.json", "result.json", "setup.log" }, StringComparer.OrdinalIgnoreCase);
            var entries = Directory.GetFileSystemEntries(path);
            foreach (var entry in entries)
            {
                if (!known.Contains(Path.GetFileName(entry)) || !File.Exists(entry) || Directory.Exists(entry) ||
                    (File.GetAttributes(entry) & FileAttributes.ReparsePoint) != 0)
                    throw new InvalidDataException("Refusing to remove an unknown or linked handoff path.");
            }
            // Validate that no process still has any staged file open before
            // removing the first one. The install gate excludes the legitimate
            // child; exclusive opens also catch a leftover process or handle.
            foreach (var entry in entries)
                using (new FileStream(entry, FileMode.Open, FileAccess.Read, FileShare.None)) { }
            foreach (var entry in entries) File.Delete(entry);
            Directory.Delete(path, false);
        }

        static void WriteDurableDiagnostic(string cache, string name, byte[] bytes, string sid)
        {
            Directory.CreateDirectory(cache);
            EnsureNoReparseAncestors(cache);
            var directory = new DirectoryInfo(cache);
            directory.SetAccessControl(SecureDirectory(sid));
            VerifyOwnerAndAcl(directory, sid);
            string target = Path.GetFullPath(Path.Combine(cache, name));
            if (!string.Equals(Path.GetDirectoryName(target), Path.GetFullPath(cache).TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Diagnostic path escaped the updater cache.");
            if (File.Exists(target))
            {
                EnsureOrdinaryFile(target);
                VerifyFileOwnerAndAcl(target, sid);
            }
            string temporary = target + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try
            {
                using (var output = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                    output.Write(bytes, 0, bytes.Length);
                File.SetAccessControl(temporary, SecureFile(sid));
                if (File.Exists(target)) File.Replace(temporary, target, null);
                else File.Move(temporary, target);
            }
            finally { try { if (File.Exists(temporary)) File.Delete(temporary); } catch { } }
        }

        static byte[] ReadDiagnosticTail(string path, int limit)
        {
            using (var input = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
            {
                long start = Math.Max(0, input.Length - limit);
                input.Position = start;
                byte[] bytes = new byte[checked((int)(input.Length - start))];
                int offset = 0;
                while (offset < bytes.Length)
                {
                    int read = input.Read(bytes, offset, bytes.Length - offset);
                    if (read == 0) throw new IOException("Setup diagnostic ended unexpectedly.");
                    offset += read;
                }
                return bytes;
            }
        }

        static bool TryOwnInstallGate(string rootId, out Mutex gate)
        {
            gate = new Mutex(false, InstallMutexName(rootId));
            try { return gate.WaitOne(0); }
            catch (AbandonedMutexException) { return true; }
        }

        public static void CleanupTerminalHandoffs()
        {
            string root = HandoffRoot();
            if (!Directory.Exists(root)) return;
            EnsureNoReparseAncestors(root);
            foreach (string directory in Directory.GetDirectories(root, "*", SearchOption.TopDirectoryOnly))
            {
                string id = Path.GetFileName(directory);
                if (id.Length != 64 || !id.All(Uri.IsHexDigit)) continue;
                try
                {
                    string sid = CurrentSid();
                    VerifyHandoffDirectory(directory, sid);
                    string recordPath = Path.Combine(directory, "handoff.json");
                    string resultPath = Path.Combine(directory, "result.json");
                    InstallHandoffRecord record = null;
                    if (File.Exists(recordPath))
                    {
                        VerifyInventory(directory, sid, "VideoProcessorUpdate.exe", "vp-update.json", "VideoProcessorSetup.exe", "handoff.json", "result.json", "setup.log");
                        record = UpdateCore.Json<InstallHandoffRecord>(StrictUtf8.GetString(ReadBounded(recordPath, UpdateCore.MetadataLimit)));
                    }
                    bool terminal = File.Exists(resultPath);
                    bool expired = false;
                    string rootId = null;
                    if (record != null && !string.IsNullOrEmpty(record.root))
                    {
                        string installRoot = Path.GetFullPath(record.root).TrimEnd(Path.DirectorySeparatorChar);
                        rootId = CurrentRootId(installRoot);
                        DateTime created;
                        if (DateTime.TryParse(record.createdUtc, System.Globalization.CultureInfo.InvariantCulture,
                            System.Globalization.DateTimeStyles.AdjustToUniversal | System.Globalization.DateTimeStyles.AssumeUniversal, out created))
                            expired = DateTime.UtcNow - created.ToUniversalTime() > TimeSpan.FromMinutes(10);
                    }
                    else expired = DateTime.UtcNow - Directory.GetLastWriteTimeUtc(directory) > TimeSpan.FromDays(1);
                    if (!terminal && !expired) continue;
                    if (rootId != null)
                    {
                        Mutex installGate;
                        if (!TryOwnInstallGate(rootId, out installGate)) { installGate.Dispose(); continue; }
                        using (installGate)
                        {
                            try
                            {
                                if (terminal && File.Exists(resultPath))
                                {
                                    string cache = CacheRoot(rootId);
                                    WriteDurableDiagnostic(cache, "last-install-result.json",
                                        ReadBounded(resultPath, UpdateCore.MetadataLimit), sid);
                                    string setupLog = Path.Combine(directory, "setup.log");
                                    if (File.Exists(setupLog))
                                        WriteDurableDiagnostic(cache, "last-install-setup.log", ReadDiagnosticTail(setupLog, 16 * 1024 * 1024), sid);
                                }
                                DeleteStagedDirectory(directory);
                            }
                            finally { try { installGate.ReleaseMutex(); } catch { } }
                        }
                    }
                    else DeleteStagedDirectory(directory);
                }
                catch { /* Unknown, active, malformed, or linked artifacts are preserved. */ }
            }
        }

        static void VerifyCurrentUpdater(string root, Installed installed, string executable, out string hash)
        {
            string path = UpdateCore.SafeFile(root, "VideoProcessorUpdate.exe");
            if (!string.Equals(Path.GetFullPath(path), Path.GetFullPath(executable), StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The running updater is not in this installation folder.");
            var entry = installed.files.FirstOrDefault(file => string.Equals(file.path, "VideoProcessorUpdate.exe", StringComparison.OrdinalIgnoreCase));
            if (entry == null || entry.policy != "managed" || !UpdateCore.IsHash(entry.sha256))
                throw new InvalidDataException("The installed manifest does not manage the updater executable.");
            hash = UpdateCore.FileHash(path);
            if (!string.Equals(hash, entry.sha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The installed updater does not match its package manifest.");
        }

        static void VerifyRequest(string root, Installed installed, byte[] signedEnvelope, Package selectedPackage,
            string installerPath, out Release release, out Package package, out string updaterHash)
        {
            if (signedEnvelope == null || signedEnvelope.Length == 0 || signedEnvelope.Length > UpdateCore.MetadataLimit)
                throw new InvalidDataException("The original signed release envelope is unavailable.");
            release = UpdateCore.Verify(StrictUtf8.GetString(signedEnvelope), UpdateCore.PublicKey(), DateTime.UtcNow);
            package = release.packages.FirstOrDefault(value => value.flavor == installed.flavor);
            if (package == null || selectedPackage == null ||
                !string.Equals(package.sha256, selectedPackage.sha256, StringComparison.OrdinalIgnoreCase) || package.size != selectedPackage.size ||
                !string.Equals(package.installManifestSha256, selectedPackage.installManifestSha256, StringComparison.OrdinalIgnoreCase) ||
                !string.Equals(package.url, selectedPackage.url, StringComparison.Ordinal))
                throw new InvalidDataException("The selected installer does not match the signed release envelope.");
            if (!string.Equals(package.flavor, installed.flavor, StringComparison.Ordinal) || release.sequence <= installed.updateSequence)
                throw new InvalidDataException("The signed update does not match this installation or is no longer newer.");
            var current = UpdateCore.ReadInstalled(root);
            if (current.applicationId != installed.applicationId || current.flavor != installed.flavor || current.updateSequence != installed.updateSequence)
                throw new InvalidDataException("The installation identity changed before setup started.");
            string installerHash = UpdateCore.FileHash(installerPath);
            if (!string.Equals(installerHash, package.sha256, StringComparison.OrdinalIgnoreCase) || new FileInfo(installerPath).Length != package.size)
                throw new InvalidDataException("Downloaded installer changed. Download it again.");
            VerifyCurrentUpdater(root, current, Process.GetCurrentProcess().MainModule.FileName, out updaterHash);
        }

        static void VerifyPipeClient(NamedPipeServerStream pipe, string sid)
        {
            string peerSid = null;
            pipe.RunAsClient(() =>
            {
                using (var peer = WindowsIdentity.GetCurrent())
                    if (peer.User != null) peerSid = peer.User.Value;
            });
            if (!string.Equals(peerSid, sid, StringComparison.OrdinalIgnoreCase))
                throw new UnauthorizedAccessException("Update handoff client is not the current user.");
        }

        static string ReadHandshake(Stream stream)
        {
            using (var bytes = new MemoryStream())
            {
                while (true)
                {
                    int value = stream.ReadByte();
                    if (value < 0) throw new IOException("Update handoff closed before readiness.");
                    if (value == 10) return StrictUtf8.GetString(bytes.ToArray());
                    if (bytes.Length >= HandshakeLimit) throw new InvalidDataException("Update handoff response is too large.");
                    bytes.WriteByte((byte)value);
                }
            }
        }

        static void WriteHandshake(Stream stream, string text)
        {
            byte[] payload = StrictUtf8.GetBytes(text + "\n");
            if (payload.Length > HandshakeLimit) throw new InvalidDataException("Update handoff response is too large.");
            stream.Write(payload, 0, payload.Length);
            stream.Flush();
        }

        static NamedPipeServerStream CreateHandshakeServer(string pipeName, string sid)
        {
            var security = new PipeSecurity();
            security.SetAccessRuleProtection(true, false);
            security.AddAccessRule(new PipeAccessRule(new SecurityIdentifier(WellKnownSidType.NetworkSid, null), PipeAccessRights.FullControl, AccessControlType.Deny));
            security.AddAccessRule(new PipeAccessRule(new SecurityIdentifier(sid), PipeAccessRights.FullControl, AccessControlType.Allow));
            return new NamedPipeServerStream(pipeName, PipeDirection.InOut, 1, PipeTransmissionMode.Byte,
                PipeOptions.Asynchronous, 1024, 1024, security);
        }

        static void SendChildError(string id, string nonce, string message)
        {
            try
            {
                using (var pipe = new NamedPipeClientStream(".", PipeName(id, nonce), PipeDirection.Out,
                    PipeOptions.Asynchronous, TokenImpersonationLevel.Impersonation))
                {
                    pipe.Connect(1000);
                    string encoded = Convert.ToBase64String(StrictUtf8.GetBytes(message ?? "Update handoff failed."));
                    WriteHandshake(pipe, "ERROR|" + nonce + "|" + encoded);
                }
            }
            catch { }
        }

        static string ParseArgument(string[] args, string name)
        {
            int index = Array.IndexOf(args, name);
            if (index < 0 || index + 1 >= args.Length) throw new ArgumentException("Missing " + name + " argument.");
            return args[index + 1];
        }

        public static void StartCoordinator(string root, Installed installed, byte[] signedEnvelope, Package selectedPackage,
            string downloadedInstaller, bool unattended)
        {
            root = UpdateCore.CanonicalRoot(root);
            Release release; Package package; string updaterHash;
            VerifyRequest(root, installed, signedEnvelope, selectedPackage, downloadedInstaller, out release, out package, out updaterHash);
            string sid = CurrentSid();
            string envelopeHash = UpdateCore.Hash(signedEnvelope);
            string handoffId = ContentId(root, updaterHash, envelopeHash, package.sha256, release.sequence);
            string rootId = CurrentRootId(root);
            string directory = HandoffPath(handoffId);
            string stagedUpdater = Path.Combine(directory, "VideoProcessorUpdate.exe");
            string stagedEnvelope = Path.Combine(directory, "vp-update.json");
            string stagedInstaller = Path.Combine(directory, "VideoProcessorSetup.exe");
            string recordPath = Path.Combine(directory, "handoff.json");
            string resultPath = Path.Combine(directory, "result.json");
            string nonce = Guid.NewGuid().ToString("N");
            Process parent = Process.GetCurrentProcess();
            long parentStart = FileTime(parent);
            var record = new InstallHandoffRecord
            {
                schemaVersion = 1, root = root,
                rootSha256 = UpdateCore.Hash(Encoding.UTF8.GetBytes(root.ToLowerInvariant())),
                installedManifestSha256 = UpdateCore.FileHash(UpdateCore.SafeFile(root, "INSTALL-MANIFEST.json")),
                updaterSha256 = updaterHash, envelopeSha256 = envelopeHash, installerSha256 = package.sha256,
                nonce = nonce, createdUtc = DateTime.UtcNow.ToString("o"), sequence = release.sequence,
                installerSize = package.size, parentPid = parent.Id, parentStartFileTime = parentStart, unattended = unattended
            };

            bool accepted = false;
            bool createdDirectory = !Directory.Exists(directory);
            Process child = null;
            using (var server = CreateHandshakeServer(PipeName(handoffId, nonce), sid))
            {
                PrepareHandoffDirectory(directory, sid);
                try
                {
                    if (File.Exists(stagedUpdater) || File.Exists(stagedEnvelope) || File.Exists(stagedInstaller) || File.Exists(recordPath) || File.Exists(resultPath))
                        throw new InvalidDataException("A previous update handoff is stale. No setup was started; retry after its diagnostics are reviewed.");
                    File.Copy(Process.GetCurrentProcess().MainModule.FileName, stagedUpdater, false);
                    if (!string.Equals(UpdateCore.FileHash(stagedUpdater), updaterHash, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidDataException("Staged updater copy failed verification.");
                    File.SetAccessControl(stagedUpdater, SecureFile(sid));
                    WriteNew(stagedEnvelope, signedEnvelope, sid);
                    using (var input = new FileStream(downloadedInstaller, FileMode.Open, FileAccess.Read, FileShare.Read))
                    using (var output = new FileStream(stagedInstaller, FileMode.CreateNew, FileAccess.Write, FileShare.Read))
                    {
                        input.CopyTo(output); output.Flush(true);
                    }
                    File.SetAccessControl(stagedInstaller, SecureFile(sid));
                    if (new FileInfo(stagedInstaller).Length != package.size || !string.Equals(UpdateCore.FileHash(stagedInstaller), package.sha256, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidDataException("Staged setup does not match the signed installer hash.");
                    WriteNew(recordPath, Encoding.UTF8.GetBytes(UpdateCore.Serialize(record)), sid);
                    VerifyHandoffDirectory(directory, sid);
                    VerifyInventory(directory, sid, "VideoProcessorUpdate.exe", "vp-update.json", "VideoProcessorSetup.exe", "handoff.json", "result.json", "setup.log");

                    Checkpoint("staged-before-parent-locks", directory);

                    using (var updaterLock = new FileStream(stagedUpdater, FileMode.Open, FileAccess.Read, FileShare.Read))
                    using (var envelopeLock = new FileStream(stagedEnvelope, FileMode.Open, FileAccess.Read, FileShare.Read))
                    using (var installerLock = new FileStream(stagedInstaller, FileMode.Open, FileAccess.Read, FileShare.Read))
                    using (var handoffRecordLock = new FileStream(recordPath, FileMode.Open, FileAccess.Read, FileShare.Read))
                    {
                        if (!string.Equals(HashStream(updaterLock), updaterHash, StringComparison.OrdinalIgnoreCase) ||
                            !string.Equals(HashStream(envelopeLock), envelopeHash, StringComparison.OrdinalIgnoreCase) ||
                            !string.Equals(HashStream(installerLock), package.sha256, StringComparison.OrdinalIgnoreCase))
                            throw new InvalidDataException("Staged handoff contents failed verification.");

                        using (var startup = new Mutex(false, StartMutexName(rootId)))
                        {
                            bool ownsStart = false;
                            try { ownsStart = startup.WaitOne(HandshakeTimeoutMs); }
                            catch (AbandonedMutexException) { ownsStart = true; }
                            if (!ownsStart) throw new TimeoutException("Updater startup is busy; no setup was started.");
                            try
                            {
                                EnsureNoInstallInProgress(rootId);
                                string args = "--install-child " + Program.Quote(handoffId) + " --nonce " + Program.Quote(nonce) +
                                    " --parent-pid " + parent.Id.ToString(System.Globalization.CultureInfo.InvariantCulture) +
                                    " --parent-start " + parentStart.ToString(System.Globalization.CultureInfo.InvariantCulture);
                                child = Process.Start(new ProcessStartInfo(stagedUpdater, args)
                                    { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = directory });
                                if (child == null) throw new IOException("Could not start the staged updater handoff.");
                            }
                            finally { if (ownsStart) startup.ReleaseMutex(); }
                        }
                        Task connect = server.WaitForConnectionAsync();
                        if (!connect.Wait(HandshakeTimeoutMs)) throw new TimeoutException("Staged updater did not become ready; no setup was started.");
                        connect.GetAwaiter().GetResult();
                        Task<string> response = Task.Run(() => ReadHandshake(server));
                        if (!response.Wait(HandshakeTimeoutMs)) throw new TimeoutException("Staged updater readiness timed out; no setup was started.");
                        VerifyPipeClient(server, sid);
                        uint clientPid;
                        if (!GetNamedPipeClientProcessId(server.SafePipeHandle, out clientPid) || clientPid != child.Id)
                            throw new UnauthorizedAccessException("Update handoff readiness came from a different process.");
                        using (var exactChild = Process.GetProcessById(child.Id))
                        {
                            long childStart = FileTime(exactChild);
                            if (!string.Equals(ProcessImage(exactChild), Path.GetFullPath(stagedUpdater), StringComparison.OrdinalIgnoreCase))
                                throw new InvalidDataException("Update handoff process image did not match its staged copy.");
                            string[] fields = response.Result.Split('|');
                            if (fields.Length == 3 && fields[0] == "ERROR" && fields[1] == nonce)
                                throw new IOException("Staged updater rejected the handoff: " + StrictUtf8.GetString(Convert.FromBase64String(fields[2])));
                            if (fields.Length != 4 || fields[0] != "READY" || fields[1] != nonce ||
                                fields[2] != child.Id.ToString(System.Globalization.CultureInfo.InvariantCulture) ||
                                fields[3] != childStart.ToString(System.Globalization.CultureInfo.InvariantCulture))
                                throw new UnauthorizedAccessException("Staged updater readiness identity did not match.");
                        }
                        accepted = true;
                    }
                }
                catch
                {
                    if (child != null)
                    {
                        try { if (!child.HasExited) child.Kill(); child.WaitForExit(2000); } catch { }
                    }
                    if (!accepted && createdDirectory) DeleteStagedDirectory(directory);
                    throw;
                }
            }
        }

        static void VerifyParent(InstallHandoffRecord record, string nonce, Process parent, string root)
        {
            if (parent.Id != record.parentPid || FileTime(parent) != record.parentStartFileTime ||
                !string.Equals(nonce, record.nonce, StringComparison.Ordinal))
                throw new InvalidDataException("Update handoff parent identity is stale.");
            string parentImage = ProcessImage(parent);
            string expected = UpdateCore.SafeFile(root, "VideoProcessorUpdate.exe");
            if (!string.Equals(parentImage, expected, StringComparison.OrdinalIgnoreCase) ||
                !string.Equals(UpdateCore.FileHash(parentImage), record.updaterSha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Update handoff parent image is not the installed updater.");
        }

        static bool WriteResult(string path, string status, string message)
        {
            try
            {
                UpdateCore.Save(path, new { schemaVersion = 1, status, message, completedUtc = DateTime.UtcNow.ToString("o") });
                File.SetAccessControl(path, SecureFile(CurrentSid()));
                return true;
            }
            catch (Exception ex)
            {
                try { Console.Error.WriteLine("Could not persist update handoff result: " + ex); } catch { }
                return false;
            }
        }

        static void ChildWork(string id, string nonce, int parentPid, long parentStart)
        {
            string sid = CurrentSid();
            string handoffDirectory = HandoffPath(id);
            string recordPath = Path.Combine(handoffDirectory, "handoff.json");
            string handshakeName = PipeName(id, nonce);
            bool readySent = false, setupStarted = false, resultWritten = false;
            string resultPath = Path.Combine(handoffDirectory, "result.json");
            List<RunningApp> apps = null;
            try
            {
                VerifyHandoffDirectory(handoffDirectory, sid);
                VerifyInventory(handoffDirectory, sid, "VideoProcessorUpdate.exe", "vp-update.json", "VideoProcessorSetup.exe", "handoff.json", "result.json", "setup.log");
                var record = UpdateCore.Json<InstallHandoffRecord>(StrictUtf8.GetString(ReadBounded(recordPath, UpdateCore.MetadataLimit)));
                if (record == null || record.schemaVersion != 1 || record.parentPid != parentPid || record.parentStartFileTime != parentStart ||
                    record.nonce != nonce || DateTime.UtcNow - DateTime.Parse(record.createdUtc, System.Globalization.CultureInfo.InvariantCulture,
                        System.Globalization.DateTimeStyles.AdjustToUniversal | System.Globalization.DateTimeStyles.AssumeUniversal) > TimeSpan.FromMinutes(10))
                    throw new InvalidDataException("Update handoff record is stale or invalid.");
                string root = UpdateCore.CanonicalRoot(record.root);
                if (!string.Equals(record.rootSha256, UpdateCore.Hash(Encoding.UTF8.GetBytes(root.ToLowerInvariant())), StringComparison.OrdinalIgnoreCase) ||
                    !string.Equals(ContentId(root, record.updaterSha256, record.envelopeSha256, record.installerSha256, record.sequence), id, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("Update handoff root or content identity does not match.");
                string rootId = CurrentRootId(root);
                if (!string.Equals(HandoffPath(id), Path.GetFullPath(handoffDirectory), StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("Update handoff is not in this user's root-specific cache.");
                string stagedUpdater = Path.Combine(handoffDirectory, "VideoProcessorUpdate.exe");
                string stagedEnvelope = Path.Combine(handoffDirectory, "vp-update.json");
                string stagedInstaller = Path.Combine(handoffDirectory, "VideoProcessorSetup.exe");
                using (var stagedUpdaterLock = new FileStream(stagedUpdater, FileMode.Open, FileAccess.Read, FileShare.Read))
                using (var envelopeLock = new FileStream(stagedEnvelope, FileMode.Open, FileAccess.Read, FileShare.Read))
                using (var installerLock = new FileStream(stagedInstaller, FileMode.Open, FileAccess.Read, FileShare.Read))
                using (var recordLock = new FileStream(recordPath, FileMode.Open, FileAccess.Read, FileShare.Read))
                {
                if (!string.Equals(HashStream(stagedUpdaterLock), record.updaterSha256, StringComparison.OrdinalIgnoreCase) ||
                    !string.Equals(HashStream(envelopeLock), record.envelopeSha256, StringComparison.OrdinalIgnoreCase) ||
                    !string.Equals(HashStream(installerLock), record.installerSha256, StringComparison.OrdinalIgnoreCase) ||
                    installerLock.Length != record.installerSize ||
                    !string.Equals(ProcessImage(Process.GetCurrentProcess()), Path.GetFullPath(stagedUpdater), StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("A staged handoff file changed; setup was not started.");
                var envelopeBytes = ReadStreamBounded(envelopeLock, UpdateCore.MetadataLimit);
                Release release = UpdateCore.Verify(StrictUtf8.GetString(envelopeBytes), UpdateCore.PublicKey(), DateTime.UtcNow);
                if (release.sequence != record.sequence) throw new InvalidDataException("Signed release sequence does not match the handoff.");
                Process parent = Process.GetProcessById(parentPid);
                VerifyParent(record, nonce, parent, root);
                using (var installGate = new Mutex(false, InstallMutexName(rootId)))
                using (var startupGate = new Mutex(false, StartMutexName(rootId)))
                {
                    bool ownsInstall = false, ownsStart = false, ownsCoordinator = false;
                    FileStream rootLock = null;
                    Mutex coordinator = null;
                    try
                    {
                        try { ownsInstall = installGate.WaitOne(0); }
                        catch (AbandonedMutexException) { ownsInstall = true; }
                        if (!ownsInstall) throw new IOException("Another update is already installing.");
                        try { ownsStart = startupGate.WaitOne(GateTimeoutMs); }
                        catch (AbandonedMutexException) { ownsStart = true; }
                        if (!ownsStart) throw new TimeoutException("Updater startup is busy.");
                        rootLock = new FileStream(UpdateCore.SafeFile(root, ".vp-update.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
                        EnsureOrdinaryFile(UpdateCore.SafeFile(root, ".vp-update.lock"));
                        var current = UpdateCore.ReadInstalled(root);
                        if (!string.Equals(UpdateCore.FileHash(UpdateCore.SafeFile(root, "INSTALL-MANIFEST.json")), record.installedManifestSha256, StringComparison.OrdinalIgnoreCase))
                            throw new InvalidDataException("The installation manifest changed during update handoff.");
                        Package package = release.packages.FirstOrDefault(value => value.flavor == current.flavor);
                        if (package == null || package.flavor != current.flavor || release.sequence <= current.updateSequence ||
                            package.size != record.installerSize || !string.Equals(package.sha256, record.installerSha256, StringComparison.OrdinalIgnoreCase))
                            throw new InvalidDataException("Signed update package no longer matches this installation.");
                        var manifestUpdater = current.files.FirstOrDefault(file => string.Equals(file.path, "VideoProcessorUpdate.exe", StringComparison.OrdinalIgnoreCase));
                        string installedUpdater = UpdateCore.SafeFile(root, "VideoProcessorUpdate.exe");
                        if (manifestUpdater == null || manifestUpdater.policy != "managed" ||
                            !string.Equals(manifestUpdater.sha256, record.updaterSha256, StringComparison.OrdinalIgnoreCase) ||
                            !string.Equals(UpdateCore.FileHash(installedUpdater), record.updaterSha256, StringComparison.OrdinalIgnoreCase))
                            throw new InvalidDataException("Installed updater changed before setup.");

                        Checkpoint("child-validated-before-ready", handoffDirectory);

                        using (var handshake = new NamedPipeClientStream(".", handshakeName, PipeDirection.InOut,
                            PipeOptions.Asynchronous, TokenImpersonationLevel.Impersonation))
                        {
                            handshake.Connect(HandshakeTimeoutMs);
                            long childStart = FileTime(Process.GetCurrentProcess());
                            WriteHandshake(handshake, "READY|" + nonce + "|" + Process.GetCurrentProcess().Id + "|" + childStart);
                            readySent = true;
                        }

                        if (!parent.WaitForExit(ParentExitTimeoutMs)) throw new TimeoutException("Updater coordinator did not exit after handoff readiness.");
                        coordinator = new Mutex(false, CoordinatorMutexName(rootId));
                        try
                        {
                            try { ownsCoordinator = coordinator.WaitOne(ParentExitTimeoutMs); }
                            catch (AbandonedMutexException) { ownsCoordinator = true; }
                            if (!ownsCoordinator) throw new TimeoutException("Updater coordinator did not release its singleton after handoff.");
                            if (ownsStart) { startupGate.ReleaseMutex(); ownsStart = false; }

                            current = UpdateCore.ReadInstalled(root);
                            string currentManifestPath = UpdateCore.SafeFile(root, "INSTALL-MANIFEST.json");
                            package = release.packages.FirstOrDefault(value => value.flavor == current.flavor);
                            if (package == null || !string.Equals(UpdateCore.FileHash(currentManifestPath), record.installedManifestSha256, StringComparison.OrdinalIgnoreCase) ||
                                current.flavor != package.flavor || release.sequence <= current.updateSequence || package.size != record.installerSize ||
                                !string.Equals(package.sha256, record.installerSha256, StringComparison.OrdinalIgnoreCase))
                                throw new InvalidDataException("The installation changed while the updater coordinator was exiting.");
                            apps = AppLifecycle.Find(root);
                            bool playerRunning = apps.Any(app => !app.config && !app.process.HasExited);
                            if (record.unattended)
                            {
                                string preferencesPath = Path.Combine(CacheRoot(rootId), "preferences.json");
                                Preferences preferences = File.Exists(preferencesPath)
                                    ? UpdateCore.Json<Preferences>(File.ReadAllText(preferencesPath)) : new Preferences { mode = "manual" };
                                if (!UpdateCore.CanAutoInstall(preferences, release, current, playerRunning))
                                    throw new InvalidOperationException("Automatic update cancelled because playback or update preferences changed before setup.");
                            }

                            AppLifecycle.Close(apps);
                            var remaining = AppLifecycle.Find(root);
                            try { if (remaining.Count != 0) throw new IOException("An application restarted before setup. Close it and retry."); }
                            finally { foreach (var app in remaining) app.process.Dispose(); }

                            current = UpdateCore.ReadInstalled(root);
                            package = release.packages.FirstOrDefault(value => value.flavor == current.flavor);
                            if (package == null || !string.Equals(UpdateCore.FileHash(UpdateCore.SafeFile(root, "INSTALL-MANIFEST.json")), record.installedManifestSha256, StringComparison.OrdinalIgnoreCase) ||
                                current.flavor != package.flavor || release.sequence <= current.updateSequence || package.size != record.installerSize ||
                                !string.Equals(package.sha256, record.installerSha256, StringComparison.OrdinalIgnoreCase))
                                throw new InvalidDataException("The installation changed before setup could start.");

                            Checkpoint("child-before-setup", handoffDirectory);

                            string args = (UpdateCore.IsPortableRoot(root, current.flavor) ? "/PORTABLEUPDATE=1 " : "") +
                                (record.unattended ? "/VERYSILENT /SUPPRESSMSGBOXES " : "/SILENT ") +
                                "/SP- /NORESTART /NORESTARTAPPLICATIONS /NOCLOSEAPPLICATIONS /DIR=" + Program.Quote(root) +
                                " /LOG=" + Program.Quote(Path.Combine(handoffDirectory, "setup.log"));
                            using (var setup = Process.Start(new ProcessStartInfo(stagedInstaller, args)
                                { UseShellExecute = false, WorkingDirectory = handoffDirectory }))
                            {
                                if (setup == null) throw new IOException("Could not start the staged setup program.");
                                setupStarted = true; setup.WaitForExit();
                                if (setup.ExitCode != 0) throw new IOException("Setup did not complete (" + setup.ExitCode + "). See the staged setup.log. Rerun setup to recover before starting VP.");
                            }
                            UpdateCore.VerifyInstalled(root, release, package);
                            AppLifecycle.Relaunch(apps, root);
                            if (!WriteResult(resultPath, "complete", "Installed and verified update " + release.sequence + "."))
                                throw new IOException("The update succeeded, but its handoff result could not be persisted.");
                            resultWritten = true;
                        }
                        finally
                        {
                            if (ownsCoordinator) { coordinator.ReleaseMutex(); ownsCoordinator = false; }
                            coordinator.Dispose();
                        }
                    }
                    catch (Exception ex)
                    {
                        if (!resultWritten) resultWritten = WriteResult(resultPath, setupStarted ? "setup-failed" : "handoff-failed", ex.Message);
                        if (readySent && !setupStarted && apps != null) AppLifecycle.RelaunchClosed(apps, root);
                        if (readySent && !record.unattended) MessageBox.Show(ex.Message, "VideoProcessor update", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        throw;
                    }
                    finally
                    {
                        if (rootLock != null) rootLock.Dispose();
                        if (ownsCoordinator && coordinator != null) try { coordinator.ReleaseMutex(); } catch { }
                        if (ownsStart) try { startupGate.ReleaseMutex(); } catch { }
                        if (ownsInstall) try { installGate.ReleaseMutex(); } catch { }
                    }
                }
                }
            }
            catch (Exception ex)
            {
                if (!readySent) SendChildError(id, nonce, ex.Message);
                if (!resultWritten) resultWritten = WriteResult(resultPath, setupStarted ? "setup-failed" : "handoff-failed", ex.Message);
                throw;
            }
            finally { if (apps != null) foreach (var app in apps) app.process.Dispose(); }
        }

        public static int RunChild(string[] args)
        {
            string id = "", nonce = "";
            try
            {
                id = ParseArgument(args, "--install-child"); nonce = ParseArgument(args, "--nonce");
                if (id.Length != 64 || !id.All(c => Uri.IsHexDigit(c)) || nonce.Length != 32 || !nonce.All(c => Uri.IsHexDigit(c)))
                    throw new ArgumentException("Invalid update handoff identity.");
                int pid; long start;
                if (!int.TryParse(ParseArgument(args, "--parent-pid"), out pid) ||
                    !long.TryParse(ParseArgument(args, "--parent-start"), System.Globalization.NumberStyles.Integer,
                        System.Globalization.CultureInfo.InvariantCulture, out start)) throw new ArgumentException("Invalid update handoff parent identity.");
                ChildWork(id, nonce, pid, start);
                return 0;
            }
            catch (Exception ex)
            {
                if (args.Contains("--install-child")) SendChildError(id, nonce, ex.Message);
                return 1;
            }
        }
    }
}
