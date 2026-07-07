using System;
using System.Collections.Generic;
using System.Data;
using System.Diagnostics;
using System.Linq;
using System.Net;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace mqttv5_tool
{
    public partial class Form1 : Form
    {
        private const int SaveRestartReconnectTimeoutMs = Management.SaveRestartReconnectTimeoutMs;
        private readonly DataTable table = new DataTable();
        private readonly List<Management> managements = new List<Management>();
        private readonly HostToolSettings hostToolSettings;
        private MqttClientForm mqttClientForm;
        private CancellationTokenSource scanCancellation;

        public Form1()
        {
            hostToolSettings = HostToolSettings.Load();
            InitializeComponent();
            InitializeLanguageSelector();
            InitializeDeviceTable();
            ApplyLanguage();
            buttonMqttClient.Visible = hostToolSettings.MqttClientEnabled;
            AppLogger.Info("Host tool started. LogFilePath=" + AppLogger.LogFilePath);
        }

        private UiLanguage CurrentLanguage
        {
            get { return UiText.Parse(hostToolSettings.Language); }
        }

        private string T(string chinese, string english)
        {
            return UiText.T(CurrentLanguage, chinese, english);
        }

        private void InitializeLanguageSelector()
        {
            comboBoxLanguage.SelectedIndexChanged -= comboBoxLanguage_SelectedIndexChanged;
            comboBoxLanguage.Items.Clear();
            comboBoxLanguage.Items.Add(UiText.LanguageDisplay(UiLanguage.Chinese));
            comboBoxLanguage.Items.Add(UiText.LanguageDisplay(UiLanguage.English));
            comboBoxLanguage.SelectedIndex = CurrentLanguage == UiLanguage.English ? 1 : 0;
            comboBoxLanguage.SelectedIndexChanged += comboBoxLanguage_SelectedIndexChanged;
        }

        private void comboBoxLanguage_SelectedIndexChanged(object sender, EventArgs e)
        {
            hostToolSettings.Language = comboBoxLanguage.SelectedIndex == 1 ? "en-US" : "zh-CN";
            hostToolSettings.Save();
            ApplyLanguage();
            if (mqttClientForm != null && !mqttClientForm.IsDisposed)
                mqttClientForm.ApplyLanguage(CurrentLanguage);
        }

        private void ApplyLanguage()
        {
            UiLanguage language = CurrentLanguage;
            UiText.Apply(this, language);
            comboBoxLanguage.SelectedIndexChanged -= comboBoxLanguage_SelectedIndexChanged;
            comboBoxLanguage.Items.Clear();
            comboBoxLanguage.Items.Add(UiText.LanguageDisplay(UiLanguage.Chinese));
            comboBoxLanguage.Items.Add(UiText.LanguageDisplay(UiLanguage.English));
            comboBoxLanguage.SelectedIndex = language == UiLanguage.English ? 1 : 0;
            comboBoxLanguage.SelectedIndexChanged += comboBoxLanguage_SelectedIndexChanged;
            ApplyDeviceGridLanguage();
            RefreshDeviceRows();
            ResetScanProgress();
        }

        private void ApplyDeviceGridLanguage()
        {
            string[] headers = new string[]
            {
                T("序号", "No."),
                T("设备型号", "Model"),
                T("设备ID", "Device ID"),
                T("设备状态", "Status"),
                T("IP地址", "IP Address"),
                T("IP分配", "IP Mode"),
                T("MAC地址", "MAC Address")
            };
            for (int i = 0; i < headers.Length && i < dataGridViewDevices.Columns.Count; i++)
                dataGridViewDevices.Columns[i].HeaderText = headers[i];
        }

        private void InitializeDeviceTable()
        {
            table.Columns.Add("序号", typeof(int));
            table.Columns.Add("设备型号", typeof(string));
            table.Columns.Add("设备ID", typeof(string));
            table.Columns.Add("设备状态", typeof(string));
            table.Columns.Add("IP地址", typeof(string));
            table.Columns.Add("IP分配", typeof(string));
            table.Columns.Add("MAC地址", typeof(string));

            dataGridViewDevices.DataSource = table;
            Form_func.init(dataGridViewDevices);
            dataGridViewDevices.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill;
            dataGridViewDevices.CellDoubleClick += dataGridViewDevices_CellDoubleClick;
        }

        private async void buttonStartSearch_Click(object sender, EventArgs e)
        {
            await SearchDevicesAsync();
        }

        private async void buttonFindNav_Click(object sender, EventArgs e)
        {
            await SearchDevicesAsync();
        }

        private void buttonStopSearch_Click(object sender, EventArgs e)
        {
            if (scanCancellation == null)
                return;
            labelScanCurrent.Text = T("正在停止查找...", "Stopping scan...");
            scanCancellation.Cancel();
        }

        private void buttonOpenLogs_Click(object sender, EventArgs e)
        {
            OpenLogDirectory();
        }

        private void buttonMqttClient_Click(object sender, EventArgs e)
        {
            OpenMqttClient();
        }

        private async void buttonRestart_Click(object sender, EventArgs e)
        {
            Management selected = GetSelectedDevice();
            if (selected == null)
                return;

            SetBusy(true);
            try
            {
                AppLogger.Info("Restart begin ip=" + selected.EthIp + " deviceId=" + selected.DeviceId);
                await Task.Run(() => RestartDevice(selected));
                AppLogger.Info("Restart sent ip=" + selected.EthIp + " deviceId=" + selected.DeviceId);
            }
            catch (Exception ex)
            {
                selected.Status = T("设置失败", "Settings failed");
                AppLogger.Error("Restart failed ip=" + selected.EthIp + " deviceId=" + selected.DeviceId, ex);
                MessageBox.Show(T("重启设备失败：", "Restart device failed: ") + ex.Message, T("重启设备", "Restart Device"), MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                SetBusy(false);
                RefreshDeviceRows();
            }
        }

        private async void buttonSettings_Click(object sender, EventArgs e)
        {
            await OpenSelectedDeviceSettingsAsync();
        }

        private async void dataGridViewDevices_CellDoubleClick(object sender, DataGridViewCellEventArgs e)
        {
            if (e.RowIndex >= 0)
                await OpenSelectedDeviceSettingsAsync();
        }

        private async void buttonAdvancedNav_Click(object sender, EventArgs e)
        {
            using (AdvancedSettingsForm form = new AdvancedSettingsForm(hostToolSettings.Language))
            {
                form.StartPosition = FormStartPosition.CenterParent;
                if (form.ShowDialog(this) == DialogResult.OK)
                {
                    await ApplyBatchSettingsAsync(form);
                }
            }
        }

        private void OpenMqttClient()
        {
            if (mqttClientForm != null && !mqttClientForm.IsDisposed)
            {
                mqttClientForm.Activate();
                return;
            }

            Management selected = GetSelectedDeviceOrDefault();
            mqttClientForm = new MqttClientForm(selected, hostToolSettings);
            mqttClientForm.StartPosition = FormStartPosition.CenterParent;
            mqttClientForm.Show(this);
        }

        private async Task SearchDevicesAsync()
        {
            if (scanCancellation != null)
                return;

            scanCancellation = new CancellationTokenSource();
            SetScanning(true);
            managements.Clear();
            RefreshDeviceRows();
            labelFound.Text = T("正在查找设备...", "Scanning devices...");
            ResetScanProgress();
            try
            {
                AppLogger.Info("UI search begin");
                var scanResults = new List<Management>();
                await Management.Scan(scanResults, OnDeviceFoundDuringScan, OnScanProgress, scanCancellation.Token);
                AppLogger.Info("UI search end found=" + scanResults.Count + " log=" + AppLogger.LogFilePath);
                ReplaceDevicesFromScanResults(scanResults);
            }
            catch (OperationCanceledException)
            {
                AppLogger.Info("UI search cancelled");
                labelFound.Text = T("查找已停止", "Scan stopped");
            }
            catch (Exception ex)
            {
                AppLogger.Error("UI search failed", ex);
                MessageBox.Show(T("开始查找失败：", "Start scan failed: ") + ex.Message + Environment.NewLine + T("日志文件：", "Log file: ") + AppLogger.LogFilePath, T("查找", "Find"), MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                scanCancellation.Dispose();
                scanCancellation = null;
                SetScanning(false);
            }
        }

        private void ResetScanProgress()
        {
            progressScan.Value = 0;
            labelScanCurrent.Text = T("扫描状态：准备扫描", "Scan status: preparing");
            labelScanProgress.Text = T("进度：0%，五线程并发，预计剩余 计算中", "Progress: 0%, Five workers, remaining calculating");
        }

        private void OnScanProgress(ScanProgressInfo progress)
        {
            if (progress == null || IsDisposed || !IsHandleCreated)
                return;

            if (InvokeRequired)
            {
                BeginInvoke(new Action(() => UpdateScanProgress(progress)));
                return;
            }

            UpdateScanProgress(progress);
        }

        private void UpdateScanProgress(ScanProgressInfo progress)
        {
            int percent = Math.Max(0, Math.Min(100, progress.Percent));
            progressScan.Value = percent;
            string currentIp = string.IsNullOrWhiteSpace(progress.CurrentIp) ? "--" : progress.CurrentIp;
            string remaining = progress.Completed == 0 && !progress.IsCompleted ? T("计算中", "calculating") : FormatDuration(progress.EstimatedRemaining);
            labelScanCurrent.Text = T("扫描状态：", "Scan status: ") + TranslateScanStage(progress.Stage) + T("，当前 IP ", ", current IP ") + currentIp + " (" + progress.Completed + "/" + progress.Total + ")";
            labelScanProgress.Text = T("进度：", "Progress: ") + percent + T("%，五线程并发，已用 ", "%, Five workers, elapsed ") + FormatDuration(progress.Elapsed) + T("，预计剩余 ", ", remaining ") + remaining + T("，已找到 ", ", found ") + progress.FoundCount + T(" 个", "");
        }

        private void OnDeviceFoundDuringScan(Management device)
        {
            if (device == null)
                return;

            AppLogger.Info("UI device found ip=" + device.EthIp + " deviceId=" + device.DeviceId);
            if (IsDisposed || !IsHandleCreated)
                return;

            if (InvokeRequired)
            {
                BeginInvoke(new Action(() => AddOrUpdateDevice(device)));
                return;
            }

            AddOrUpdateDevice(device);
        }

        private async Task OpenSelectedDeviceSettingsAsync()
        {
            Management selected = GetSelectedDevice();
            if (selected == null)
                return;

            SetBusy(true);
            try
            {
                AppLogger.Info("Read config begin ip=" + selected.EthIp + " deviceId=" + selected.DeviceId);
                await Task.Run(() => ReadSelectedDeviceConfig(selected));
                AppLogger.Info("Read config success ip=" + selected.EthIp + " deviceId=" + selected.DeviceId);
            }
            catch (Exception ex)
            {
                selected.Status = T("设置失败", "Settings failed");
                AppLogger.Error("Read config failed ip=" + selected.EthIp + " deviceId=" + selected.DeviceId, ex);
                MessageBox.Show(T("读取设备配置失败：", "Read device configuration failed: ") + ex.Message, T("设置", "Settings"), MessageBoxButtons.OK, MessageBoxIcon.Error);
                RefreshDeviceRows();
                return;
            }
            finally
            {
                SetBusy(false);
            }

            using (Form2 form = new Form2(hostToolSettings.Language))
            {
                form.StartPosition = FormStartPosition.CenterParent;
                form.LoadFromManagement(selected);
                if (form.ShowDialog(this) != DialogResult.OK)
                    return;

                form.ApplyToManagement(selected);
            }

            SetBusy(true);
            try
            {
                AppLogger.Info("Write config begin ip=" + selected.EthIp + " deviceId=" + selected.DeviceId);
                await Task.Run(() => WriteSelectedDeviceConfig(selected));
                selected.Status = T("设置成功，设备已重新连接", "Settings written; device online");
                AppLogger.Info("Write config success ip=" + selected.EthIp + " deviceId=" + selected.DeviceId);
            }
            catch (Exception ex)
            {
                selected.Status = T("设置失败", "Settings failed");
                AppLogger.Error("Write config failed ip=" + selected.EthIp + " deviceId=" + selected.DeviceId, ex);
                MessageBox.Show(T("写入设备失败：", "Write device failed: ") + ex.Message, T("设置", "Settings"), MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                SetBusy(false);
                RefreshDeviceRows();
            }
        }

        private void SetBusy(bool busy)
        {
            UseWaitCursor = busy;
            buttonRestart.Enabled = !busy;
            buttonSettings.Enabled = !busy;
            buttonAdvancedNav.Enabled = !busy;
            buttonStartSearch.Enabled = !busy && scanCancellation == null;
            buttonFindNav.Enabled = !busy && scanCancellation == null;
            buttonStopSearch.Enabled = !busy && scanCancellation != null;
        }

        private void SetScanning(bool scanning)
        {
            buttonStartSearch.Enabled = !scanning;
            buttonFindNav.Enabled = !scanning;
            buttonStopSearch.Enabled = scanning;
            buttonRestart.Enabled = !scanning;
            buttonSettings.Enabled = !scanning;
            buttonAdvancedNav.Enabled = !scanning;
        }

        private static void RestartDevice(Management selected)
        {
            try
            {
                selected.Connect();
                selected.Reset();
            }
            finally
            {
                selected.Disconnect();
            }
        }

        private static void ReadSelectedDeviceConfig(Management selected)
        {
            try
            {
                selected.Connect();
                selected.Upload();
            }
            finally
            {
                selected.Disconnect();
            }
        }

        private static void WriteSelectedDeviceConfig(Management selected)
        {
            IPAddress expectedIp = GetExpectedManagementIpAfterWrite(selected);
            try
            {
                selected.Connect();
                selected.Load();
                selected.SaveAndRestart();
            }
            finally
            {
                selected.Disconnect();
            }
            WaitForDeviceAfterSaveAndRestart(selected, expectedIp);
        }

        private async Task ApplyBatchSettingsAsync(AdvancedSettingsForm form)
        {
            if (managements.Count == 0)
            {
                MessageBox.Show(T("当前没有可设置的设备。", "There is no configurable device."), T("一键设置", "Apply All"), MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            DialogResult confirm = MessageBox.Show(T("确认按勾选参数一键设置所有设备？", "Apply selected settings to all devices?"), T("一键设置", "Apply All"), MessageBoxButtons.YesNo, MessageBoxIcon.Question);
            if (confirm != DialogResult.Yes)
                return;

            List<Management> devices = managements.ToList();
            SetBusy(true);
            try
            {
                for (int i = 0; i < devices.Count; i++)
                {
                    Management device = devices[i];
                    try
                    {
                        labelFound.Text = T("正在批量设置 ", "Batch setting ") + (i + 1) + "/" + devices.Count + ": " + device.EthIp;
                        AppLogger.Info("Batch config begin ip=" + device.EthIp + " deviceId=" + device.DeviceId);
                        await Task.Run(() => ConnectAndUploadDevice(device));
                        form.ApplyTo(device);
                        await Task.Run(() => SaveAndRestartConnectedDevice(device));
                        device.Status = T("设置成功，设备已重新连接", "Settings written; device online");
                        AppLogger.Info("Batch config success ip=" + device.EthIp + " deviceId=" + device.DeviceId);
                    }
                    catch (Exception ex)
                    {
                        device.Status = T("设置失败", "Settings failed");
                        AppLogger.Error("Batch config failed ip=" + device.EthIp + " deviceId=" + device.DeviceId, ex);
                        Console.WriteLine(ex.Message);
                    }
                    finally
                    {
                        await Task.Run(() => SafeDisconnectDevice(device));
                        RefreshDeviceRows();
                    }
                }
            }
            finally
            {
                SetBusy(false);
                RefreshDeviceRows();
            }
        }

        private static void ConnectAndUploadDevice(Management device)
        {
            device.Connect();
            device.Upload();
        }

        private static void SaveAndRestartConnectedDevice(Management device)
        {
            IPAddress expectedIp = GetExpectedManagementIpAfterWrite(device);
            device.Load();
            device.SaveAndRestart();
            device.Disconnect();
            WaitForDeviceAfterSaveAndRestart(device, expectedIp);
        }

        private static IPAddress GetExpectedManagementIpAfterWrite(Management device)
        {
            if (device != null && device.Mode == 0 && device.Ip != null)
                return device.Ip;
            return device == null ? IPAddress.None : device.EthIp;
        }

        private static void WaitForDeviceAfterSaveAndRestart(Management selected, IPAddress expectedIp)
        {
            Management reconnected = Management.WaitForDevice(expectedIp, SaveRestartReconnectTimeoutMs);
            selected.CopyConnectionStateFrom(reconnected);
        }

        private static void SafeDisconnectDevice(Management device)
        {
            try
            {
                device.Disconnect();
            }
            catch (Exception ex)
            {
                AppLogger.Error("Disconnect failed ip=" + device.EthIp + " deviceId=" + device.DeviceId, ex);
            }
        }

        private Management GetSelectedDevice()
        {
            if (dataGridViewDevices.CurrentRow == null || dataGridViewDevices.CurrentRow.Index < 0)
            {
                MessageBox.Show(T("请先选择一台设备。", "Select a device first."), T("设备列表", "Device List"), MessageBoxButtons.OK, MessageBoxIcon.Information);
                return null;
            }

            int rowIndex = dataGridViewDevices.CurrentRow.Index;
            if (rowIndex >= managements.Count)
                return null;
            return managements[rowIndex];
        }

        private Management GetSelectedDeviceOrDefault()
        {
            if (dataGridViewDevices.CurrentRow != null && dataGridViewDevices.CurrentRow.Index >= 0)
            {
                int rowIndex = dataGridViewDevices.CurrentRow.Index;
                if (rowIndex < managements.Count)
                    return managements[rowIndex];
            }
            if (managements.Count > 0)
                return managements[0];
            return null;
        }

        private void RefreshDeviceRows()
        {
            table.Clear();
            for (int i = 0; i < managements.Count; i++)
            {
                Management item = managements[i];
                table.Rows.Add(
                    i + 1,
                    item.DeviceModel,
                    item.DeviceId,
                    TranslateDeviceStatus(item.Status),
                    item.EthIp.ToString(),
                    TranslateIpAssignText(item),
                    FormatMac(item.Mac.GetAddressBytes()));
            }
            if (managements.Count == 0 && !string.IsNullOrEmpty(Management.LastScanDiagnostics))
                labelFound.Text = T("找到0个设备；", "Found 0 devices; ") + TranslateScanDiagnostics(Management.LastScanDiagnostics) + T("；日志已写入 logs", "; logs written to logs");
            else
                labelFound.Text = T("找到", "Found ") + managements.Count + T("个设备；日志已写入 logs", " devices; logs written to logs");
            ApplyDeviceGridLanguage();
        }

        private string TranslateDeviceStatus(string status)
        {
            return UiText.Translate(CurrentLanguage, status);
        }

        private string TranslateIpAssignText(Management item)
        {
            if (item == null)
                return string.Empty;
            return UiText.Translate(CurrentLanguage, item.IpAssignText);
        }

        private string TranslateScanStage(string stage)
        {
            return UiText.Translate(CurrentLanguage, stage);
        }

        private string TranslateScanDiagnostics(string diagnostics)
        {
            if (string.IsNullOrEmpty(diagnostics) || CurrentLanguage == UiLanguage.Chinese)
                return diagnostics;
            return diagnostics
                .Replace("扫描网段：", "Scan prefixes: ")
                .Replace("；本机已存在 192.168.0.x 地址。", "; local host already has a 192.168.0.x address.")
                .Replace("；以太网当前是 169.254 自动地址，直连板卡请给以太网配置 192.168.0.x/24。", "; Ethernet is using a 169.254 auto address. For direct board connection, set Ethernet to 192.168.0.x/24.")
                .Replace("；直连板卡默认回退地址通常是 192.168.0.30，请确认电脑以太网也在 192.168.0.x/24。", "; direct-board fallback is usually 192.168.0.30. Confirm the PC Ethernet is also in 192.168.0.x/24.")
                .Replace("：", ": ");
        }

        private void AddOrUpdateDevice(Management device)
        {
            if (device == null)
                return;

            int existingIndex = managements.FindIndex(item => IsSameDevice(item, device));
            if (existingIndex >= 0)
                managements[existingIndex] = device;
            else
                managements.Add(device);

            SortDevices();
            RefreshDeviceRows();
        }

        private void ReplaceDevicesFromScanResults(IEnumerable<Management> scanResults)
        {
            managements.Clear();
            foreach (Management device in scanResults)
                AddOrUpdateDeviceWithoutRefresh(device);
            SortDevices();
            RefreshDeviceRows();
        }

        private void AddOrUpdateDeviceWithoutRefresh(Management device)
        {
            if (device == null)
                return;

            int existingIndex = managements.FindIndex(item => IsSameDevice(item, device));
            if (existingIndex >= 0)
                managements[existingIndex] = device;
            else
                managements.Add(device);
        }

        private void SortDevices()
        {
            managements.Sort((left, right) => CompareIp(left.EthIp, right.EthIp));
        }

        private static bool IsSameDevice(Management left, Management right)
        {
            if (left == null || right == null)
                return false;
            if (Equals(left.EthIp, right.EthIp))
                return true;
            if (string.IsNullOrWhiteSpace(left.DeviceId) || string.IsNullOrWhiteSpace(right.DeviceId))
                return false;
            return string.Equals(left.DeviceId, right.DeviceId, StringComparison.Ordinal);
        }

        private static int CompareIp(IPAddress left, IPAddress right)
        {
            byte[] leftBytes = left.GetAddressBytes();
            byte[] rightBytes = right.GetAddressBytes();
            for (int i = 0; i < 4; i++)
            {
                int result = leftBytes[i].CompareTo(rightBytes[i]);
                if (result != 0)
                    return result;
            }
            return 0;
        }

        private static string FormatDuration(TimeSpan value)
        {
            if (value < TimeSpan.Zero)
                value = TimeSpan.Zero;
            if (value.TotalHours >= 1)
                return value.ToString(@"h\:mm\:ss");
            return value.ToString(@"m\:ss");
        }

        private void OpenLogDirectory()
        {
            try
            {
                System.IO.Directory.CreateDirectory(AppLogger.LogDirectory);
                Process.Start("explorer.exe", AppLogger.LogDirectory);
            }
            catch (Exception ex)
            {
                AppLogger.Error("Open log directory failed", ex);
                MessageBox.Show(T("打开日志目录失败：", "Open log folder failed: ") + ex.Message + Environment.NewLine + AppLogger.LogDirectory, T("日志", "Logs"), MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private static string FormatMac(byte[] bytes)
        {
            if (bytes == null || bytes.Length == 0)
                return string.Empty;
            return string.Join("-", bytes.Select(item => item.ToString("x2")).ToArray());
        }
    }
}
