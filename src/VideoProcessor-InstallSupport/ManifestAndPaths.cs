using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Principal;
using Microsoft.Win32;

namespace VideoProcessor.InstallSupport
{
    internal static partial class Program
    {
        private static string SafePath(string root, string relative)
        {
            string normalized = NormalizeRelative(relative);
            string fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string fullPath = Path.GetFullPath(Path.Combine(fullRoot, normalized));
            string prefix = fullRoot + Path.DirectorySeparatorChar;
            if (!fullPath.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Path escapes the installation: " + relative);
            EnsureNoReparse(fullPath);
            return fullPath;
        }

        private static string SafeChildPath(string root, string candidate)
        {
            string fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string fullCandidate = Path.GetFullPath(candidate);
            if (!fullCandidate.StartsWith(fullRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Path escapes the installation: " + candidate);
            EnsureNoReparse(fullCandidate);
            return fullCandidate;
        }

        private static string NormalizeRelative(string relative)
        {
            if (String.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative) ||
                relative.IndexOfAny(new[] { ':', '*', '?', '"', '<', '>', '|' }) >= 0)
                throw new InvalidOperationException("Unsafe package path: " + relative);
            string value = relative.Replace('/', '\\');
            string[] components = value.Split('\\');
            if (components.Length == 0 || components.Any(x => x.Length == 0 || x == "." || x == ".."))
                throw new InvalidOperationException("Unsafe package path: " + relative);
            return value;
        }

        private static void EnsureNoReparse(string path)
        {
            string candidate = Path.GetFullPath(path);
            while (!String.IsNullOrEmpty(candidate))
            {
                if (File.Exists(candidate) || Directory.Exists(candidate))
                {
                    FileAttributes attributes = File.GetAttributes(candidate);
                    if ((attributes & FileAttributes.ReparsePoint) != 0)
                        throw new InvalidOperationException("Setup cannot operate through a junction or symbolic link: " + candidate);
                }
                string parent = Path.GetDirectoryName(candidate);
                if (String.IsNullOrEmpty(parent) || String.Equals(parent, candidate, StringComparison.OrdinalIgnoreCase))
                    break;
                candidate = parent;
            }
        }

        private static bool IsOperatorFile(string relative)
        {
            return OperatorFilePattern.IsMatch(relative.Replace('/', '\\'));
        }

        private static IDictionary<string, object> ReadJsonObject(string file)
        {
            if (!File.Exists(file)) throw new FileNotFoundException("Required setup file is missing.", file);
            object value;
            try { value = Json.DeserializeObject(File.ReadAllText(file)); }
            catch (Exception ex) { throw new InvalidDataException("Invalid JSON file: " + file, ex); }
            return AsObject(value, "Invalid JSON object: " + file);
        }

        private static IDictionary<string, object> ReadInstallManifest(string file, string installRoot)
        {
            IDictionary<string, object> manifest = ReadJsonObject(file);
            string applicationId = GetString(manifest, "applicationId");
            if (GetInt(manifest, "schemaVersion") != 1 ||
                (applicationId != FullApplicationId && applicationId != ConfigApplicationId))
                throw new InvalidDataException("Unsupported installation manifest: " + file);

            IList<object> files = GetArray(manifest, "files");
            if (files == null || files.Count == 0)
                throw new InvalidDataException("Unsupported installation manifest: " + file);
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            ValidateManifestEntries(files, "payload", installRoot, seen, true);
            IList<object> cleanup = GetArray(manifest, "cleanupFiles");
            if (cleanup != null) ValidateManifestEntries(cleanup, "cleanup", installRoot, seen, false);
            return manifest;
        }

        private static void ValidateManifestEntries(IList<object> entries, string section, string installRoot, HashSet<string> seen, bool requirePolicy)
        {
            foreach (object raw in entries)
            {
                IDictionary<string, object> entry = AsObject(raw, "Invalid " + section + " entry.");
                string relative = NormalizeRelative(GetString(entry, "path"));
                SafePath(installRoot, relative);
                string sha256 = GetString(entry, "sha256");
                if (!Sha256Pattern.IsMatch(sha256) || !seen.Add(relative))
                    throw new InvalidDataException("Invalid " + section + " entry: " + relative);
                if (requirePolicy)
                {
                    string policy = GetString(entry, "policy");
                    if (policy != "managed" && policy != "seed")
                        throw new InvalidDataException("Invalid payload entry: " + relative);
                    if (policy == "managed" && IsOperatorFile(relative))
                        throw new InvalidDataException("Operator data cannot be managed by setup: " + relative);
                }
                else if (IsOperatorFile(relative))
                    throw new InvalidDataException("Invalid cleanup entry: " + relative);
            }
        }

        private static void AssertInstallIdentity(Options options, IDictionary<string, object> manifest, string installRoot)
        {
            if (GetString(manifest, "applicationId") != options.ApplicationId)
                throw new InvalidDataException("Setup payload identity does not match this installer.");
            bool configOnly = options.ApplicationId == ConfigApplicationId;
            string productGuid = configOnly ? ConfigProductGuid : FullProductGuid;
            string registered = GetRegisteredPath(productGuid);
            if (!options.PortableUpdate && !String.IsNullOrEmpty(registered) && !SamePath(registered, installRoot))
                throw new InvalidDataException("This installer is registered at " + registered + ". Updates must use that folder.");

            if (options.PortableUpdate)
            {
                if (!String.IsNullOrEmpty(registered) && SamePath(registered, installRoot))
                    throw new InvalidDataException("Portable update cannot modify a registered installation.");
                if (Directory.Exists(installRoot) && Directory.GetFiles(installRoot, "unins*.exe", SearchOption.TopDirectoryOnly).Length != 0)
                    throw new InvalidDataException("Portable update cannot modify an installation with uninstall records.");
                IDictionary<string, object> portableManifest = ReadInstallManifest(SafePath(installRoot, "INSTALL-MANIFEST.json"), installRoot);
                if (GetString(portableManifest, "applicationId") != options.ApplicationId)
                    throw new InvalidDataException("Portable update identity does not match the existing folder.");
            }

            string existing = SafePath(installRoot, "INSTALL-MANIFEST.json");
            if (File.Exists(existing))
            {
                try
                {
                    IDictionary<string, object> previous = ReadJsonObject(existing);
                    if (previous.ContainsKey("applicationId") && GetString(previous, "applicationId") != options.ApplicationId)
                        throw new InvalidDataException("A different installation flavor owns this folder. Choose a separate folder.");
                }
                catch (InvalidDataException ex)
                {
                    if (ex.Message.StartsWith("A different installation flavor", StringComparison.Ordinal)) throw;
                }
            }
            if (configOnly && File.Exists(SafePath(installRoot, "VideoProcessor.exe")))
                throw new InvalidDataException("Config-only setup cannot replace a full VP installation. Use the full setup.");
        }

        private static string GetRegisteredPath(string guid)
        {
            string keyPath = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\{" + guid + "}_is1";
            using (RegistryKey hive = RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, RegistryView.Registry64))
            using (RegistryKey key = hive.OpenSubKey(keyPath, false))
            {
                if (key == null) return null;
                object value = key.GetValue("Inno Setup: App Path");
                return value == null ? null : Convert.ToString(value);
            }
        }

        private static void AssertNotAdministrator()
        {
            using (WindowsIdentity identity = WindowsIdentity.GetCurrent())
            {
                var principal = new WindowsPrincipal(identity);
                if (principal.IsInRole(WindowsBuiltInRole.Administrator))
                    throw new InvalidOperationException("Run this per-user installer normally, not as administrator, so permissions match ordinary VP operation.");
            }
        }

        private static void AssertApplicationsClosed(string installRoot)
        {
            string prefix = installRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
            foreach (string name in new[] { "VideoProcessor", "VideoProcessor-GUI", "VideoProcessorConfig" })
            {
                foreach (Process process in Process.GetProcessesByName(name))
                {
                    using (process)
                    {
                        string processPath;
                        try { processPath = process.MainModule.FileName; }
                        catch (Exception ex)
                        {
                            if (process.HasExited) continue;
                            throw new InvalidOperationException("Cannot inspect running " + name + ". Close it and retry setup.", ex);
                        }
                        if (String.IsNullOrEmpty(processPath))
                            throw new InvalidOperationException("Cannot inspect running " + name + ". Close it and retry setup.");
                        if (processPath.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                            throw new InvalidOperationException(
                                "Save your work and close VideoProcessor and Config, including its tray icon, then retry. Running: " +
                                name + " (PID " + process.Id + ")");
                    }
                }
            }
        }

        private static bool SamePath(string left, string right)
        {
            if (String.IsNullOrEmpty(left) || String.IsNullOrEmpty(right)) return false;
            try
            {
                return String.Equals(
                    Path.GetFullPath(left).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                    Path.GetFullPath(right).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                    StringComparison.OrdinalIgnoreCase);
            }
            catch { return false; }
        }
    }
}
