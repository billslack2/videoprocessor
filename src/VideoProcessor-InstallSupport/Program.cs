using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Principal;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;
using Microsoft.Win32;

namespace VideoProcessor.InstallSupport
{
    internal static partial class Program
    {
#if INSTALL_SUPPORT_TEST_IDENTITY
        private const string FullApplicationId = "VideoProcessor-0B37C49A-79D2-4D5D-9A7E-C99B31A2F001";
        private const string ConfigApplicationId = "VideoProcessorConfig-7AD19C54-1F18-4227-8AF1-C99B31A2F002";
        private const string FullProductGuid = "0B37C49A-79D2-4D5D-9A7E-C99B31A2F001";
        private const string ConfigProductGuid = "7AD19C54-1F18-4227-8AF1-C99B31A2F002";
#else
        private const string FullApplicationId = "VideoProcessor-42D852F1-70E9-43ED-8739-D61752106D59";
        private const string ConfigApplicationId = "VideoProcessorConfig-BA15DBE8-210F-42AA-AE86-B4628395E77F";
        private const string FullProductGuid = "42D852F1-70E9-43ED-8739-D61752106D59";
        private const string ConfigProductGuid = "BA15DBE8-210F-42AA-AE86-B4628395E77F";
#endif
        private const int JournalSchemaVersion = 1;
        private static readonly Regex Sha256Pattern = new Regex("^[0-9a-fA-F]{64}$", RegexOptions.Compiled | RegexOptions.CultureInvariant);
        private static readonly Regex OperatorFilePattern = new Regex(
            @"^(logs|shaders|luts|profiles)([\\/]|$)|\.(cfg|ini|state|cube|hlsl|glsl|bak|log)$|VideoProcessorShaderCache\.bin$",
            RegexOptions.Compiled | RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
        private static readonly JavaScriptSerializer Json = new JavaScriptSerializer { MaxJsonLength = 16777216, RecursionLimit = 128 };

        private sealed class Options
        {
            public string Action;
            public string ApplicationId;
            public string InstallRoot;
            public string PayloadManifest;
            public string ResultPath;
            public string BackupDirectory;
            public bool PortableUpdate;
        }

        private sealed class InstallPlan
        {
            public readonly Dictionary<string, string> Current = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            public readonly Dictionary<string, string> Cleanup = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            public readonly Dictionary<string, string> Previous = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            public readonly Dictionary<string, string> Recovered = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            public readonly List<string> Obsolete = new List<string>();
        }

        private sealed class JournalEntry
        {
            public string Path;
            public bool Existed;
            public string Sha256;
        }

        private static int Main(string[] args)
        {
            Options options = null;
            string message;
            int exitCode;
            try
            {
                options = ParseArguments(args);
                message = Invoke(options);
                exitCode = 0;
                Console.Out.WriteLine(message);
            }
            catch (Exception ex)
            {
                message = ex.Message;
                exitCode = 1;
                Console.Error.WriteLine(message);
            }

            if (options != null && !String.IsNullOrEmpty(options.ResultPath))
            {
                try { File.WriteAllText(options.ResultPath, message, new UTF8Encoding(false)); }
                catch (Exception ex)
                {
                    if (exitCode == 0)
                    {
                        Console.Error.WriteLine("Could not write the setup result file: " + ex.Message);
                        exitCode = 1;
                    }
                }
            }
            return exitCode;
        }

        private static Options ParseArguments(string[] args)
        {
            var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            var flags = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            for (int i = 0; i < args.Length; ++i)
            {
                string key = args[i];
                if (!key.StartsWith("--", StringComparison.Ordinal))
                    throw new InvalidDataException("Unexpected installer helper argument: " + key);
                key = key.Substring(2);
                if (key.Equals("portable-update", StringComparison.OrdinalIgnoreCase))
                {
                    if (!flags.Add(key)) throw new InvalidDataException("Duplicate installer helper argument: --" + key);
                    continue;
                }
                if (i + 1 >= args.Length || args[i + 1].StartsWith("--", StringComparison.Ordinal))
                    throw new InvalidDataException("Missing value for installer helper argument: --" + key);
                if (values.ContainsKey(key)) throw new InvalidDataException("Duplicate installer helper argument: --" + key);
                values.Add(key, args[++i]);
            }

            var result = new Options();
            result.Action = Required(values, "action");
            result.ApplicationId = Required(values, "application-id");
            result.InstallRoot = Required(values, "install-root");
            result.PayloadManifest = Optional(values, "payload-manifest");
            result.ResultPath = Optional(values, "result-path");
            result.BackupDirectory = Optional(values, "backup-directory");
            result.PortableUpdate = flags.Contains("portable-update");

            if (!new[] { "Check", "Prepare", "Verify", "Commit", "Restore", "Finalize" }
                .Contains(result.Action, StringComparer.OrdinalIgnoreCase))
                throw new InvalidDataException("An installer action is required.");
            result.Action = result.Action.Substring(0, 1).ToUpperInvariant() + result.Action.Substring(1).ToLowerInvariant();
            if (result.ApplicationId != FullApplicationId && result.ApplicationId != ConfigApplicationId)
                throw new InvalidDataException("Setup application identity is not supported by this helper.");
            if (String.IsNullOrWhiteSpace(result.InstallRoot))
                throw new InvalidDataException("An installation folder is required.");
            if ((result.Action == "Check" || result.Action == "Prepare" || result.Action == "Verify" || result.Action == "Commit") &&
                String.IsNullOrWhiteSpace(result.PayloadManifest))
                throw new InvalidDataException("A payload manifest is required for this installer action.");
            if ((result.Action == "Restore" || result.Action == "Finalize") && String.IsNullOrWhiteSpace(result.BackupDirectory))
                throw new InvalidDataException("A backup directory is required for this installer action.");
            return result;
        }

        private static string Required(IDictionary<string, string> values, string name)
        {
            string value;
            if (!values.TryGetValue(name, out value) || String.IsNullOrWhiteSpace(value))
                throw new InvalidDataException("Required installer helper argument is missing: --" + name);
            return value;
        }

        private static string Optional(IDictionary<string, string> values, string name)
        {
            string value;
            return values.TryGetValue(name, out value) ? value : null;
        }

        private static string Invoke(Options options)
        {
            string installRoot = Path.GetFullPath(options.InstallRoot).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            if (installRoot.Length == 0 || String.Equals(installRoot, Path.GetPathRoot(installRoot).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar), StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Select a dedicated VideoProcessor folder, not a drive root.");
            string history = SafePath(installRoot, ".vp-installer-backups");

            if (options.Action == "Restore")
            {
                string backup = ValidateBackupDirectory(installRoot, history, options.BackupDirectory);
                RestoreBackup(installRoot, backup);
                return "Previous application files restored. Settings/state preserved. Rerun the matching installer to repair shortcuts and registration.";
            }
            if (options.Action == "Finalize")
            {
                string backup = ValidateBackupDirectory(installRoot, history, options.BackupDirectory);
                IDictionary<string, object> journal = ReadJsonObject(SafePath(backup, "transaction.json"));
                if (!SamePath(GetString(journal, "installRoot"), installRoot) || GetString(journal, "status") != "complete")
                    throw new InvalidDataException("Setup has not committed this transaction.");
                if (Directory.Exists(history))
                {
                    foreach (string directory in Directory.GetDirectories(history))
                        RemoveCompletedBackup(installRoot, SafeChildPath(history, directory));
                }
                RemoveEmptyDirectories(history);
                foreach (string relative in new[] { "prerequisites", "setup" })
                    RemoveEmptyDirectories(SafePath(installRoot, relative));
                return "Completed recovery backups and empty setup-only directories cleaned; unrecognized or modified files retained.";
            }

            IDictionary<string, object> manifest = ReadInstallManifest(Path.GetFullPath(options.PayloadManifest), installRoot);
            if (options.Action == "Check" || options.Action == "Prepare")
            {
                AssertInstallIdentity(options, manifest, installRoot);
                AssertNotAdministrator();
                AssertApplicationsClosed(installRoot);
                RecoverPendingTransactions(installRoot, history);
                InstallPlan plan = BuildPlan(manifest, installRoot);
                CheckWritable(installRoot, plan, manifest);
                if (options.Action == "Check")
                    return "Location, ownership, running applications, and write permissions checked.";
                return PrepareTransaction(installRoot, history, manifest, plan);
            }

            VerifyPayload(manifest, installRoot);
            if (options.Action == "Commit")
            {
                string backup = ValidateBackupDirectory(installRoot, history, options.BackupDirectory);
                string journalPath = SafePath(backup, "transaction.json");
                IDictionary<string, object> journal = ReadJsonObject(journalPath);
                if (!SamePath(GetString(journal, "installRoot"), installRoot) || GetString(journal, "status") != "pending")
                    throw new InvalidDataException("The setup transaction is not pending for this installation.");
                AtomicCopy(options.PayloadManifest, SafePath(installRoot, "INSTALL-MANIFEST.json"));
                journal["status"] = "complete";
                WriteJsonAtomic(journalPath, journal);
            }
            return "Verified installed build " + GetString(manifest, "build") + ".";
        }

        private static string ValidateBackupDirectory(string installRoot, string history, string value)
        {
            string backup = Path.GetFullPath(value);
            string prefix = history.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
            if (!backup.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Backup must be inside this installation history.");
            EnsureNoReparse(backup);
            return backup;
        }

        private static string GetString(IDictionary<string, object> value, string key)
        {
            object result;
            if (!value.TryGetValue(key, out result) || result == null) return String.Empty;
            return Convert.ToString(result);
        }

        private static int GetInt(IDictionary<string, object> value, string key)
        {
            object result;
            if (!value.TryGetValue(key, out result) || result == null) return 0;
            return Convert.ToInt32(result);
        }

        private static bool GetBoolean(IDictionary<string, object> value, string key)
        {
            object result;
            if (!value.TryGetValue(key, out result) || !(result is bool))
                throw new InvalidDataException("Invalid backup journal field: " + key);
            return (bool)result;
        }

        private static IList<object> GetArray(IDictionary<string, object> value, string key)
        {
            object result;
            if (!value.TryGetValue(key, out result) || result == null) return null;
            var array = result as object[];
            if (array != null) return array;
            var list = result as IList<object>;
            if (list != null) return list;
            throw new InvalidDataException("Invalid JSON array: " + key);
        }

        private static IDictionary<string, object> AsObject(object value, string message)
        {
            IDictionary<string, object> result = value as IDictionary<string, object>;
            if (result == null) throw new InvalidDataException(message);
            return result;
        }

        private static Dictionary<string, object> Object(params object[] keyValues)
        {
            var result = new Dictionary<string, object>(StringComparer.Ordinal);
            for (int i = 0; i < keyValues.Length; i += 2)
                result.Add(Convert.ToString(keyValues[i]), keyValues[i + 1]);
            return result;
        }
    }
}
