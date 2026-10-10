// Copyright VideoProcessor contributors. Licensed under GPL-3.0; see LICENSE.txt.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;
using Microsoft.Win32;

namespace VideoProcessor.Update
{
    public sealed class Envelope { public string payload; public string signature; }
    public sealed class Package
    {
        public string flavor, url, sha256, installManifestSha256;
        public long size;
    }
    public sealed class Release
    {
        public int schemaVersion, minimumUpdaterVersion, rpcVersion, configurationVersion;
        public long sequence;
        public string version, commit, channel, architecture, expiresUtc, notes;
        public Package[] packages;
    }
    public sealed class Installed
    {
        public int schemaVersion, rpcVersion, configurationVersion;
        public string applicationId, coreVersion, sourceCommit, sourceFingerprint, flavor, updateChannel;
        public long updateSequence;
        public bool dirty;
        public InstalledFile[] files;
    }
    public sealed class InstalledFile { public string path, policy, sha256; }
    public sealed class Preferences
    {
        public bool automatic = true; // Legacy preference retained for migration.
        public string mode; // manual, notify, auto; null migrates from automatic.
        public bool allowPlaybackRestart;
        public string channel = "beta", lastCheckUtc;
        public long skippedSequence;
        public string skippedChannel;
        public Dictionary<string, long> highest = new Dictionary<string, long>();
    }
    public static class UpdateCore
    {
        public const int UpdaterVersion = 1;
        public const string FullId = "VideoProcessor-42D852F1-70E9-43ED-8739-D61752106D59";
        public const string ConfigId = "VideoProcessorConfig-BA15DBE8-210F-42AA-AE86-B4628395E77F";
        public const string Repo = "billslack2/videoprocessor";
        public const string AssetPrefix = "https://github.com/" + Repo + "/releases/download/";
        public const int MetadataLimit = 1024 * 1024;
        public static T Json<T>(string text)
        {
            return new JavaScriptSerializer { MaxJsonLength = MetadataLimit, RecursionLimit = 24 }.Deserialize<T>(text);
        }
        public static string Serialize(object value)
        {
            return new JavaScriptSerializer { MaxJsonLength = MetadataLimit, RecursionLimit = 24 }.Serialize(value);
        }
        public static string Hash(byte[] bytes)
        {
            using (var sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant();
        }
        public static string FileHash(string file)
        {
            using (var sha = SHA256.Create()) using (var stream = File.OpenRead(file))
                return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant();
        }
        public static string PublicKey()
        {
            using (var input = Assembly.GetExecutingAssembly().GetManifestResourceStream("UpdatePublicKey.xml"))
            {
                if (input == null) throw new InvalidDataException("Updater has no trusted release key.");
                using (var reader = new StreamReader(input)) return reader.ReadToEnd();
            }
        }
        public static bool IsHash(string value) { return value != null && Regex.IsMatch(value, "^[0-9a-fA-F]{64}$"); }
        public static bool IsAssetUrl(string value)
        {
            Uri uri;
            return Uri.TryCreate(value, UriKind.Absolute, out uri) && uri.Scheme == "https" && uri.IsDefaultPort &&
                uri.UserInfo.Length == 0 && uri.Fragment.Length == 0 && uri.Query.Length == 0 &&
                value.StartsWith(AssetPrefix, StringComparison.Ordinal) &&
                !value.Contains("..") && !value.Contains("\\") && uri.AbsolutePath.StartsWith("/" + Repo + "/releases/download/", StringComparison.Ordinal);
        }
        public static Release Verify(string envelopeJson, string publicKey, DateTime utcNow)
        {
            if (Encoding.UTF8.GetByteCount(envelopeJson) > MetadataLimit) throw new InvalidDataException("Update metadata is too large.");
            Envelope envelope = Json<Envelope>(envelopeJson);
            if (envelope == null || envelope.payload == null || envelope.signature == null) throw new InvalidDataException("Unsigned update metadata.");
            byte[] payload = Convert.FromBase64String(envelope.payload);
            byte[] signature = Convert.FromBase64String(envelope.signature);
            using (var rsa = new RSACryptoServiceProvider())
            {
                rsa.PersistKeyInCsp = false;
                rsa.FromXmlString(publicKey);
                if (rsa.KeySize < 2048 || !rsa.VerifyData(payload, CryptoConfig.MapNameToOID("SHA256"), signature))
                    throw new InvalidDataException("Release signature is invalid. Nothing was downloaded or installed.");
            }
            Release release = Json<Release>(new UTF8Encoding(false, true).GetString(payload));
            DateTime expires;
            if (release == null || release.schemaVersion != 1 || release.sequence <= 0 ||
                release.minimumUpdaterVersion < 1 || release.minimumUpdaterVersion > UpdaterVersion ||
                (release.channel != "stable" && release.channel != "beta") || release.architecture != "x64" ||
                release.commit == null || !Regex.IsMatch(release.commit, "^[0-9a-f]{40}$") ||
                string.IsNullOrWhiteSpace(release.version) || release.version.Length > 100 ||
                release.rpcVersion <= 0 || release.configurationVersion <= 0 ||
                release.notes == null || release.notes.Length > 30000 ||
                !DateTime.TryParse(release.expiresUtc, System.Globalization.CultureInfo.InvariantCulture,
                    System.Globalization.DateTimeStyles.AdjustToUniversal | System.Globalization.DateTimeStyles.AssumeUniversal, out expires) || expires <= utcNow ||
                release.packages == null || release.packages.Length == 0 || release.packages.Length > 2)
                throw new InvalidDataException("Release metadata is expired or unsupported. Check the GitHub release page.");
            var flavors = new HashSet<string>();
            foreach (var package in release.packages)
            {
                if (package == null || (package.flavor != "full" && package.flavor != "config") || !flavors.Add(package.flavor) ||
                    !IsAssetUrl(package.url) || !package.url.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) ||
                    !IsHash(package.sha256) || !IsHash(package.installManifestSha256) || package.size <= 0 || package.size > 2147483648L)
                    throw new InvalidDataException("Release package is invalid.");
            }
            return release;
        }
        public static string CanonicalRoot(string path)
        {
            string full = Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar);
            if (full.Length < 4 || !Directory.Exists(full)) throw new InvalidDataException("Installation folder does not exist.");
            for (var dir = new DirectoryInfo(full); dir != null; dir = dir.Parent)
                if ((dir.Attributes & FileAttributes.ReparsePoint) != 0) throw new InvalidDataException("An update cannot run through a linked folder.");
            return full;
        }
        public static string SafeFile(string root, string relative)
        {
            if (string.IsNullOrEmpty(relative) || Path.IsPathRooted(relative) || relative.Contains(":") || relative.Split('/', '\\').Any(p => p == ".." || p == "."))
                throw new InvalidDataException("Unsafe installation path.");
            string path = Path.GetFullPath(Path.Combine(root, relative));
            if (!path.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Path escapes installation.");
            for (string current = path; !string.Equals(current, root, StringComparison.OrdinalIgnoreCase); current = Path.GetDirectoryName(current))
                if ((File.Exists(current) || Directory.Exists(current)) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                    throw new InvalidDataException("Linked installation file.");
            return path;
        }
        public static Installed ReadInstalled(string root)
        {
            string path = SafeFile(root, "INSTALL-MANIFEST.json");
            if (new FileInfo(path).Length > MetadataLimit) throw new InvalidDataException("Installation manifest is too large.");
            Installed installed = Json<Installed>(File.ReadAllText(path));
            if (installed == null || installed.schemaVersion != 1 ||
                (installed.applicationId != FullId && installed.applicationId != ConfigId) || installed.dirty || installed.files == null)
                throw new InvalidDataException("Automatic updates require a clean released package. Use a current setup or portable ZIP first.");
            string expected = installed.applicationId == FullId ? "full" : "config";
            if (installed.flavor != null && installed.flavor != expected) throw new InvalidDataException("Installation flavor does not match its identity.");
            installed.flavor = expected;
            return installed;
        }
        public static string RegistryKey(string flavor)
        {
            string guid = flavor == "full" ? "42D852F1-70E9-43ED-8739-D61752106D59" : "BA15DBE8-210F-42AA-AE86-B4628395E77F";
            return @"Software\Microsoft\Windows\CurrentVersion\Uninstall\{" + guid + "}_is1";
        }
        public static bool IsPortableRoot(string root, string flavor)
        {
            using (var registry = RegistryKeyOpen(flavor))
            {
                string registered = registry == null ? null : registry.GetValue("Inno Setup: App Path") as string;
                bool portable = registered == null || !string.Equals(Path.GetFullPath(registered).TrimEnd(Path.DirectorySeparatorChar), root, StringComparison.OrdinalIgnoreCase);
                if (portable && Directory.GetFiles(root, "unins*.exe").Length != 0)
                    throw new InvalidOperationException("This folder has installer records belonging to another location or account. Run its matching setup to repair the registration.");
                return portable;
            }
        }
        static Microsoft.Win32.RegistryKey RegistryKeyOpen(string flavor)
        {
            using (var hive = Microsoft.Win32.RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, RegistryView.Registry64))
                return hive.OpenSubKey(RegistryKey(flavor));
        }
        public static string UpdateMode(Preferences preferences)
        {
            if (preferences.mode == "manual" || preferences.mode == "notify" || preferences.mode == "auto") return preferences.mode;
            return preferences.automatic ? "notify" : "manual";
        }
        public static bool CanAutoInstall(Preferences preferences, Release release, Installed installed, bool playerRunning)
        {
            return UpdateMode(preferences) == "auto" && release != null &&
                !(preferences.skippedSequence == release.sequence && preferences.skippedChannel == release.channel) &&
                release.rpcVersion == installed.rpcVersion && release.configurationVersion == installed.configurationVersion &&
                (!playerRunning || preferences.allowPlaybackRestart);
        }
        public static bool IsNewer(Release release, Installed installed, string channel, long highest)
        {
            return release.channel == channel && release.sequence > installed.updateSequence && release.sequence >= highest &&
                release.packages.Any(p => p.flavor == installed.flavor);
        }
        static bool AllowedRedirect(Uri uri)
        {
            return uri.Scheme == "https" && uri.IsDefaultPort && uri.UserInfo.Length == 0 &&
                (uri.Host == "github.com" || uri.Host == "api.github.com" || uri.Host == "release-assets.githubusercontent.com" || uri.Host == "objects.githubusercontent.com");
        }
        static HttpWebResponse Open(string url)
        {
            Uri uri = new Uri(url);
            for (int redirects = 0; redirects < 6; redirects++)
            {
                if (!AllowedRedirect(uri)) throw new InvalidDataException("Untrusted download redirect.");
                var request = (HttpWebRequest)WebRequest.Create(uri);
                request.UserAgent = "VideoProcessor-Updater/1";
                request.Accept = "application/vnd.github+json";
                request.Timeout = 20000;
                request.ReadWriteTimeout = 30000;
                request.AllowAutoRedirect = false;
                var response = (HttpWebResponse)request.GetResponse();
                if ((int)response.StatusCode >= 300 && (int)response.StatusCode <= 399)
                {
                    string location = response.Headers["Location"];
                    response.Dispose();
                    uri = new Uri(uri, location);
                    continue;
                }
                if (response.StatusCode != HttpStatusCode.OK) { response.Dispose(); throw new IOException("GitHub did not return the requested file."); }
                return response;
            }
            throw new IOException("Too many download redirects.");
        }
        public static byte[] GetBytes(string url, int limit)
        {
            using (var response = Open(url)) using (var input = response.GetResponseStream()) using (var output = new MemoryStream())
            {
                byte[] block = new byte[16384]; int read;
                while ((read = input.Read(block, 0, block.Length)) > 0)
                {
                    if (output.Length + read > limit) throw new InvalidDataException("Download exceeds the allowed size.");
                    output.Write(block, 0, read);
                }
                return output.ToArray();
            }
        }
        public static Release Discover(Installed installed, Preferences preferences)
        {
            Release selected = null;
            int badSignatures = 0;
            for (int page = 1; page <= 5; page++)
            {
                string json = Encoding.UTF8.GetString(GetBytes("https://api.github.com/repos/" + Repo + "/releases?per_page=30&page=" + page, MetadataLimit));
                var releases = Json<List<Dictionary<string, object>>>(json);
                if (releases == null) throw new InvalidDataException("Invalid GitHub release listing.");
                foreach (var item in releases)
                {
                    if (item.ContainsKey("draft") && (bool)item["draft"]) continue;
                    object assetsValue;
                    if (!item.TryGetValue("assets", out assetsValue)) continue;
                    foreach (var assetObject in (System.Collections.IEnumerable)assetsValue)
                    {
                        var asset = (Dictionary<string, object>)assetObject;
                        if ((string)asset["name"] != "vp-update.json") continue;
                        string url = (string)asset["browser_download_url"];
                        if (!IsAssetUrl(url)) continue;
                        Release offered;
                        try { offered = Verify(Encoding.UTF8.GetString(GetBytes(url, MetadataLimit)), PublicKey(), DateTime.UtcNow); }
                        catch (Exception)
                        { badSignatures++; continue; }
                        long highest; preferences.highest.TryGetValue(preferences.channel, out highest);
                        if (IsNewer(offered, installed, preferences.channel, highest) && (selected == null || offered.sequence > selected.sequence)) selected = offered;
                    }
                }
                if (releases.Count < 30) break;
            }
            if (selected == null && badSignatures != 0) throw new InvalidDataException("Some releases could not be verified or were expired. No eligible update was found.");
            if (selected != null) preferences.highest[preferences.channel] = selected.sequence;
            return selected;
        }
        public static void Download(Package package, string destination, Action<int> progress)
        {
            string partial = destination + ".partial";
            try
            {
                using (var response = Open(package.url)) using (var input = response.GetResponseStream())
                using (var output = new FileStream(partial, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    if (response.ContentLength > 0 && response.ContentLength != package.size) throw new InvalidDataException("Installer size does not match the signed release.");
                    byte[] block = new byte[65536]; int read, lastPercent = -1; long received = 0;
                    while ((read = input.Read(block, 0, block.Length)) > 0)
                    {
                        received += read;
                        if (received > package.size) throw new InvalidDataException("Installer exceeds its signed size.");
                        output.Write(block, 0, read);
                        int percent = (int)(received * 100 / package.size);
                        if (percent != lastPercent && progress != null) progress(percent);
                        lastPercent = percent;
                    }
                    if (received != package.size) throw new InvalidDataException("Installer download was incomplete.");
                }
                if (!string.Equals(FileHash(partial), package.sha256, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Installer hash does not match the signed release.");
                File.Move(partial, destination);
            }
            finally { if (File.Exists(partial)) File.Delete(partial); }
        }
        public static void VerifyInstalled(string root, Release release, Package package)
        {
            if (!string.Equals(FileHash(SafeFile(root, "INSTALL-MANIFEST.json")), package.installManifestSha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Installed manifest does not match the selected release. Rerun setup to repair; applications were not restarted.");
            var installed = ReadInstalled(root);
            if (installed.sourceCommit != release.commit || installed.updateSequence != release.sequence || installed.flavor != package.flavor)
                throw new InvalidDataException("Installed build identity does not match the selected release.");
            foreach (var entry in installed.files)
                if (entry.policy == "managed" && (!IsHash(entry.sha256) || !string.Equals(FileHash(SafeFile(root, entry.path)), entry.sha256, StringComparison.OrdinalIgnoreCase)))
                    throw new InvalidDataException("Installed file verification failed: " + entry.path);
        }
        public static void Save(string path, object value)
        {
            string temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
            File.WriteAllText(temporary, Serialize(value), new UTF8Encoding(false));
            if (File.Exists(path)) File.Replace(temporary, path, null); else File.Move(temporary, path);
        }
    }
}

