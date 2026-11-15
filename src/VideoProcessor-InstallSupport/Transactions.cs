using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;

namespace VideoProcessor.InstallSupport
{
    internal static partial class Program
    {
        private static void CheckWritable(string installRoot, InstallPlan plan, IDictionary<string, object> manifest)
        {
            foreach (string directory in new[] { installRoot, SafePath(installRoot, "logs"), SafePath(installRoot, "vprenderer"), SafePath(installRoot, "vprenderer-beta") })
                AssertWritable(directory);
            foreach (object raw in GetArray(manifest, "files"))
            {
                IDictionary<string, object> entry = AsObject(raw, "Invalid payload entry.");
                string file = SafePath(installRoot, GetString(entry, "path"));
                AssertWritable(Path.GetDirectoryName(file));
            }

            var toCheck = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { "VideoProcessor.cfg", "VideoProcessor.state" };
            foreach (string relative in plan.Current.Keys) toCheck.Add(relative);
            foreach (string relative in plan.Obsolete) toCheck.Add(relative);
            foreach (string relative in toCheck)
            {
                string file = SafePath(installRoot, relative);
                if (Directory.Exists(file)) throw new IOException("A folder blocks an application file: " + relative);
                if (File.Exists(file))
                {
                    using (FileStream stream = File.Open(file, FileMode.Open, FileAccess.ReadWrite, FileShare.None)) { }
                }
            }
        }

        private static void AssertWritable(string directory)
        {
            Directory.CreateDirectory(directory);
            EnsureNoReparse(directory);
            string probe = Path.Combine(directory, ".vp-write-test-" + Guid.NewGuid().ToString("N"));
            using (FileStream stream = new FileStream(probe, FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None)) { }
            File.Delete(probe);
        }

