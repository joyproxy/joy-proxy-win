using System.Collections.ObjectModel;
using System.Windows;
using Microsoft.Win32;
using JoyProxy.Models;
using JoyProxy.Services;

namespace JoyProxy;

public partial class MainWindow : Window
{
    private readonly ServiceClient _client = new();
    private readonly ObservableCollection<string> _historyItems = new();
    private readonly ObservableCollection<ProcessPickerService.ProcessItem> _processes = new();
    private AppSettings _settings;
    private bool _connected;

    public MainWindow()
    {
        InitializeComponent();
        _settings = SettingsStore.Load();
        ProxyTypeCombo.SelectedIndex = _settings.LastProxy.Type == ProxyTypeKind.Http ? 1 : 0;
        HostBox.Text = _settings.LastProxy.Host;
        PortBox.Text = _settings.LastProxy.Port.ToString();
        UserBox.Text = _settings.LastProxy.Username;
        PassBox.Password = _settings.LastProxy.Password;
        TargetExeBox.Text = _settings.LastTargetExe;
        HistoryCombo.ItemsSource = _historyItems;
        ProcessList.ItemsSource = _processes;
        ReloadHistory();
        RefreshProcesses();
    }

    private void ReloadHistory()
    {
        _historyItems.Clear();
        foreach (var item in _settings.ProxyHistory.Take(20))
        {
            _historyItems.Add($"{item.Type} {item.Host}:{item.Port}");
        }
    }

    private ProxySettings ReadProxy()
    {
        _ = int.TryParse(PortBox.Text, out var port);
        return new ProxySettings
        {
            Type = ProxyTypeCombo.SelectedIndex == 1 ? ProxyTypeKind.Http : ProxyTypeKind.Socks5,
            Host = HostBox.Text.Trim(),
            Port = port > 0 ? port : 1080,
            Username = UserBox.Text.Trim(),
            Password = PassBox.Password
        };
    }

    private void SaveSettings()
    {
        _settings.LastProxy = ReadProxy();
        _settings.LastTargetExe = TargetExeBox.Text.Trim();
        var key = $"{_settings.LastProxy.Type} {_settings.LastProxy.Host}:{_settings.LastProxy.Port}";
        _settings.ProxyHistory.RemoveAll(h =>
            h.Type == _settings.LastProxy.Type && h.Host == _settings.LastProxy.Host && h.Port == _settings.LastProxy.Port);
        _settings.ProxyHistory.Insert(0, _settings.LastProxy with { });
        if (_settings.ProxyHistory.Count > 20)
        {
            _settings.ProxyHistory = _settings.ProxyHistory.Take(20).ToList();
        }
        SettingsStore.Save(_settings);
        ReloadHistory();
    }

    private void SetStatus(string text)
    {
        StatusText.Text = text;
    }

    private async void TestProxy_Click(object sender, RoutedEventArgs e)
    {
        if (_connected)
        {
            MessageBox.Show(this, "请先断开连接再测试代理。", "JoyProxy", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        var proxy = ReadProxy();
        SetStatus("正在测试代理...");
        await Task.Run(() =>
        {
            var code = RelayNative.TestProxy(proxy);
            Dispatcher.Invoke(() =>
            {
                SetStatus(code == 0 ? "代理测试成功" : $"代理测试失败 ({code})");
            });
        });
    }

    private void BrowseExe_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog
        {
            Filter = "Executable (*.exe)|*.exe",
            Title = "选择目标程序"
        };
        if (dlg.ShowDialog(this) == true)
        {
            TargetExeBox.Text = dlg.FileName;
        }
    }

    private void RefreshProcesses_Click(object sender, RoutedEventArgs e) => RefreshProcesses();

    private void RefreshProcesses()
    {
        _processes.Clear();
        foreach (var item in ProcessPickerService.ListProcesses())
        {
            _processes.Add(item);
        }
    }

    private void ProcessList_SelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
    {
        if (ProcessList.SelectedItem is ProcessPickerService.ProcessItem item)
        {
            TargetExeBox.Text = item.ExePath;
        }
    }

    private async void Connect_Click(object sender, RoutedEventArgs e)
    {
        if (_connected)
        {
            SetStatus("正在断开...");
            await _client.StopAsync();
            _connected = false;
            ConnectButton.Content = "连接代理";
            SetStatus("已断开");
            return;
        }

        var proxy = ReadProxy();
        var exe = TargetExeBox.Text.Trim();
        if (string.IsNullOrWhiteSpace(proxy.Host) || string.IsNullOrWhiteSpace(exe) || !File.Exists(exe))
        {
            MessageBox.Show(this, "请填写有效代理并选择存在的 exe 文件。", "JoyProxy", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        SaveSettings();
        SetStatus("正在请求管理员权限并启动服务...");
        if (!ServiceLauncher.TryLaunchElevated(Environment.ProcessId, out var launchError))
        {
            SetStatus(launchError ?? "启动服务失败");
            return;
        }

        await Task.Delay(1200);
        var pong = await _client.PingAsync();
        if (pong?.Type != "pong")
        {
            SetStatus("无法连接 JoyProxyService（请确认 UAC 已允许）");
            return;
        }

        SetStatus("正在注入 Hook...");
        var resp = await _client.StartAsync(proxy, exe);
        if (resp?.Type == "started")
        {
            _connected = true;
            ConnectButton.Content = "断开代理";
            SetStatus($"已连接 · 目标: {Path.GetFileName(exe)} · 已有进程将走代理，新启动的同路径进程也会自动注入");
            return;
        }
        SetStatus(resp?.Message ?? resp?.Code ?? "连接失败");
    }

    private void HistoryCombo_SelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
    {
        var idx = HistoryCombo.SelectedIndex;
        if (idx < 0 || idx >= _settings.ProxyHistory.Count)
        {
            return;
        }
        var h = _settings.ProxyHistory[idx];
        ProxyTypeCombo.SelectedIndex = h.Type == ProxyTypeKind.Http ? 1 : 0;
        HostBox.Text = h.Host;
        PortBox.Text = h.Port.ToString();
        UserBox.Text = h.Username;
        PassBox.Password = h.Password;
    }
}
