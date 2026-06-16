using System.Text.Json;
using System.Text.Json.Serialization;

namespace JoyProxy.Models;

public enum ProxyTypeKind
{
    Socks5,
    Http
}

public sealed class ProxySettings
{
    public ProxyTypeKind Type { get; set; } = ProxyTypeKind.Socks5;
    public string Host { get; set; } = "";
    public int Port { get; set; } = 1080;
    public string Username { get; set; } = "";
    public string Password { get; set; } = "";
}

public sealed class AppSettings
{
    public ProxySettings LastProxy { get; set; } = new();
    public string LastTargetExe { get; set; } = "";
    public List<ProxySettings> ProxyHistory { get; set; } = new();
}

public sealed class IpcRequest
{
    [JsonPropertyName("type")]
    public string Type { get; set; } = "";

    [JsonPropertyName("id")]
    public int Id { get; set; }

    [JsonPropertyName("proxyType")]
    public string? ProxyType { get; set; }

    [JsonPropertyName("host")]
    public string? Host { get; set; }

    [JsonPropertyName("port")]
    public int? Port { get; set; }

    [JsonPropertyName("username")]
    public string? Username { get; set; }

    [JsonPropertyName("password")]
    public string? Password { get; set; }

    [JsonPropertyName("exePath")]
    public string? ExePath { get; set; }
}

public sealed class IpcResponse
{
    [JsonPropertyName("type")]
    public string? Type { get; set; }

    [JsonPropertyName("id")]
    public int Id { get; set; }

    [JsonPropertyName("code")]
    public string? Code { get; set; }

    [JsonPropertyName("message")]
    public string? Message { get; set; }

    [JsonPropertyName("payload")]
    public JsonElement? Payload { get; set; }
}
