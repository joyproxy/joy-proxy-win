using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.Json;
using JoyProxy.Models;

namespace JoyProxy.Services;

public static class SettingsStore
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    private static string SettingsPath =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "JoyProxy", "settings.json");

    public static AppSettings Load()
    {
        try
        {
            if (File.Exists(SettingsPath))
            {
                var json = File.ReadAllText(SettingsPath);
                return JsonSerializer.Deserialize<AppSettings>(json) ?? new AppSettings();
            }
        }
        catch
        {
            // ignore corrupt settings
        }
        return new AppSettings();
    }

    public static void Save(AppSettings settings)
    {
        var dir = Path.GetDirectoryName(SettingsPath)!;
        Directory.CreateDirectory(dir);
        File.WriteAllText(SettingsPath, JsonSerializer.Serialize(settings, JsonOptions));
    }
}

public static class ServiceLauncher
{
    public static bool TryLaunchElevated(int parentPid, out string? error)
    {
        error = null;
        var exe = Path.Combine(AppContext.BaseDirectory, "JoyProxyService.exe");
        if (!File.Exists(exe))
        {
            error = "JoyProxyService.exe not found beside JoyProxy.exe";
            return false;
        }
        var psi = new ProcessStartInfo
        {
            FileName = exe,
            Arguments = $"--parent-pid {parentPid}",
            UseShellExecute = true,
            Verb = "runas",
            WorkingDirectory = AppContext.BaseDirectory
        };
        try
        {
            Process.Start(psi);
            return true;
        }
        catch (Exception ex)
        {
            error = ex.Message;
            return false;
        }
    }
}

public static class RelayNative
{
    [DllImport("joyproxy_relay.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int joyproxy_relay_test_proxy(
        byte proxyType,
        IntPtr proxyHost,
        ushort proxyPort,
        IntPtr proxyUser,
        IntPtr proxyPass);

    public static int TestProxy(ProxySettings proxy)
    {
        var dll = Path.Combine(AppContext.BaseDirectory, "joyproxy_relay.dll");
        if (!File.Exists(dll))
        {
            return -100;
        }
        return Utf8Call(proxy, joyproxy_relay_test_proxy);
    }

    private static int Utf8Call(ProxySettings proxy, Func<byte, IntPtr, ushort, IntPtr, IntPtr, int> fn)
    {
        var host = Marshal.StringToCoTaskMemUTF8(proxy.Host);
        var user = Marshal.StringToCoTaskMemUTF8(proxy.Username ?? "");
        var pass = Marshal.StringToCoTaskMemUTF8(proxy.Password ?? "");
        try
        {
            var type = (byte)(proxy.Type == ProxyTypeKind.Http ? 1 : 0);
            return fn(type, host, (ushort)proxy.Port, user, pass);
        }
        finally
        {
            Marshal.FreeCoTaskMem(host);
            Marshal.FreeCoTaskMem(user);
            Marshal.FreeCoTaskMem(pass);
        }
    }
}

public static class ProcessPickerService
{
    public sealed record ProcessItem(int Pid, string Name, string ExePath)
    {
        public override string ToString() => $"{Name} ({Pid}) — {ExePath}";
    }

    public static IEnumerable<ProcessItem> ListProcesses()
    {
        foreach (var p in Process.GetProcesses().OrderBy(x => x.ProcessName))
        {
            string path;
            try
            {
                path = p.MainModule?.FileName ?? "";
            }
            catch
            {
                continue;
            }
            if (string.IsNullOrWhiteSpace(path))
            {
                continue;
            }
            if (path.Contains("JoyProxy", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }
            yield return new ProcessItem(p.Id, p.ProcessName, path);
        }
    }
}
