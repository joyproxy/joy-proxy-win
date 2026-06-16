using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using JoyProxy.Models;

namespace JoyProxy.Services;

public sealed class ServiceClient(string pipeName = @"\\.\pipe\joyproxy-v1")
{
    private int _nextId;

    public async Task<IpcResponse?> SendAsync(IpcRequest request, CancellationToken cancellationToken = default)
    {
        using var pipe = new NamedPipeClientStream(".", pipeName.Replace(@"\\.\pipe\", ""), PipeDirection.InOut, PipeOptions.Asynchronous);
        await pipe.ConnectAsync(3000, cancellationToken);
        request.Id = Interlocked.Increment(ref _nextId);
        var json = JsonSerializer.Serialize(request);
        var bytes = Encoding.UTF8.GetBytes(json + "\n");
        await pipe.WriteAsync(bytes, cancellationToken);
        await pipe.FlushAsync(cancellationToken);

        using var ms = new MemoryStream();
        var buffer = new byte[4096];
        while (true)
        {
            var read = await pipe.ReadAsync(buffer, cancellationToken);
            if (read <= 0)
            {
                break;
            }
            ms.Write(buffer, 0, read);
            var text = Encoding.UTF8.GetString(ms.GetBuffer(), 0, (int)ms.Length);
            var idx = text.IndexOf('\n');
            if (idx >= 0)
            {
                var line = text[..idx].Trim();
                return JsonSerializer.Deserialize<IpcResponse>(line);
            }
        }
        return null;
    }

    public Task<IpcResponse?> PingAsync(CancellationToken ct = default) =>
        SendAsync(new IpcRequest { Type = "ping" }, ct);

    public Task<IpcResponse?> StartAsync(ProxySettings proxy, string exePath, CancellationToken ct = default) =>
        SendAsync(new IpcRequest
        {
            Type = "start",
            ProxyType = proxy.Type == ProxyTypeKind.Http ? "http" : "socks5",
            Host = proxy.Host,
            Port = proxy.Port,
            Username = proxy.Username,
            Password = proxy.Password,
            ExePath = exePath
        }, ct);

    public Task<IpcResponse?> StopAsync(CancellationToken ct = default) =>
        SendAsync(new IpcRequest { Type = "stop" }, ct);

    public Task<IpcResponse?> StatusAsync(CancellationToken ct = default) =>
        SendAsync(new IpcRequest { Type = "status" }, ct);
}
