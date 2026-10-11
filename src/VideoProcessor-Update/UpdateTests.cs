using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using VideoProcessor.Update;
public static class UpdateTests
{
    static int count;
    static void Assert(bool value, string label) { if (!value) throw new Exception(label); count++; Console.WriteLine("PASS " + label); }
    static void Reject(Action action, string label) { bool failed=false; try { action(); } catch { failed=true; } Assert(failed,label); }
    static string Signed(Release value, RSACryptoServiceProvider key)
    {
        byte[] bytes=Encoding.UTF8.GetBytes(UpdateCore.Serialize(value));
        return UpdateCore.Serialize(new Envelope {payload=Convert.ToBase64String(bytes),signature=Convert.ToBase64String(key.SignData(bytes,CryptoConfig.MapNameToOID("SHA256")))});
    }
    public static int Main()
    {
        using (var key=new RSACryptoServiceProvider(2048))
        {
            key.PersistKeyInCsp=false; string pub=key.ToXmlString(false);
            var release=new Release {schemaVersion=1,minimumUpdaterVersion=1,sequence=42,version="1.4.00-beta",commit=new string('a',40),channel="beta",architecture="x64",rpcVersion=2,configurationVersion=1,expiresUtc=DateTime.UtcNow.AddDays(1).ToString("o"),notes="Release notes",packages=new[]{new Package{flavor="full",url=UpdateCore.AssetPrefix+"test/setup.exe",size=100,sha256=new string('a',64),installManifestSha256=new string('b',64)}}};
            var prefs = new Preferences();
            var current = new Installed { rpcVersion=2, configurationVersion=1 };
            Assert(UpdateCore.UpdateMode(prefs)=="notify", "default mode only notifies");
            prefs.automatic=false; Assert(UpdateCore.UpdateMode(prefs)=="manual", "old disabled preference stays disabled");
            prefs.mode="auto";
            Assert(UpdateCore.CanAutoInstall(prefs,release,current,false), "explicit automatic mode permits idle update");
            Assert(!UpdateCore.CanAutoInstall(prefs,release,current,true), "automatic mode waits for running VP by default");
            prefs.allowPlaybackRestart=true;
            Assert(UpdateCore.CanAutoInstall(prefs,release,current,true), "explicit restart preference permits unattended VP restart");
            prefs.mode="notify"; Assert(!UpdateCore.CanAutoInstall(prefs,release,current,false), "notification mode cannot install unattended");
            prefs.mode="manual"; Assert(!UpdateCore.CanAutoInstall(prefs,release,current,false), "manual mode cannot install unattended");
            prefs.mode="auto"; current.rpcVersion=99;
            Assert(!UpdateCore.CanAutoInstall(prefs,release,current,false), "incompatible remote protocol requires manual review");
            current.rpcVersion=2; prefs.skippedSequence=release.sequence; prefs.skippedChannel=release.channel;
            Assert(!UpdateCore.CanAutoInstall(prefs,release,current,false), "skipped release cannot install unattended");
            string signed=Signed(release,key);
            Assert(UpdateCore.Verify(signed,pub,DateTime.UtcNow).sequence==42,"valid pinned signature accepted");
            var envelope=UpdateCore.Json<Envelope>(signed); byte[] changed=Convert.FromBase64String(envelope.payload); changed[10]^=1; envelope.payload=Convert.ToBase64String(changed);
            Reject(()=>UpdateCore.Verify(UpdateCore.Serialize(envelope),pub,DateTime.UtcNow),"tampered metadata rejected before parsing");
            using(var other=new RSACryptoServiceProvider(2048)) {other.PersistKeyInCsp=false; Reject(()=>UpdateCore.Verify(signed,other.ToXmlString(false),DateTime.UtcNow),"untrusted key rejected");}
            Reject(()=>UpdateCore.Verify(signed,pub,DateTime.UtcNow.AddDays(2)),"expired metadata rejected");
            var installed=new Installed{flavor="full",updateSequence=41,coreVersion="1.4.00-beta"};
            Assert(UpdateCore.IsNewer(release,installed,"beta",0),"same version newer build offered");
            Assert(!UpdateCore.IsNewer(release,installed,"stable",0),"beta not offered on stable channel");
            Assert(!UpdateCore.IsNewer(release,installed,"beta",43),"previously seen newer sequence prevents replay");
            installed.updateSequence=42;Assert(!UpdateCore.IsNewer(release,installed,"beta",0),"same build not offered again");
            installed.updateSequence=41;installed.flavor="config";Assert(!UpdateCore.IsNewer(release,installed,"beta",0),"full setup not offered to Config-only install");
            Assert(!UpdateCore.IsAssetUrl("https://github.com/other/repo/releases/download/v/setup.exe"),"foreign repository rejected");
            Assert(!UpdateCore.IsAssetUrl("http://github.com/billslack2/videoprocessor/releases/download/v/setup.exe"),"HTTP rejected");
            Assert(!UpdateCore.IsAssetUrl(UpdateCore.AssetPrefix+"v/../setup.exe"),"traversal URL rejected");
            release.packages[0].size=-1;Reject(()=>UpdateCore.Verify(Signed(release,key),pub,DateTime.UtcNow),"negative package size rejected");release.packages[0].size=100;
            release.packages=new[]{release.packages[0],release.packages[0]};Reject(()=>UpdateCore.Verify(Signed(release,key),pub,DateTime.UtcNow),"duplicate package flavor rejected");release.packages=new[]{release.packages[0]};
            release.minimumUpdaterVersion=2;Reject(()=>UpdateCore.Verify(Signed(release,key),pub,DateTime.UtcNow),"unsupported updater version rejected");release.minimumUpdaterVersion=1;
            string root=Path.Combine(Path.GetTempPath(),"vp-update-tests-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
            try
            {
                Reject(()=>UpdateCore.SafeFile(root,"../escape.exe"),"installation path traversal rejected");
                string exe=Path.Combine(root,"VideoProcessor.exe");File.WriteAllText(exe,"fixture");
                var manifest=new Installed {schemaVersion=1,applicationId=UpdateCore.FullId,sourceCommit=release.commit,updateSequence=release.sequence,flavor="full",files=new[]{new InstalledFile{path="VideoProcessor.exe",policy="managed",sha256=UpdateCore.FileHash(exe)}}};
                string inventory=Path.Combine(root,"INSTALL-MANIFEST.json");File.WriteAllText(inventory,UpdateCore.Serialize(manifest));
                release.packages[0].installManifestSha256=UpdateCore.FileHash(inventory);
                UpdateCore.VerifyInstalled(root,release,release.packages[0]);Assert(true,"matching installed package verified");
                File.WriteAllText(exe,"changed");Reject(()=>UpdateCore.VerifyInstalled(root,release,release.packages[0]),"mismatched installed binary prevents restart");
                manifest.applicationId=UpdateCore.ConfigId;File.WriteAllText(inventory,UpdateCore.Serialize(manifest));Reject(()=>UpdateCore.ReadInstalled(root),"flavor and installation identity mismatch rejected");
                string preferences=Path.Combine(root,"preferences.json");UpdateCore.Save(preferences,new Preferences());UpdateCore.Save(preferences,new Preferences{automatic=false});Assert(!UpdateCore.Json<Preferences>(File.ReadAllText(preferences)).automatic,"atomic shared preferences replacement");
            }
            finally {Directory.Delete(root,true);}
        }
        Console.WriteLine(count+" updater checks passed");return 0;
    }
}
