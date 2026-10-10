using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using VideoProcessor.Update;
public static class UpdateControlTests
{
 static int checks;
 static void Assert(bool condition, string label) { if(!condition)throw new Exception(label); checks++; Console.WriteLine("PASS "+label); }
 static string Request(string exe,string root,string json)
 {
  using(var p=Process.Start(new ProcessStartInfo(exe,"--root "+Program.Quote(root)+" --control "+Program.Quote(json)) { UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true }))
  {
   if(!p.WaitForExit(10000)){p.Kill();throw new Exception("Control bridge timed out");}
   return p.StandardOutput.ReadToEnd().Trim();
  }
 }
 public static int Main(string[] args)
 {
  string exe=Path.GetFullPath(args[0]);
  foreach(string flavor in new[]{"full","config"})
  {
   string root=Path.Combine(Path.GetTempPath(),"vp-update-control-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
   File.WriteAllText(Path.Combine(root,"INSTALL-MANIFEST.json"),UpdateCore.Serialize(new Installed{schemaVersion=1,applicationId=flavor=="full"?UpdateCore.FullId:UpdateCore.ConfigId,flavor=flavor,coreVersion="control-test",rpcVersion=2,configurationVersion=1,files=new InstalledFile[0]}));
   using(var worker=Process.Start(new ProcessStartInfo(exe,"--worker --service --root "+Program.Quote(root)) {UseShellExecute=false,CreateNoWindow=true}))
   {
    try
    {
     string state="";
     for(int attempt=0;attempt<5;attempt++) {state=Request(exe,root,"{\"command\":\"state\"}");if(state.Contains("\"flavor\""))break;Thread.Sleep(200);}
     Assert(state.Contains("\"flavor\":\""+flavor+"\""),flavor+" package works without VP executable");
     state=Request(exe,root,"{\"command\":\"settings\",\"mode\":\"manual\",\"channel\":\"stable\",\"allowPlaybackRestart\":false}");
     Assert(state.Contains("\"mode\":\"manual\"")&&state.Contains("\"channel\":\"stable\""),flavor+" preferences round trip through control bridge");
     Assert(Request(exe,root,"{\"command\":\"settings\",\"mode\":\"unknown\",\"channel\":\"beta\"}").Contains("\"error\""),flavor+" malformed preferences rejected");
     state=Request(exe,root,"{\"command\":\"state\"}");Assert(state.Contains("\"mode\":\"manual\"")&&state.Contains("\"channel\":\"stable\""),flavor+" rejected settings do not modify preferences");
     Assert(Request(exe,root,"{\"command\":\"install\"}").Contains("\"error\""),flavor+" cannot install without a verified download");
     worker.Refresh();Assert(worker.MainWindowHandle==IntPtr.Zero,flavor+" helper never opens an updater window");
     Request(exe,root,"{\"command\":\"detach\"}");Assert(worker.WaitForExit(5000),flavor+" idle helper exits when Config panel detaches");
    }
    finally { if(!worker.HasExited){worker.Kill();worker.WaitForExit();} }
   }
   var manifest=UpdateCore.ReadInstalled(root); manifest.dirty=true;
   File.WriteAllText(Path.Combine(root,"INSTALL-MANIFEST.json"),UpdateCore.Serialize(manifest));
   Assert(Request(exe,root,"{\"command\":\"state\"}").Contains("clean released package"),flavor+" invalid installation gives actionable error instead of timeout");
   Directory.Delete(root,true);
  }
  Console.WriteLine(checks+" update control checks passed");return 0;
 }
}
