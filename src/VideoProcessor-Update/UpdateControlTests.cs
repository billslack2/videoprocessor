using System;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Security.Principal;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using VideoProcessor.Update;
public static class UpdateControlTests
{
 static int checks;
 static void Assert(bool condition, string label) { if(!condition)throw new Exception(label); checks++; Console.WriteLine("PASS "+label); }
 static void RecordRoot(string root)
 {
  string list=Environment.GetEnvironmentVariable("VP_UPDATE_TEST_ROOT_LIST");
  if(!string.IsNullOrEmpty(list))File.AppendAllText(list,root+Environment.NewLine,new UTF8Encoding(false));
 }
 static string Request(string exe,string root,string json)
 {
  using(var p=Process.Start(new ProcessStartInfo(exe,"--root "+Program.Quote(root)+" --control "+Program.Quote(json)) { UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true }))
  {
   if(!p.WaitForExit(10000)){p.Kill();throw new Exception("Control bridge timed out");}
   return p.StandardOutput.ReadToEnd().Trim();
  }
 }
 static string NewRoot(string label,string flavor)
 {
  string root=Path.Combine(Path.GetTempPath(),label+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
  RecordRoot(root);
  File.WriteAllText(Path.Combine(root,"INSTALL-MANIFEST.json"),UpdateCore.Serialize(new Installed{schemaVersion=1,applicationId=flavor=="full"?UpdateCore.FullId:UpdateCore.ConfigId,flavor=flavor,coreVersion="control-test",rpcVersion=2,configurationVersion=1,files=new InstalledFile[0]}));
  return root;
 }
 static int CountImage(string exe)
 {
  int count=0;
  foreach(var process in Process.GetProcessesByName(Path.GetFileNameWithoutExtension(exe)))
  {
   try { if(string.Equals(Path.GetFullPath(process.MainModule.FileName),exe,StringComparison.OrdinalIgnoreCase))count++; }
   catch { }
   finally { process.Dispose(); }
  }
  return count;
 }
 static string ReadLine(Process process,int timeoutMs)
 {
  Task<string> read=process.StandardOutput.ReadLineAsync();
  if(!read.Wait(timeoutMs))throw new Exception("Persistent control bridge response timed out");
  return read.Result;
 }
 static bool WaitForImageCount(string exe,int expected,int timeoutMs)
 {
  DateTime deadline=DateTime.UtcNow.AddMilliseconds(timeoutMs);
  while(DateTime.UtcNow<deadline){if(CountImage(exe)==expected)return true;Thread.Sleep(100);}
  return CountImage(exe)==expected;
 }
 static NamedPipeClientStream OpenPipe(string id)
 {
  var pipe=new NamedPipeClientStream(".","VideoProcessor.Update.Control."+id,PipeDirection.InOut,PipeOptions.Asynchronous,TokenImpersonationLevel.Impersonation);
  pipe.Connect(3000);return pipe;
 }
 static bool IsClosed(NamedPipeClientStream pipe)
 {
  try
  {
   var value=new byte[4096];int total=0;
   while(total<1024*1024){Task<int> read=pipe.ReadAsync(value,0,value.Length);if(!read.Wait(3000))return false;int count=read.Result;if(count==0)return true;total+=count;}
   return false;
  }
  catch(AggregateException ex){return ex.InnerException is IOException;}
  catch(IOException){return true;}
 }
 public static int Main(string[] args)
 {
  Assert(AppLifecycle.RestartArguments(new RunningApp{config=false,arguments="--fullscreen"})=="--fullscreen","VP restart preserves its launch arguments");
  Assert(AppLifecycle.RestartArguments(new RunningApp{config=true,restoreInTray=true,arguments="--updates"}).EndsWith(" --restore-tray"),"hidden Config overrides an earlier Updates launch");
  Assert(AppLifecycle.RestartArguments(new RunningApp{config=true,restoreInTray=false,arguments="--background"}).EndsWith(" --restore-visible"),"visible Config overrides an earlier background launch");
  string exe=Path.GetFullPath(args[0]);
  string raceRoot=NewRoot("vp-update-start-race-","full");
  var starters=new Process[6];
  try
  {
   for(int i=0;i<starters.Length;i++)starters[i]=Process.Start(new ProcessStartInfo(exe,"--root "+Program.Quote(raceRoot)+" --service"){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true});
   for(int i=0;i<starters.Length;i++)
   {
    if(!starters[i].WaitForExit(20000)){starters[i].Kill();throw new Exception("Concurrent worker launcher did not finish");}
    string startupError="";Task<string> startupErrorRead=starters[i].StandardError.ReadLineAsync();if(startupErrorRead.Wait(250))startupError=startupErrorRead.Result;
    Assert(starters[i].ExitCode==0,"concurrent service launcher "+i+" completed (exit "+starters[i].ExitCode+", "+startupError+")");
   }
   string state=Request(exe,raceRoot,"{\"command\":\"state\"}");
   Assert(state.Contains("\"flavor\":\"full\""),"cold-start race leaves a reachable worker");
   Assert(CountImage(exe)==1,"cold-start race leaves exactly one updater worker process");
   string id=UpdateCore.Hash(Encoding.UTF8.GetBytes(WindowsIdentity.GetCurrent().User.Value+"|"+raceRoot.ToLowerInvariant()));
   string runner=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"VideoProcessor","Updates",id,"runner");
   Assert(!Directory.Exists(runner),"ordinary worker startup creates no GUID runner copies");
   string stateCommand="{\"command\":\"state\"}\n";

   using(var session=Process.Start(new ProcessStartInfo(exe,"--root "+Program.Quote(raceRoot)+" --control-session")
    {UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true}))
   {
    string command="{\"command\":\"state\"}\n";
    session.StandardInput.Write(command+command+command+command+command);session.StandardInput.Flush();
    for(int i=0;i<5;i++){string reply=ReadLine(session,8000);Assert(reply.Contains("\"flavor\":\"full\""),"persistent bridge returns batched poll "+i+": "+reply);}
    Assert(!session.HasExited,"polling stays in one persistent bridge process");
    string settings="{\"command\":\"settings\",\"mode\":\"manual\",\"channel\":\"stable\",\"allowPlaybackRestart\":false}\n";
    session.StandardInput.Write(settings);session.StandardInput.Flush();
    Assert(ReadLine(session,8000).Contains("Update preferences saved"),"persistent bridge acknowledges first paced settings request");
    Thread.Sleep(200);
    session.StandardInput.Write(settings);session.StandardInput.Flush();
    Assert(ReadLine(session,8000).Contains("Update preferences saved"),"persistent bridge reads a new paced request after its first response");

    var singleBurst=new StringBuilder();for(int i=0;i<64;i++)singleBurst.Append(stateCommand);
    using(var stalled=OpenPipe(id))
    {
     byte[] burst=Encoding.UTF8.GetBytes(singleBurst.ToString()); stalled.Write(burst,0,burst.Length); stalled.Flush();
     Thread.Sleep(1000);
     Assert(Request(exe,raceRoot,"{\"command\":\"state\"}").Contains("\"flavor\":\"full\""),"short command remains available beside a stalled response reader");
    }
    session.StandardInput.Close();
    Assert(session.WaitForExit(8000),"closing a bridge session sends detach and exits");
   }
   Assert(WaitForImageCount(exe,0,5000),"detached session shuts down its idle worker");

   using(var restart=Process.Start(new ProcessStartInfo(exe,"--root "+Program.Quote(raceRoot)+" --service"){UseShellExecute=false,CreateNoWindow=true}))
   {Assert(restart.WaitForExit(15000)&&restart.ExitCode==0,"service reconnect starts a fresh singleton worker");}
   Assert(WaitForImageCount(exe,1,5000),"reconnected service has one worker process");
   using(var keeper=Process.Start(new ProcessStartInfo(exe,"--root "+Program.Quote(raceRoot)+" --control-session")
    {UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true}))
   {
   keeper.StandardInput.Write("{\"command\":\"state\"}\n");keeper.StandardInput.Flush();
   string keeperReply=ReadLine(keeper,8000);
   Assert(keeperReply.Contains("\"flavor\":\"full\""),"reconnected Config session acquires a worker lease: "+keeperReply);
   using(var malformed=OpenPipe(id))
   {
    byte[] badUtf8={255,10};malformed.Write(badUtf8,0,badUtf8.Length);malformed.Flush();
    Assert(IsClosed(malformed),"server closes a malformed UTF-8 frame without dispatch");
   }
   using(var oversized=OpenPipe(id))
   {
    byte[] tooLarge=Encoding.UTF8.GetBytes(new string('x',8193)+"\n");oversized.Write(tooLarge,0,tooLarge.Length);oversized.Flush();
    Assert(IsClosed(oversized),"server closes an over-limit raw frame by encoded byte count");
   }
   using(var partial=OpenPipe(id))
   {
    byte[] incomplete=Encoding.UTF8.GetBytes("{\"command\":\"state\"");partial.Write(incomplete,0,incomplete.Length);partial.Flush();
   }
   Thread.Sleep(300);
   Assert(Request(exe,raceRoot,"{\"command\":\"state\"}").Contains("\"flavor\":\"full\""),"partial frame disconnect releases its connection slot");

   var stalledClients=new System.Collections.Generic.List<NamedPipeClientStream>();
   try
   {
    var burstText=new StringBuilder();for(int i=0;i<64;i++)burstText.Append(stateCommand);
    byte[] fullBurst=Encoding.UTF8.GetBytes(burstText.ToString());
    for(int i=0;i<7;i++){var client=OpenPipe(id);stalledClients.Add(client);client.Write(fullBurst,0,fullBurst.Length);client.Flush();}
    bool allSlotsBounded=false;
    using(var blocked= new NamedPipeClientStream(".","VideoProcessor.Update.Control."+id,PipeDirection.InOut,PipeOptions.Asynchronous,TokenImpersonationLevel.Impersonation))
    {try{blocked.Connect(500);}catch(TimeoutException){allSlotsBounded=true;}catch(IOException){allSlotsBounded=true;}}
    Assert(allSlotsBounded,"seven stalled readers and one session fill the bounded pipe slots");
    Thread.Sleep(6000);
    bool allClosed=true;
    foreach(var client in stalledClients)if(!IsClosed(client))allClosed=false;
    Assert(allClosed,"server closes all stalled response readers after its bounded write timeout");
    Assert(Request(exe,raceRoot,"{\"command\":\"state\"}").Contains("\"flavor\":\"full\""),"expired stalled clients release every bounded server slot");
   }
   finally{foreach(var client in stalledClients)client.Dispose();}
   using(var second=Process.Start(new ProcessStartInfo(exe,"--root "+Program.Quote(raceRoot)+" --control-session")
    {UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true}))
   {
    second.StandardInput.Write("{\"command\":\"state\"}\n");second.StandardInput.Flush();
    Assert(ReadLine(second,8000).Contains("\"flavor\":\"full\""),"second visible Config session attaches to one worker");
    keeper.StandardInput.Close();Assert(keeper.WaitForExit(8000),"graceful bridge EOF releases its session lease");
    Assert(CountImage(exe)==2,"one session closing does not release another visible session");
    second.Kill();second.WaitForExit();
    Assert(WaitForImageCount(exe,0,5000),"forced bridge termination releases its session lease and idle worker");
   }
   }
  }
  finally
  {
   foreach(var starter in starters)if(starter!=null)starter.Dispose();
   foreach(var worker in Process.GetProcessesByName(Path.GetFileNameWithoutExtension(exe)))
   { try { if(string.Equals(Path.GetFullPath(worker.MainModule.FileName),exe,StringComparison.OrdinalIgnoreCase))worker.Kill(); } catch{} finally {worker.Dispose();} }
   if(Directory.Exists(raceRoot))Directory.Delete(raceRoot,true);
  }
  foreach(string flavor in new[]{"full","config"})
  {
   string root=NewRoot("vp-update-control-",flavor);
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
     Assert(Request(exe,root,"{\"command\":\""+new string('é',5000)+"\"}").Contains("\"error\""),flavor+" UTF-8 request limit counts encoded bytes");
     worker.Refresh();Assert(worker.MainWindowHandle==IntPtr.Zero,flavor+" helper never opens an updater window");
     using(var session=Process.Start(new ProcessStartInfo(exe,"--root "+Program.Quote(root)+" --control-session")
      {UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true}))
     {
      session.StandardInput.Write("{\"command\":\"state\"}\n");session.StandardInput.Flush();
      Assert(ReadLine(session,8000).Contains("\"flavor\":\""+flavor+"\""),flavor+" panel session attaches to helper");
      session.StandardInput.Close();Assert(session.WaitForExit(8000),flavor+" bridge EOF releases its session lease");
     }
     Assert(worker.WaitForExit(5000),flavor+" idle helper exits after its Config session detaches");
    }
    finally { if(!worker.HasExited){worker.Kill();worker.WaitForExit();} }
   }
   var manifest=UpdateCore.ReadInstalled(root); manifest.dirty=true;
   File.WriteAllText(Path.Combine(root,"INSTALL-MANIFEST.json"),UpdateCore.Serialize(manifest));
   Assert(Request(exe,root,"{\"command\":\"state\"}").Contains("clean released package"),flavor+" invalid installation gives actionable error instead of timeout");
   using(var session=Process.Start(new ProcessStartInfo(exe,"--root "+Program.Quote(root)+" --control-session")
    {UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true}))
   {
    Task<string> responseRead=session.StandardOutput.ReadLineAsync();
    if(!responseRead.Wait(8000)){session.Kill();throw new Exception(flavor+" invalid persistent session did not return an installation error");}
    string response=responseRead.Result??"";
    Assert(response.Contains("Automatic updates require a clean released package"),flavor+" persistent session reports the clean-package requirement before attach");
    Assert(session.WaitForExit(5000)&&session.ExitCode!=0,flavor+" invalid persistent session exits without entering reconnect retries");
   }
   Directory.Delete(root,true);
  }
  Console.WriteLine(checks+" update control checks passed");return 0;
 }
}
