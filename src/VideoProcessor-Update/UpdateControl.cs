using System;
using System.IO;
using System.IO.Pipes;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Threading.Tasks;
namespace VideoProcessor.Update
{
    // Local, current-user-only command channel. No listener on the LAN.
    public sealed class UpdateControl : IDisposable
    {
        readonly string name;
        readonly UpdateWindow window;
        volatile bool stopped;
        NamedPipeServerStream server;
        public UpdateControl(string id, UpdateWindow owner)
        {
            name = "VideoProcessor.Update.Control." + id; window = owner;
            var handle = window.Handle;
            Task.Run((Func<Task>)Serve);
        }
        public static string Send(string id, string command)
        {
            if (command.Length > 8192) throw new InvalidDataException("Update command is too large.");
            using (var pipe = new NamedPipeClientStream(".", "VideoProcessor.Update.Control." + id, PipeDirection.InOut, PipeOptions.Asynchronous))
            {
                pipe.Connect(1500);
                byte[] request = Encoding.UTF8.GetBytes(command + "\n"); pipe.Write(request, 0, request.Length); pipe.Flush();
                var read = ReadLine(pipe, 1024 * 1024);
                if (!read.Wait(5000)) throw new IOException("Update helper did not respond.");
                return read.Result;
            }
        }
        static async Task<string> ReadLine(Stream stream, int limit)
        {
            using (var result = new MemoryStream())
            {
                byte[] value = new byte[4096];
                while (result.Length < limit)
                {
                    int count = await stream.ReadAsync(value, 0, value.Length);
                    if (count == 0) throw new IOException("Update connection closed.");
                    int newline = Array.IndexOf(value, (byte)10, 0, count);
                    int length = newline < 0 ? count : newline;
                    if (result.Length + length > limit) throw new InvalidDataException("Update message is too large.");
                    result.Write(value, 0, length);
                    if (newline >= 0) return new UTF8Encoding(false, true).GetString(result.ToArray());
                }
                throw new InvalidDataException("Update message is too large.");
            }
        }
        async Task Serve()
        {
            while (!stopped)
            {
                try
                {
                    var security = new PipeSecurity();
                    security.SetAccessRuleProtection(true, false);
                    security.AddAccessRule(new PipeAccessRule(WindowsIdentity.GetCurrent().User, PipeAccessRights.FullControl, AccessControlType.Allow));
                    using (var pipe = new NamedPipeServerStream(name, PipeDirection.InOut, 1, PipeTransmissionMode.Byte,
                        PipeOptions.Asynchronous, 4096, 4096, security))
                    {
                        server = pipe; await pipe.WaitForConnectionAsync();
                        var read = ReadLine(pipe, 8192);
                        if (await Task.WhenAny(read, Task.Delay(5000)) != read) continue;
                        string command = await read;
                        var completion = new TaskCompletionSource<string>();
                        window.BeginInvoke((Action)(() => {
                            try { completion.TrySetResult(window.ControlRequest(command)); }
                            catch (Exception ex) { completion.TrySetResult(UpdateCore.Serialize(new { error = ex.Message })); }
                        }));
                        if (await Task.WhenAny(completion.Task, Task.Delay(5000)) != completion.Task) continue;
                        byte[] response = Encoding.UTF8.GetBytes(await completion.Task + "\n");
                        await pipe.WriteAsync(response, 0, response.Length); await pipe.FlushAsync();
                    }
                }
                catch { }
                if (!stopped) await Task.Delay(100);
            }
        }
        public void Dispose() { stopped = true; var pipe = server; if (pipe != null) pipe.Dispose(); }
    }
    public sealed class UpdateCommand
    {
        public string command, mode, channel;
        public bool allowPlaybackRestart;
    }
}
