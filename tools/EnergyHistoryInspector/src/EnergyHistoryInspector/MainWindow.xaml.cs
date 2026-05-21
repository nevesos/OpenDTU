using EnergyHistoryInspector.EnergyHistory;
using Microsoft.Win32;
using System.Windows;

namespace EnergyHistoryInspector;

public partial class MainWindow : Window
{
    private readonly EnergyHistoryFileParser _parser = new();
    private EnergyHistoryFile? _currentFile;

    public MainWindow()
    {
        InitializeComponent();
    }

    private void OpenButton_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            Title = "Energy-History-Datei oeffnen",
            Filter = "Energy History (*.eh5;*.ehd;*.ehm)|*.eh5;*.ehd;*.ehm|Alle Dateien (*.*)|*.*",
            CheckFileExists = true,
            Multiselect = false,
        };

        if (dialog.ShowDialog(this) == true)
        {
            LoadFile(dialog.FileName);
        }
    }

    private void RepairButton_Click(object sender, RoutedEventArgs e)
    {
        if (_currentFile is null || !_currentFile.Scan.CanTruncateFinalBlock)
        {
            return;
        }

        var message = $"Der defekte letzte Block wird abgeschnitten.\n\nDatei: {_currentFile.Scan.FilePath}\nBackup wird vorher erstellt.\n\nFortfahren?";
        if (MessageBox.Show(this, message, "Energy History reparieren", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes)
        {
            return;
        }

        try
        {
            var backupPath = EnergyHistoryFileRepair.TruncateFinalInvalidBlock(_currentFile.Scan.FilePath, _currentFile.Scan.LastValidOffset);
            StatusText.Text = $"Reparatur abgeschlossen. Backup: {backupPath}";
            LoadFile(_currentFile.Scan.FilePath);
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Reparatur fehlgeschlagen", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void BlocksGrid_SelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
    {
        if (BlocksGrid.SelectedItem is EnergyHistoryBlock block)
        {
            RecordsGrid.ItemsSource = block.Records;
        }
    }

    private void LoadFile(string path)
    {
        try
        {
            _currentFile = _parser.Parse(path);
            FilePathText.Text = path;
            HeaderTypeText.Text = _currentFile.Header.FileType.ToString();
            HeaderTargetText.Text = _currentFile.Header.TargetType.ToString();
            HeaderSerialText.Text = _currentFile.Header.Serial.ToString();
            HeaderPeriodText.Text = _currentFile.Header.Month == 0
                ? _currentFile.Header.Year.ToString()
                : $"{_currentFile.Header.Year}-{_currentFile.Header.Month:00}";
            HeaderRecordSizeText.Text = $"{_currentFile.Header.RecordSize} B";
            HeaderCrcText.Text = $"0x{_currentFile.Header.HeaderCrc:x8}";

            ScanFileSizeText.Text = $"{_currentFile.Scan.FileSize:n0} B";
            ScanBlocksText.Text = $"{_currentFile.Scan.ValidBlocks:n0} / {_currentFile.Scan.SkippedBlocks:n0}";
            ScanRecordsText.Text = $"{_currentFile.Scan.ValidRecords:n0} / {_currentFile.Scan.SkippedRecords:n0}";
            ScanOffsetText.Text = $"{_currentFile.Scan.LastValidOffset:n0}";
            ScanRepairText.Text = _currentFile.Scan.CanTruncateFinalBlock ? "Ja" : "Nein";

            BlocksGrid.ItemsSource = _currentFile.Blocks;
            RecordsGrid.ItemsSource = _currentFile.Records;
            RepairButton.IsEnabled = _currentFile.Scan.CanTruncateFinalBlock;
            StatusText.Text = "Datei geladen.";
        }
        catch (Exception ex)
        {
            _currentFile = null;
            BlocksGrid.ItemsSource = null;
            RecordsGrid.ItemsSource = null;
            RepairButton.IsEnabled = false;
            StatusText.Text = "Fehler beim Laden.";
            MessageBox.Show(this, ex.Message, "Datei konnte nicht gelesen werden", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }
}
