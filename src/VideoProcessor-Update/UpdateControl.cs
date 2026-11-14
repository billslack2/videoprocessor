using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace VideoProcessor.Update
{
    // Local, current-user-only command channel. No listener on the LAN.
    public sealed class UpdateControl : IDisposable
    {
        const int MaxClients = 8;
        const int MaxQueuedDispatches = 8;
        const int RequestLimit = 8192;
        const int ResponseLimit = 1024 * 1024;
        const int ClientIdleLimitMs = 10000;
        const int RequestTimeoutMs = 5000;
        static readonly UTF8Encoding StrictUtf8 = new UTF8Encoding(false, true);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr GetStdHandle(int standardHandle);

        [StructLayout(LayoutKind.Sequential)]
        struct IoStatusBlock { public IntPtr status; public UIntPtr information; }

        [DllImport("ntdll.dll")]
        static extern int NtQueryInformationFile(SafeFileHandle file, out IoStatusBlock ioStatus,
            out uint information, uint length, int informationClass);

        static bool IsOverlappedInput(SafeFileHandle handle)
        {
            IoStatusBlock ioStatus;
            uint mode;
            int status = NtQueryInformationFile(handle, out ioStatus, out mode, sizeof(uint), 16); // FileModeInformation
            if (status < 0) throw new IOException("Could not determine the inherited update bridge input mode (NTSTATUS 0x" +
                status.ToString("X8", System.Globalization.CultureInfo.InvariantCulture) + ").");
            const uint synchronousIoAlert = 0x10, synchronousIoNonAlert = 0x20;
            return (mode & (synchronousIoAlert | synchronousIoNonAlert)) == 0;
        }

        readonly string name;
        readonly string currentSid;
        readonly UpdateWindow window;
        readonly object pipeLock = new object();
        readonly HashSet<NamedPipeServerStream> pipes = new HashSet<NamedPipeServerStream>();
        readonly SemaphoreSlim slots = new SemaphoreSlim(MaxClients, MaxClients);
        readonly SemaphoreSlim dispatchSlots = new SemaphoreSlim(MaxQueuedDispatches, MaxQueuedDispatches);
        readonly CancellationTokenSource stopping = new CancellationTokenSource();
        volatile bool stopped;
        NamedPipeServerStream accepting;

        public UpdateControl(string id, UpdateWindow owner)
        {
            name = "VideoProcessor.Update.Control." + id;
            currentSid = WindowsIdentity.GetCurrent().User.Value;
            window = owner;
            var handle = window.Handle;
            Task.Run((Func<Task>)Serve);
        }

        public static string Send(string id, string command)
        {
            using (var connection = new ControlConnection(id))
                return connection.Request(command, 1500, RequestTimeoutMs);
        }

        public static bool Probe(string id, int timeoutMs)
        {
            using (var pipe = new NamedPipeClientStream(".", "VideoProcessor.Update.Control." + id,
                PipeDirection.InOut, PipeOptions.Asynchronous, TokenImpersonationLevel.Impersonation))
            {
                try { pipe.Connect(timeoutMs); return true; }
                catch (TimeoutException) { return false; }
                catch (IOException) { return false; }
            }
        }

        // Qt owns one bridge process per visible Update panel. It sends one JSON
        // request per line on stdin and receives one JSON response per line.
        public static void RunSession(string id)
        {
            Console.Out.Flush();
            IntPtr standardInput = GetStdHandle(-10); // STD_INPUT_HANDLE
            if (standardInput == IntPtr.Zero || standardInput == new IntPtr(-1))
                throw new IOException("The update bridge has no standard input pipe.");
            var inputHandle = new SafeFileHandle(standardInput, false);
            bool overlappedInput = IsOverlappedInput(inputHandle);
            using (var inputStream = new FileStream(inputHandle, FileAccess.Read, 4096, overlappedInput))
            using (var connection = new ControlConnection(id))
            {
                var input = new BoundedLineReader(inputStream);
                string sessionId = Guid.NewGuid().ToString("N");
                bool attached = false;
                try
                {
                    string attach = RequestSession(connection, UpdateCore.Serialize(new { command = "attach", sessionId }));
                    var attachReply = UpdateCore.Json<Dictionary<string, object>>(attach);
                    if (attachReply == null || attachReply.ContainsKey("error"))
                        throw new IOException(attachReply != null && attachReply.ContainsKey("error")
                            ? Convert.ToString(attachReply["error"]) : "Update helper returned an invalid attach response.");
                    attached = true;
                    while (true)
                    {
                        byte[] frame = input.ReadLineAsync(RequestLimit).GetAwaiter().GetResult();
                        if (frame == null) break;
                        string command = StrictUtf8.GetString(frame);
                        string response;
                        try { response = RequestSession(connection, command); }
                        catch (Exception ex)
                        {
                            connection.Reset();
                            response = ErrorResponse(ex, ex is IOException || ex is TimeoutException);
                            Console.Out.Write(response);
                            Console.Out.Write('\n');
                            Console.Out.Flush();
                            attached = false;
                            break; // The request may have completed; never replay it on a new pipe.
                        }
                        Console.Out.Write(response);
                        Console.Out.Write('\n');
                        Console.Out.Flush();
                    }
                }
                catch (Exception ex)
                {
                    Console.Out.Write(ErrorResponse(ex, false));
                    Console.Out.Write('\n');
                    Console.Out.Flush();
                }

                // Closing the visible Config session explicitly releases the
                // worker's hosted state. The worker itself remains alive while
                // an install or automatic operation is in progress.
                if (attached)
                {
                    try { RequestSession(connection, UpdateCore.Serialize(new { command = "detach", sessionId })); }
                    catch { }
                }
            }
        }

        static string ErrorResponse(Exception ex, bool connectionError)
        {
            return UpdateCore.Serialize(new { error = ex.Message, connectionError = connectionError });
        }

        static string RequestSession(ControlConnection connection, string command)
        {
            DateTime deadline = DateTime.UtcNow.AddMilliseconds(5500);
            ControlConnectException last = null;
            while (true)
            {
                try { return connection.Request(command, 250, RequestTimeoutMs); }
                catch (ControlConnectException ex)
                {
                    last = ex;
                    if (DateTime.UtcNow >= deadline) throw last;
                    Thread.Sleep(100);
                }
            }
        }

        static byte[] EncodeFrame(string text, int limit)
        {
            if (text == null || text.IndexOf('\n') >= 0 || text.IndexOf('\r') >= 0)
                throw new InvalidDataException("Update message must be one line.");
            byte[] payload = StrictUtf8.GetBytes(text);
            if (payload.Length > limit) throw new InvalidDataException("Update message is too large.");
            var frame = new byte[payload.Length + 1];
            Buffer.BlockCopy(payload, 0, frame, 0, payload.Length);
            frame[frame.Length - 1] = 10;
            return frame;
        }

        static async Task<T> Within<T>(Task<T> operation, int timeoutMs, string message)
        {
            if (await Task.WhenAny(operation, Task.Delay(timeoutMs)).ConfigureAwait(false) != operation)
                throw new TimeoutException(message);
            return await operation.ConfigureAwait(false);
        }

        static async Task Within(Task operation, int timeoutMs, string message)
        {
            if (await Task.WhenAny(operation, Task.Delay(timeoutMs)).ConfigureAwait(false) != operation)
                throw new TimeoutException(message);
            await operation.ConfigureAwait(false);
        }

        static async Task WriteFrame(Stream stream, string text, int limit)
        {
            byte[] frame = EncodeFrame(text, limit);
            await stream.WriteAsync(frame, 0, frame.Length).ConfigureAwait(false);
            await stream.FlushAsync().ConfigureAwait(false);
        }

        async Task Serve()
        {
            int setupErrors = 0;
            while (!stopped)
            {
                NamedPipeServerStream pipe = null;
                bool slotHeld = false;
                try
                {
                    await slots.WaitAsync(stopping.Token);
                    slotHeld = true;
                    var security = new PipeSecurity();
                    security.SetAccessRuleProtection(true, false);
                    security.AddAccessRule(new PipeAccessRule(new SecurityIdentifier(WellKnownSidType.NetworkSid, null), PipeAccessRights.FullControl, AccessControlType.Deny));
                    security.AddAccessRule(new PipeAccessRule(new SecurityIdentifier(currentSid), PipeAccessRights.FullControl, AccessControlType.Allow));
                    pipe = new NamedPipeServerStream(name, PipeDirection.InOut, MaxClients, PipeTransmissionMode.Byte,
                        PipeOptions.Asynchronous, 4096, 4096, security);
                    lock (pipeLock)
                    {
                        if (stopped) throw new OperationCanceledException();
                        pipes.Add(pipe);
                        accepting = pipe;
                    }
                    await pipe.WaitForConnectionAsync();
                    lock (pipeLock) if (accepting == pipe) accepting = null;
                    var connected = pipe;
                    pipe = null;
                    StartClient(connected);
                }
                catch (OperationCanceledException)
                {
                    if (pipe != null) Release(pipe, slotHeld); else if (slotHeld) ReleaseSlot();
                    break;
                }
                catch (ObjectDisposedException)
                {
                    if (pipe != null) Release(pipe, slotHeld); else if (slotHeld) ReleaseSlot();
                    if (stopped) break;
                }
                catch (Exception ex)
                {
                    if (pipe != null) Release(pipe, slotHeld); else if (slotHeld) ReleaseSlot();
                    if (stopped) break;
                    if (Interlocked.Increment(ref setupErrors) <= 3) Console.Error.WriteLine("Update control listener: " + ex);
                }
                if (!stopped) await Task.Delay(100);
            }
        }

        void StartClient(NamedPipeServerStream pipe) { Task.Run((Func<Task>)(() => ServeClient(pipe))); }

        async Task ServeClient(NamedPipeServerStream pipe)
        {
            string sessionId = null;
            try
            {
                var reader = new BoundedLineReader(pipe);
                bool verifiedClient = false;
                while (!stopped && pipe.IsConnected)
                {
                    byte[] frame = await Within(reader.ReadLineAsync(RequestLimit), ClientIdleLimitMs,
                        "Update client did not send a complete request.");
                    if (frame == null) return;
                    if (!verifiedClient) { VerifyClient(pipe); verifiedClient = true; }
                    string command = StrictUtf8.GetString(frame);
                    string response;
                    var completion = new TaskCompletionSource<string>();
                    int invocationState = 0; // 0 = queued, 1 = started, 2 = expired before start.
                    bool hasDispatchPermit = await dispatchSlots.WaitAsync(0);
                    if (!hasDispatchPermit)
                    {
                        response = ErrorResponse(new IOException("Update helper is busy; retry shortly."), false);
                        await Within(WriteFrame(pipe, response, ResponseLimit), RequestTimeoutMs,
                            "Update client stopped receiving responses.");
                        continue;
                    }
                    bool callbackOwnsPermit = false;
                    try
                    {
                        window.BeginInvoke((Action)(() =>
                        {
                            try
                            {
                                if (Interlocked.CompareExchange(ref invocationState, 1, 0) != 0) return;
                                try
                                {
                                    var request = UpdateCore.Json<UpdateCommand>(command);
                                    var result = window.ControlRequest(command, pipe);
                                    if (request != null && request.command == "attach") sessionId = request.sessionId;
                                    else if (request != null && request.command == "detach") sessionId = null;
                                    completion.TrySetResult(result);
                                }
                                catch (Exception ex) { completion.TrySetResult(ErrorResponse(ex, false)); }
                            }
                            finally { dispatchSlots.Release(); }
                        }));
                        callbackOwnsPermit = true;
                        response = await Within(completion.Task, RequestTimeoutMs, "Update helper did not respond.");
                    }
                    catch (Exception ex)
                    {
                        bool expiredBeforeStart = Interlocked.CompareExchange(ref invocationState, 2, 0) == 0;
                        response = ErrorResponse(ex, ex is IOException || ex is TimeoutException || !expiredBeforeStart);
                    }
                    finally { if (!callbackOwnsPermit) dispatchSlots.Release(); }

                    InvalidDataException oversized = null;
                    try { await Within(WriteFrame(pipe, response, ResponseLimit), RequestTimeoutMs, "Update client stopped receiving responses."); }
                    catch (InvalidDataException ex) { oversized = ex; }
                    if (oversized != null)
                        await Within(WriteFrame(pipe, ErrorResponse(oversized, false), ResponseLimit), RequestTimeoutMs,
                            "Update client stopped receiving responses.");
                }
            }
            catch { }
            finally
            {
                if (sessionId != null)
                {
                    string detached = sessionId;
                    try
                    {
                        if (!window.IsDisposed && window.IsHandleCreated)
                            window.BeginInvoke((Action)(() => window.ControlSessionDisconnected(detached, pipe)));
                    }
                    catch { }
                }
                Release(pipe, true);
            }
        }

        void VerifyClient(NamedPipeServerStream pipe)
        {
            string peerSid = null;
            pipe.RunAsClient(() =>
            {
                using (var peer = WindowsIdentity.GetCurrent())
                    if (peer.User != null) peerSid = peer.User.Value;
            });
            if (!string.Equals(peerSid, currentSid, StringComparison.OrdinalIgnoreCase))
                throw new UnauthorizedAccessException("Update command client is not the current user.");
        }

        void Release(NamedPipeServerStream pipe, bool releaseSlot)
        {
            lock (pipeLock)
            {
                pipes.Remove(pipe);
                if (accepting == pipe) accepting = null;
            }
            try { pipe.Dispose(); } catch { }
            if (releaseSlot) ReleaseSlot();
        }

        void ReleaseSlot() { try { slots.Release(); } catch (SemaphoreFullException) { } }

        public void Dispose()
        {
            stopped = true;
            try { stopping.Cancel(); } catch { }
            NamedPipeServerStream[] current;
            lock (pipeLock) current = pipes.ToArray();
            foreach (var pipe in current) try { pipe.Dispose(); } catch { }
        }

        sealed class ControlConnection : IDisposable
        {
            readonly string pipeName;
            NamedPipeClientStream pipe;
            BoundedLineReader reader;
            public bool IsConnected { get { return pipe != null && pipe.IsConnected; } }

            public ControlConnection(string id) { pipeName = "VideoProcessor.Update.Control." + id; }

            public string Request(string command, int connectTimeoutMs, int responseTimeoutMs)
            {
                byte[] frame = EncodeFrame(command, RequestLimit);
                try { EnsureConnected(connectTimeoutMs); }
                catch (Exception ex) { throw new ControlConnectException(ex); }
                try
                {
                    var timer = Stopwatch.StartNew();
                    Within(WriteBytes(pipe, frame), responseTimeoutMs, "Update helper stopped accepting requests.").GetAwaiter().GetResult();
                    int remaining = Math.Max(1, responseTimeoutMs - (int)timer.ElapsedMilliseconds);
                    var read = reader.ReadLineAsync(ResponseLimit);
                    byte[] response = Within(read, remaining, "Update helper did not respond.").GetAwaiter().GetResult();
                    if (response == null) throw new IOException("Update connection closed.");
                    return StrictUtf8.GetString(response);
                }
                catch
                {
                    Reset();
                    throw;
                }
            }

            void EnsureConnected(int timeoutMs)
            {
                if (pipe != null && pipe.IsConnected) return;
                Reset();
                var candidate = new NamedPipeClientStream(".", pipeName, PipeDirection.InOut, PipeOptions.Asynchronous, TokenImpersonationLevel.Impersonation);
                try
                {
                    candidate.Connect(timeoutMs);
                    pipe = candidate;
                    reader = new BoundedLineReader(pipe);
                }
                catch { candidate.Dispose(); throw; }
            }

            public void Reset()
            {
                var current = pipe;
                pipe = null; reader = null;
                if (current != null) try { current.Dispose(); } catch { }
            }

            public void Dispose() { Reset(); }
        }

        sealed class ControlConnectException : IOException
        {
            public ControlConnectException(Exception inner) : base("Update worker is not ready to accept control requests.", inner) { }
        }

        static async Task WriteBytes(Stream stream, byte[] bytes)
        {
            await stream.WriteAsync(bytes, 0, bytes.Length).ConfigureAwait(false);
            await stream.FlushAsync().ConfigureAwait(false);
        }

        sealed class BoundedLineReader
        {
            readonly Stream stream;
            readonly byte[] buffer = new byte[4096];
            int offset, count;

            public BoundedLineReader(Stream input) { stream = input; }

            public async Task<byte[]> ReadLineAsync(int limit)
            {
                using (var result = new MemoryStream())
                {
                    while (true)
                    {
                        if (offset < count)
                        {
                            int newline = Array.IndexOf(buffer, (byte)10, offset, count - offset);
                            int length = (newline < 0 ? count : newline) - offset;
                            if (result.Length + length > limit) throw new InvalidDataException("Update message is too large.");
                            result.Write(buffer, offset, length);
                            offset += length;
                            if (newline >= 0)
                            {
                                offset++;
                                return result.ToArray();
                            }
                        }

                        offset = 0; count = 0;
                        count = await stream.ReadAsync(buffer, 0, buffer.Length).ConfigureAwait(false);
                        if (count == 0)
                        {
                            if (result.Length == 0) return null;
                            throw new IOException("Update connection closed before the request ended.");
                        }
                    }
                }
            }
        }
    }

    public sealed class UpdateCommand
    {
        public string command, mode, channel, sessionId;
        public bool allowPlaybackRestart;
    }
}