        private static string PrepareTransaction(string installRoot, string history, IDictionary<string, object> manifest, InstallPlan plan)
        {
            string backup = Path.Combine(history, DateTime.Now.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N"));
            string filesRoot = Path.Combine(backup, "files");
            Directory.CreateDirectory(filesRoot);
            EnsureNoReparse(backup);

            var entries = new List<object>();
            var paths = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (string path in plan.Current.Keys) paths.Add(path);
            foreach (string path in plan.Obsolete) paths.Add(path);
            paths.Add("INSTALL-MANIFEST.json");

            foreach (string relative in paths)
            {
                string file = SafePath(installRoot, relative);
                if (Directory.Exists(file)) throw new IOException("A folder blocks an application file: " + relative);
                bool existed = File.Exists(file);
                string hash = String.Empty;
                if (existed)
                {
                    string saved = SafePath(filesRoot, relative);
                    Directory.CreateDirectory(Path.GetDirectoryName(saved));
                    File.Copy(file, saved, false);
                    hash = HashFile(file);
                    if (!String.Equals(HashFile(saved), hash, StringComparison.OrdinalIgnoreCase))
                        throw new IOException("Backup verification failed: " + relative);
                }
                entries.Add(Object("path", relative, "existed", existed, "sha256", hash));
            }

            var journal = Object(
                "schemaVersion", JournalSchemaVersion,
                "installRoot", installRoot,
                "status", "pending",
                "build", GetString(manifest, "build"),
                "files", entries.ToArray());
            string journalPath = Path.Combine(backup, "transaction.json");
            WriteJsonAtomic(journalPath, journal);

            try
            {
                foreach (KeyValuePair<string, string> item in plan.Recovered)
                {
                    string savedRelative = Path.Combine("recovered-files", Path.GetFileName(backup), item.Key).Replace('\\', '/');
                    string saved = SafePath(installRoot, savedRelative);
                    Directory.CreateDirectory(Path.GetDirectoryName(saved));
                    File.Copy(SafePath(installRoot, item.Key), saved, false);
                    if (!String.Equals(HashFile(saved), item.Value, StringComparison.OrdinalIgnoreCase))
                        throw new IOException("Recovery copy failed: " + item.Key);
                }
                foreach (string relative in plan.Obsolete)
                {
                    string file = SafePath(installRoot, relative);
                    if (File.Exists(file)) File.Delete(file);
                }
            }
            catch
            {
                RestoreBackup(installRoot, backup);
                throw;
            }
            return backup;
        }

        private static void RecoverPendingTransactions(string installRoot, string history)
        {
            if (!Directory.Exists(history)) return;
            EnsureNoReparse(history);
            foreach (string directory in Directory.GetDirectories(history))
            {
                string safeDirectory = SafeChildPath(history, directory);
                string journalPath = SafePath(safeDirectory, "transaction.json");
                if (!File.Exists(journalPath)) continue;
                IDictionary<string, object> journal = ReadJsonObject(journalPath);
                if (GetString(journal, "status") == "pending") RestoreBackup(installRoot, safeDirectory);
            }
        }

        private static void RestoreBackup(string installRoot, string backup)
        {
            AssertApplicationsClosed(installRoot);
            string journalPath = SafePath(backup, "transaction.json");
            IDictionary<string, object> journal = ReadJsonObject(journalPath);
            if (GetInt(journal, "schemaVersion") != JournalSchemaVersion ||
                !SamePath(GetString(journal, "installRoot"), installRoot))
                throw new InvalidDataException("Backup belongs to another installation.");
            IList<object> rawEntries = GetArray(journal, "files");
            if (rawEntries == null) throw new InvalidDataException("Backup journal has no file inventory.");

            // Validate every journal path and backup hash before the first restore write.
            var entries = new List<JournalEntry>();
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            string filesRoot = SafePath(backup, "files");
            foreach (object raw in rawEntries)
            {
                IDictionary<string, object> entry = AsObject(raw, "Invalid backup journal entry.");
                string relative = NormalizeRelative(GetString(entry, "path"));
                if (IsOperatorFile(relative)) throw new InvalidDataException("Refusing to restore operator data.");
                if (!seen.Add(relative)) throw new InvalidDataException("Backup journal contains duplicate paths.");
                string destination = SafePath(installRoot, relative);
                if (Directory.Exists(destination)) throw new IOException("A folder blocks recovery: " + relative);
                bool existed = GetBoolean(entry, "existed");
                string hash = GetString(entry, "sha256");
                if (existed)
                {
                    if (!Sha256Pattern.IsMatch(hash)) throw new InvalidDataException("Invalid backup hash: " + relative);
                    string saved = SafePath(filesRoot, relative);
                    if (!File.Exists(saved) || !String.Equals(HashFile(saved), hash, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidDataException("Backup failed verification: " + relative);
                }
                else if (!String.IsNullOrEmpty(hash))
                    throw new InvalidDataException("Invalid empty-file backup record: " + relative);
                entries.Add(new JournalEntry { Path = relative, Existed = existed, Sha256 = hash });
            }

            foreach (JournalEntry entry in entries)
            {
                string destination = SafePath(installRoot, entry.Path);
                if (entry.Existed)
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(destination));
                    AtomicCopy(SafePath(filesRoot, entry.Path), destination);
                }
                else if (File.Exists(destination))
                    File.Delete(destination);
            }
            journal["status"] = "restored";
            WriteJsonAtomic(journalPath, journal);
        }

        private static void VerifyPayload(IDictionary<string, object> manifest, string installRoot)
        {
            foreach (object raw in GetArray(manifest, "files"))
            {
                IDictionary<string, object> entry = AsObject(raw, "Invalid payload entry.");
                string relative = NormalizeRelative(GetString(entry, "path"));
                string file = SafePath(installRoot, relative);
                if (!File.Exists(file)) throw new FileNotFoundException("Installed file is missing: " + relative, file);
                if (GetString(entry, "policy") == "managed" &&
                    !String.Equals(HashFile(file), GetString(entry, "sha256"), StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("Installed file does not match the selected build: " + relative);
            }
        }

        private static void RemoveCompletedBackup(string installRoot, string backup)
        {
            string journalPath = SafePath(backup, "transaction.json");
            if (!File.Exists(journalPath)) return;
            IDictionary<string, object> journal;
            try { journal = ReadJsonObject(journalPath); }
            catch { return; }
            string status = GetString(journal, "status");
            if (GetInt(journal, "schemaVersion") != JournalSchemaVersion ||
                !SamePath(GetString(journal, "installRoot"), installRoot) ||
                (status != "complete" && status != "restored"))
                return;

            var owned = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { Path.GetFullPath(journalPath) };
            IList<object> rawEntries = GetArray(journal, "files");
            if (rawEntries == null) return;
            string filesRoot = SafePath(backup, "files");
            foreach (object raw in rawEntries)
            {
                IDictionary<string, object> entry = AsObject(raw, "Invalid backup journal entry.");
                string relative = NormalizeRelative(GetString(entry, "path"));
                if (IsOperatorFile(relative)) throw new InvalidDataException("Refusing to prune a backup claiming operator data.");
                string saved = SafePath(filesRoot, relative);
                if (GetBoolean(entry, "existed"))
                {
                    if (!File.Exists(saved) || !String.Equals(HashFile(saved), GetString(entry, "sha256"), StringComparison.OrdinalIgnoreCase))
                        return;
                    owned.Add(Path.GetFullPath(saved));
                }
            }

            var pending = new Queue<string>();
            pending.Enqueue(backup);
            while (pending.Count != 0)
            {
                string current = pending.Dequeue();
                EnsureNoReparse(current);
                foreach (string child in Directory.GetFileSystemEntries(current))
                {
                    EnsureNoReparse(child);
                    if (Directory.Exists(child)) pending.Enqueue(child);
                    else if (!owned.Contains(Path.GetFullPath(child))) return;
                }
            }

            foreach (string file in owned)
                if (!String.Equals(file, Path.GetFullPath(journalPath), StringComparison.OrdinalIgnoreCase))
                    File.Delete(file);
            File.Delete(journalPath);
            RemoveEmptyDirectories(backup);
        }

        private static void RemoveEmptyDirectories(string directory)
        {
            if (!Directory.Exists(directory)) return;
            EnsureNoReparse(directory);
            foreach (string child in Directory.GetDirectories(directory))
            {
                if ((File.GetAttributes(child) & FileAttributes.ReparsePoint) != 0) continue;
                RemoveEmptyDirectories(child);
            }
            if (Directory.GetFileSystemEntries(directory).Length == 0)
                Directory.Delete(directory, false);
        }

        private static void WriteJsonAtomic(string file, object value)
        {
            string directory = Path.GetDirectoryName(Path.GetFullPath(file));
            Directory.CreateDirectory(directory);
            EnsureNoReparse(directory);
            string temporary = file + "." + Guid.NewGuid().ToString("N") + ".tmp";
            byte[] bytes = new UTF8Encoding(true).GetBytes(Json.Serialize(value));
            try
            {
                using (FileStream stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    stream.Write(bytes, 0, bytes.Length);
                    stream.Flush(true);
                }
                EnsureNoReparse(file);
                if (File.Exists(file)) File.Replace(temporary, file, null);
                else File.Move(temporary, file);
            }
            finally
            {
                if (File.Exists(temporary)) File.Delete(temporary);
            }
        }

        private static void AtomicCopy(string source, string destination)
        {
            string directory = Path.GetDirectoryName(Path.GetFullPath(destination));
            Directory.CreateDirectory(directory);
            EnsureNoReparse(source);
            EnsureNoReparse(destination);
            EnsureNoReparse(directory);
            string temporary = destination + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try
            {
                File.Copy(source, temporary, false);
                if (File.Exists(destination)) File.Replace(temporary, destination, null);
                else File.Move(temporary, destination);
            }
            finally
            {
                if (File.Exists(temporary)) File.Delete(temporary);
            }
        }
    }
}
