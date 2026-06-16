using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Controls;
using JoyProxy.Services;

namespace JoyProxy;

public partial class ProcessPickerWindow : Window
{
    private readonly ObservableCollection<ProcessPickerService.ProcessItem> _all = new();
    private readonly ObservableCollection<ProcessPickerService.ProcessItem> _filtered = new();

    public string? SelectedExePath { get; private set; }

    public ProcessPickerWindow()
    {
        InitializeComponent();
        ProcessList.ItemsSource = _filtered;
        RefreshList();
    }

    private void RefreshList()
    {
        _all.Clear();
        foreach (var item in ProcessPickerService.ListProcesses())
        {
            _all.Add(item);
        }
        ApplyFilter();
    }

    private void ApplyFilter()
    {
        var q = FilterBox?.Text?.Trim() ?? "";
        _filtered.Clear();
        foreach (var item in _all)
        {
            if (string.IsNullOrEmpty(q)
                || item.Name.Contains(q, StringComparison.OrdinalIgnoreCase)
                || item.ExePath.Contains(q, StringComparison.OrdinalIgnoreCase))
            {
                _filtered.Add(item);
            }
        }
    }

    private void Refresh_Click(object sender, RoutedEventArgs e) => RefreshList();

    private void FilterBox_TextChanged(object sender, TextChangedEventArgs e) => ApplyFilter();

    private void ProcessList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (ProcessList.SelectedItem is ProcessPickerService.ProcessItem item)
        {
            SelectedExePath = item.ExePath;
        }
    }

    private void Ok_Click(object sender, RoutedEventArgs e)
    {
        if (string.IsNullOrWhiteSpace(SelectedExePath))
        {
            MessageBox.Show(this, "请先选择一个进程。", "JoyProxy", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        DialogResult = true;
        Close();
    }

    private void Cancel_Click(object sender, RoutedEventArgs e)
    {
        DialogResult = false;
        Close();
    }
}
