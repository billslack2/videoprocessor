using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace VideoProcessor.InstallSupport
{
    internal static partial class Program
    {
        private static InstallPlan BuildPlan(IDictionary<string, object> manifest, string installRoot)
        {
            var plan = new InstallPlan();
            foreach (object raw in GetArray(manifest, "files"))
            {
                IDictionary<string, object> entry = AsObject(raw, "Invalid payload entry.");
                string relative = NormalizeRelative(GetString(entry, "path"));
                if (GetString(entry, "policy") == "managed") plan.Current[relative] = GetString(entry, "sha256");
            }
            IList<object> cleanupEntries = GetArray(manifest, "cleanupFiles");
            if (cleanupEntries != null)
            {
                foreach (object raw in cleanupEntries)
                {
                    IDictionary<string, object> entry = AsObject(raw, "Invalid cleanup entry.");
                    plan.Cleanup[NormalizeRelative(GetString(entry, "path"))] = GetString(entry, "sha256");
                }
            }

            string installed = SafePath(installRoot, "INSTALL-MANIFEST.json");
            IDictionary<string, object> oldManifest = null;
            if (File.Exists(installed))
            {
                try { oldManifest = ReadInstallManifest(installed, installRoot); }
                catch { oldManifest = null; }
            }
            if (oldManifest != null)
            {
                foreach (object raw in GetArray(oldManifest, "files"))
                {
                    IDictionary<string, object> entry = AsObject(raw, "Invalid old payload entry.");
                    string relative = NormalizeRelative(GetString(entry, "path"));
                    if (GetString(entry, "policy") == "managed" || plan.Cleanup.ContainsKey(relative))
                        plan.Previous[relative] = GetString(entry, "sha256");
                }
                // Carry a prior setup-only ownership hash forward only for paths
                // explicitly retired by this package. Edited text remains user-owned.
                IList<object> oldCleanup = GetArray(oldManifest, "cleanupFiles");
                if (oldCleanup != null)
                {
                    foreach (object raw in oldCleanup)
                    {
                        IDictionary<string, object> entry = AsObject(raw, "Invalid old cleanup entry.");
                        string relative = NormalizeRelative(GetString(entry, "path"));
                        if (plan.Cleanup.ContainsKey(relative))
                            plan.Previous[relative] = GetString(entry, "sha256");
                    }
                }
            }
            else
            {
                AddZipAdoptionInventory(plan, installRoot);
            }

            foreach (KeyValuePair<string, string> entry in plan.Previous)
            {
                string relative = entry.Key;
                if (plan.Current.ContainsKey(relative)) continue;
                string file = SafePath(installRoot, relative);
                if (File.Exists(file))
                {
                    string actual = HashFile(file);
                    if (!String.Equals(actual, entry.Value, StringComparison.OrdinalIgnoreCase))
                    {
                        if (!(plan.Cleanup.ContainsKey(relative) && !IsBinaryPath(relative)))
                        {
                            plan.Recovered[relative] = actual;
                        }
                        else
                        {
                            // Modified obsolete setup-only text is operator-owned.
                            continue;
                        }
                    }
                }
                if (!plan.Obsolete.Contains(relative, StringComparer.OrdinalIgnoreCase))
                    plan.Obsolete.Add(relative);
            }

            foreach (string directory in new[] { "", "config", "vprenderer", "vprenderer-beta" })
            {
                string fullDirectory = String.IsNullOrEmpty(directory) ? installRoot : SafePath(installRoot, directory);
                if (!Directory.Exists(fullDirectory)) continue;
                IEnumerable<string> dllFiles = directory == "config"
                    ? EnumerateConfigDlls(installRoot, fullDirectory)
                    : Directory.GetFiles(fullDirectory, "*.dll", SearchOption.TopDirectoryOnly);
                foreach (string dll in dllFiles)
                {
                    string relative = dll.Substring(installRoot.TrimEnd(Path.DirectorySeparatorChar).Length + 1).Replace('/', '\\');
                    relative = NormalizeRelative(relative);
                    SafePath(installRoot, relative);
                    if (!plan.Current.ContainsKey(relative) && !plan.Previous.ContainsKey(relative))
                    {
                        plan.Recovered[relative] = HashFile(dll);
                        if (!plan.Obsolete.Contains(relative, StringComparer.OrdinalIgnoreCase))
                            plan.Obsolete.Add(relative);
                    }
                }
            }
            return plan;
        }

        private static IEnumerable<string> EnumerateConfigDlls(string installRoot, string directory)
        {
            var pending = new Stack<string>();
            pending.Push(directory);
            while (pending.Count != 0)
            {
                string current = pending.Pop();
                EnsureNoReparse(current);
                foreach (string file in Directory.GetFiles(current, "*.dll", SearchOption.TopDirectoryOnly))
                {
                    SafeChildPath(installRoot, file);
                    yield return file;
                }
                foreach (string subdirectory in Directory.GetDirectories(current))
                {
                    EnsureNoReparse(subdirectory);
                    pending.Push(subdirectory);
                }
            }
        }

        private static void AddZipAdoptionInventory(InstallPlan plan, string installRoot)
        {
            string legacy = SafePath(installRoot, "RELEASE-MANIFEST.json");
            if (!File.Exists(legacy)) return;
            IDictionary<string, object> inventory;
            try { inventory = ReadJsonObject(legacy); }
            catch { return; }
            if (GetInt(inventory, "schemaVersion") != 1 || GetString(inventory, "layoutVersion") != "VP-0107")
                return;

            if (plan.Cleanup.ContainsKey("RELEASE-MANIFEST.json"))
                plan.Previous["RELEASE-MANIFEST.json"] = HashFile(legacy);
            foreach (KeyValuePair<string, string> item in plan.Cleanup)
            {
                string file = SafePath(installRoot, item.Key);
                if (File.Exists(file) && String.Equals(HashFile(file), item.Value, StringComparison.OrdinalIgnoreCase))
                    plan.Previous[item.Key] = item.Value;
            }

            try { AddRecognizedRuntimeMetadata(plan, installRoot, inventory); }
            catch { /* Unrecognized generated metadata remains operator-owned. */ }

            IList<object> files = GetArray(inventory, "files");
            if (files == null) return;
            foreach (object raw in files)
            {
                IDictionary<string, object> entry = AsObject(raw, "Invalid legacy inventory entry.");
                string relative = NormalizeRelative(GetString(entry, "destination"));
                string extension = Path.GetExtension(relative);
                if ((extension.Equals(".dll", StringComparison.OrdinalIgnoreCase) || extension.Equals(".exe", StringComparison.OrdinalIgnoreCase)) &&
                    !IsOperatorFile(relative))
                {
                    string file = SafePath(installRoot, relative);
                    if (File.Exists(file)) plan.Previous[relative] = HashFile(file);
                }
            }
        }

        private static void AddRecognizedRuntimeMetadata(InstallPlan plan, string installRoot, IDictionary<string, object> inventory)
        {
            const string runtimeRelative = @"prerequisites\runtime-requirement.json";
            string runtimeFile = SafePath(installRoot, runtimeRelative);
            if (!plan.Cleanup.ContainsKey(runtimeRelative) || !File.Exists(runtimeFile)) return;
            IList<object> generatedFiles = GetArray(inventory, "generatedFiles");
            if (generatedFiles == null || !generatedFiles.Any(x => Convert.ToString(x) == "prerequisites/runtime-requirement.json"))
                return;

            IDictionary<string, object> runtime = ReadJsonObject(runtimeFile);
            string[] allowed = { "schemaVersion", "architecture", "minimumVersion", "installerVersion", "installerSha256", "runtimeFiles", "buildArtifacts" };
            if (runtime.Keys.Any(k => !allowed.Contains(k, StringComparer.Ordinal)) ||
                GetInt(runtime, "schemaVersion") != 1 || GetString(runtime, "architecture") != "x64")
                return;
            IList<object> buildArtifacts = GetArray(runtime, "buildArtifacts");
            if (buildArtifacts == null || buildArtifacts.Count != 4) return;
            string redist = SafePath(installRoot, @"prerequisites\vc_redist.x64.exe");
            if (!File.Exists(redist) || !String.Equals(HashFile(redist), GetString(runtime, "installerSha256"), StringComparison.OrdinalIgnoreCase))
                return;
            foreach (object raw in buildArtifacts)
            {
                IDictionary<string, object> entry = AsObject(raw, "Invalid runtime build artifact.");
                string file = SafePath(installRoot, GetString(entry, "file"));
                if (!File.Exists(file) || !String.Equals(HashFile(file), GetString(entry, "sha256"), StringComparison.OrdinalIgnoreCase))
                    return;
            }
            plan.Previous[runtimeRelative] = HashFile(runtimeFile);
        }

        private static bool IsBinaryPath(string relative)
        {
            string extension = Path.GetExtension(relative);
            return extension.Equals(".dll", StringComparison.OrdinalIgnoreCase) || extension.Equals(".exe", StringComparison.OrdinalIgnoreCase);
        }

        private static string HashFile(string file)
        {
            using (System.Security.Cryptography.SHA256 sha = System.Security.Cryptography.SHA256.Create())
            using (FileStream stream = File.OpenRead(file))
            {
                byte[] hash = sha.ComputeHash(stream);
                var result = new System.Text.StringBuilder(hash.Length * 2);
                foreach (byte b in hash) result.Append(b.ToString("x2"));
                return result.ToString().ToUpperInvariant();
            }
        }
    }
}
