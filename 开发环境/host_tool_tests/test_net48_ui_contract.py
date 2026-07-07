import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "mqttv5_tool" / "mqttv5_tool"
EMULATOR_APP = ROOT / "emqx_device_emulator_tool" / "EmqxDeviceEmulator.App"
HOST_SCAN_PROBE = ROOT / "开发环境" / "host_tool_scan_probe" / "Program.cs"
HOST_CONFIG_PROBE = ROOT / "开发环境" / "host_tool_config_probe" / "Program.cs"
DOCUMENTED_REQUEST_TOPIC = "v1/devices/request/{device_name}"
DOCUMENTED_RESPONSE_TOPIC = "v1/devices/response/{device_name}"


class Net48GatewayHostToolContractTests(unittest.TestCase):
    def test_project_targets_dotnet_framework_48(self):
        csproj = (PROJECT / "mqttv5_tool.csproj").read_text(encoding="utf-8")
        program_cs = (PROJECT / "Program.cs").read_text(encoding="utf-8")

        self.assertTrue(
            "<TargetFrameworkVersion>v4.8</TargetFrameworkVersion>" in csproj
            or "<TargetFramework>net48</TargetFramework>" in csproj
        )
        self.assertIn('<Reference Include="System.Windows.Forms"', csproj)
        self.assertIn('<PackageReference Include="NModbus" Version="3.0.81"', csproj)
        self.assertIn("Application.EnableVisualStyles();", program_cs)
        self.assertIn("Application.SetCompatibleTextRenderingDefault(false);", program_cs)
        self.assertNotIn("ApplicationConfiguration.Initialize()", program_cs)

    def test_main_form_contains_gateway_document_controls(self):
        all_cs = self._read_all_csharp()
        for expected in (
            "查找",
            "高级设置",
            "语言选择",
            "设备列表",
            "重启设备",
            "设置",
            "开始查找",
            "找到0个设备",
            "版本：2.3.8 稳定性修复版",
        ):
            self.assertIn(expected, all_cs)

    def test_device_grid_uses_required_columns(self):
        all_cs = self._read_all_csharp()
        for expected in (
            "序号",
            "设备型号",
            "设备ID",
            "设备状态",
            "IP地址",
            "IP分配",
            "MAC地址",
        ):
            self.assertIn(expected, all_cs)

    def test_single_and_batch_settings_include_required_emqx_fields(self):
        all_cs = self._read_all_csharp()
        for expected in (
            "局域网设置",
            "服务器设置",
            "设备ID",
            "DHCP",
            "DNS服务器",
            "服务器IP或域名",
            "端口",
            "用户名",
            "登录密码",
            "主题1",
            "主题2",
            "主题3",
            "主题4",
            "主题5",
            "NTP地址",
            "Qos",
            "一键设置",
            "设置保存",
            "设置加载",
        ):
            self.assertIn(expected, all_cs)

    def test_settings_dialog_uses_plaintext_password_and_non_overlapping_topic_layout(self):
        form2_cs = (PROJECT / "Form2.cs").read_text(encoding="utf-8")
        advanced_settings_cs = (PROJECT / "AdvancedSettingsForm.cs").read_text(encoding="utf-8")

        self.assertNotIn("textPassword.PasswordChar", form2_cs)
        self.assertNotIn("textPassword.PasswordChar", advanced_settings_cs)
        self.assertIn("AddLabeledText(page, labels[i], 20, y, 220, 500)", form2_cs)
        self.assertIn("textBox.Location = new Point(textX, y)", form2_cs)
        self.assertIn("label.MaximumSize = new Size(textX - x - 10, 0)", form2_cs)

    def test_host_tool_reboots_after_saving_configuration(self):
        form1_cs = (PROJECT / "Form1.cs").read_text(encoding="utf-8")

        self.assertIn("SaveAndRestart", form1_cs)
        self.assertRegex(form1_cs, r"selected\.SaveAndRestart\s*\(\s*\)")
        self.assertRegex(form1_cs, r"device\.SaveAndRestart\s*\(\s*\)")

    def test_host_tool_runs_device_network_operations_off_ui_thread(self):
        form1_cs = (PROJECT / "Form1.cs").read_text(encoding="utf-8")

        self.assertIn("private async void buttonRestart_Click", form1_cs)
        self.assertIn("await Task.Run(() => RestartDevice(selected))", form1_cs)
        self.assertIn("private async Task OpenSelectedDeviceSettingsAsync", form1_cs)
        self.assertIn("await Task.Run(() => ReadSelectedDeviceConfig(selected))", form1_cs)
        self.assertIn("await Task.Run(() => WriteSelectedDeviceConfig(selected))", form1_cs)
        self.assertIn("private void SetBusy(bool busy)", form1_cs)
        self.assertIn("UseWaitCursor = busy", form1_cs)
        self.assertIn("buttonRestart.Enabled = !busy", form1_cs)
        self.assertIn("private async void buttonAdvancedNav_Click", form1_cs)
        self.assertIn("await ApplyBatchSettingsAsync(form)", form1_cs)
        self.assertIn("private async Task ApplyBatchSettingsAsync", form1_cs)
        self.assertIn("await Task.Run(() => ConnectAndUploadDevice(device))", form1_cs)
        self.assertIn("await Task.Run(() => SaveAndRestartConnectedDevice(device))", form1_cs)
        self.assertNotIn("Application.DoEvents()", form1_cs)

    def test_host_tool_validates_emqx_configuration_before_write(self):
        management_cs = (PROJECT / "Management.cs").read_text(encoding="utf-8")
        form2_cs = (PROJECT / "Form2.cs").read_text(encoding="utf-8")
        advanced_settings_cs = (PROJECT / "AdvancedSettingsForm.cs").read_text(encoding="utf-8")

        self.assertIn("ValidateForWrite", management_cs)
        self.assertIn("ValidateTextLength", management_cs)
        self.assertIn("Management.TopicLength", form2_cs)
        self.assertIn("ValidateForWrite", form2_cs)
        self.assertNotIn("五个主题必须互不相同", form2_cs)
        self.assertNotIn("Topics.Distinct", management_cs)
        self.assertIn("Qos 只能是 0、1 或 2。", management_cs)
        self.assertIn("bytes[QosOffset + i] > 2", management_cs)
        self.assertIn("qosBoxes[i].Maximum = 2", form2_cs)
        self.assertIn("qosBoxes[i].Maximum = 2", advanced_settings_cs)
        self.assertIn('byte.TryParse(value, out byte qos)', advanced_settings_cs)

    def test_host_tool_rejects_unsafe_static_network_configuration_before_write(self):
        management_cs = (PROJECT / "Management.cs").read_text(encoding="utf-8")
        form2_cs = (PROJECT / "Form2.cs").read_text(encoding="utf-8")

        self.assertIn("ValidateStaticNetwork", management_cs)
        self.assertIn("IsValidSubnetMask", management_cs)
        self.assertIn("IsUsableHostAddress", management_cs)
        self.assertIn("IsSameSubnet", management_cs)
        self.assertIn("IsValidUnicastMac", management_cs)
        self.assertIn("静态 IP 地址不能是 0.0.0.0、广播、回环或组播地址。", management_cs)
        self.assertIn("子网掩码必须是连续掩码，且不能是 0.0.0.0 或 255.255.255.255。", management_cs)
        self.assertIn("默认网关必须和静态 IP 在同一子网。", management_cs)
        self.assertIn("DNS服务器不能是 0.0.0.0、广播、回环或组播地址。", management_cs)
        self.assertIn("MAC地址不能为空、全 FF 或组播地址。", management_cs)
        self.assertIn("ValidateStaticNetworkFields", form2_cs)
        self.assertIn("Management.ValidateStaticNetworkFields(ip, sn, gw, dns, loadedMac)", form2_cs)

    def test_scan_reuses_probe_connection_for_single_socket_w5500(self):
        management_cs = (PROJECT / "Management.cs").read_text(encoding="utf-8")

        self.assertIn("private bool Connect(TcpClient connectedClient)", management_cs)
        self.assertIn("AttachConnectedClient(connectedClient)", management_cs)
        self.assertIn("management.Connect(client);", management_cs)
        self.assertNotIn("management.Connect();\n                    management.Upload();", management_cs)
        self.assertIn("LingerOption(true, 0)", management_cs)
        self.assertIn("connectedClient.NoDelay = true", management_cs)

    def test_scan_is_throttled_for_w5500_and_uses_longer_timeouts(self):
        management_cs = (PROJECT / "Management.cs").read_text(encoding="utf-8")

        self.assertIn("ScanConcurrencyLimit", management_cs)
        self.assertIn("SemaphoreSlim", management_cs)
        self.assertIn("WaitAsync", management_cs)
        self.assertRegex(management_cs, r"ScanConnectTimeoutMs\s*=\s*1000")
        self.assertIn("master.Transport.ReadTimeout = 1000", management_cs)
        self.assertIn("master.Transport.WriteTimeout = 1000", management_cs)
        self.assertIn("ScanUploadRetryCount", management_cs)

    def test_scan_prioritizes_default_board_addresses_before_full_subnet(self):
        management_cs = (PROJECT / "Management.cs").read_text(encoding="utf-8")

        self.assertIn("PriorityScanHosts", management_cs)
        self.assertRegex(management_cs, r"PriorityScanHosts\s*=\s*new\s+int\[\]\s*\{\s*108\s*,\s*30\s*,\s*111\s*,\s*218\s*\}")
        self.assertIn("ScanPriorityCandidatesAsync", management_cs)
        self.assertRegex(
            management_cs,
            r"ScanPriorityCandidatesAsync\s*\([\s\S]+foreach\s*\(\s*int host in PriorityScanHosts\s*\)",
        )
        self.assertIn("scannedIps", management_cs)

    def test_scan_probe_returns_first_found_device_when_no_expected_ip_is_given(self):
        probe_cs = HOST_SCAN_PROBE.read_text(encoding="utf-8")

        self.assertIn("string expectedIp = args.Length > 0 ? args[0].Trim() : string.Empty", probe_cs)
        self.assertIn("FirstDeviceTimeoutMs", probe_cs)
        self.assertIn("ManualResetEventSlim", probe_cs)
        self.assertIn("Management.Scan(devices, device =>", probe_cs)
        self.assertIn('Console.WriteLine("SCAN_MODE=first")', probe_cs)
        self.assertIn('Console.WriteLine("FIRST_IP=" + devices[0].EthIp);', probe_cs)
        self.assertIn("if (string.IsNullOrEmpty(expectedIp))", probe_cs)
        self.assertIn("return ScanFirstDevice();", probe_cs)

    def test_config_probe_roundtrips_mqtt_tls_fields_for_8884(self):
        probe_cs = HOST_CONFIG_PROBE.read_text(encoding="utf-8")

        self.assertIn('DefaultBoardIp = "192.168.0.30"', probe_cs)
        self.assertIn("device.TlsMode = 2;", probe_cs)
        self.assertIn("device.TlsVerifyPeer = 1;", probe_cs)
        self.assertIn('Console.WriteLine("TlsMode=" + device.TlsMode);', probe_cs)
        self.assertIn('Console.WriteLine("TlsVerifyPeer=" + device.TlsVerifyPeer);', probe_cs)

    def test_config_probe_can_write_server_request_response_topics(self):
        probe_cs = HOST_CONFIG_PROBE.read_text(encoding="utf-8")

        self.assertIn("write-unified-request-topic", probe_cs)
        self.assertIn("write-single-request-topic", probe_cs)
        self.assertIn("write-server-topics", probe_cs)
        self.assertIn("ApplyServerTopics", probe_cs)
        self.assertIn("VerifyServerTopics", probe_cs)
        self.assertIn('"v1/devices/request/"', probe_cs)
        self.assertIn('"v1/devices/response/"', probe_cs)
        self.assertIn("BuildRequestTopic", probe_cs)
        self.assertIn("BuildResponseTopic", probe_cs)
        self.assertIn("BuildDeviceName", probe_cs)
        self.assertRegex(
            probe_cs,
            r"device\.Topics\[0\]\s*=\s*responseTopic[\s\S]+"
            r"device\.Topics\[1\]\s*=\s*requestTopic[\s\S]+"
            r"device\.Topics\[2\]\s*=\s*responseTopic[\s\S]+"
            r"device\.Topics\[3\]\s*=\s*requestTopic[\s\S]+"
            r"device\.Topics\[4\]\s*=\s*responseTopic",
        )

    def test_config_probe_can_write_static_ip_and_restore_dhcp(self):
        probe_cs = HOST_CONFIG_PROBE.read_text(encoding="utf-8")

        self.assertIn("write-static", probe_cs)
        self.assertIn("write-dhcp", probe_cs)
        self.assertIn("ApplyStaticNetwork", probe_cs)
        self.assertIn("ApplyDhcpNetwork", probe_cs)
        self.assertIn("VerifyStaticNetwork", probe_cs)
        self.assertIn("VerifyDhcpNetwork", probe_cs)
        self.assertIn("connected.Device.SaveAndRestart();", probe_cs)
        self.assertIn('Console.WriteLine("SAVE_AND_RESTART_SENT=1");', probe_cs)
        self.assertIn('Console.WriteLine("STATIC_CONFIG_ROUNDTRIP_OK=1");', probe_cs)
        self.assertIn('Console.WriteLine("DHCP_CONFIG_ROUNDTRIP_OK=1");', probe_cs)

    def test_scan_uses_begin_connect_timeout_for_net48_w5500_compatibility(self):
        management_cs = (PROJECT / "Management.cs").read_text(encoding="utf-8")

        self.assertIn("BeginConnect", management_cs)
        self.assertIn("PriorityScanConnectTimeoutMs", management_cs)
        self.assertIn("PriorityScanRetryDelayMs", management_cs)
        self.assertIn("AsyncWaitHandle.WaitOne(timeoutMs)", management_cs)
        self.assertIn("ScanPriorityIpBlockingAsync", management_cs)
        self.assertRegex(management_cs, r"ScanPriorityIpBlocking[\s\S]+ConnectClientWithTimeout\(client, ip, attemptTimeoutMs\)")
        self.assertRegex(management_cs, r"ScanPriorityIpBlocking[\s\S]+management\.Connect\(client\);")
        self.assertRegex(management_cs, r"ScanPriorityCandidatesAsync[\s\S]+PriorityScanConnectTimeoutMs")
        self.assertIn("retryUntilReady", management_cs)
        self.assertIn("DateTime.UtcNow", management_cs)
        self.assertIn("EndConnect", management_cs)
        self.assertNotIn("ConnectAsync(ip, ModbusPort)", management_cs)

    def test_direct_management_connect_retries_during_board_socket_startup(self):
        management_cs = (PROJECT / "Management.cs").read_text(encoding="utf-8")

        self.assertIn("ManagementConnectRetryCount", management_cs)
        self.assertIn("ManagementConnectRetryDelayMs", management_cs)
        self.assertRegex(management_cs, r"for\s*\(\s*int attempt = 0; attempt < ManagementConnectRetryCount; attempt\+\+\s*\)")
        self.assertIn("Thread.Sleep(ManagementConnectRetryDelayMs)", management_cs)
        self.assertIn("client.Close();", management_cs)

    def test_scan_includes_default_direct_board_subnet_and_reports_network_diagnostics(self):
        management_cs = (PROJECT / "Management.cs").read_text(encoding="utf-8")
        form1_cs = (PROJECT / "Form1.cs").read_text(encoding="utf-8")

        self.assertIn("DefaultBoardScanPrefixes", management_cs)
        self.assertIn('"192.168.0."', management_cs)
        self.assertIn('"192.168.10."', management_cs)
        self.assertIn("LastScanDiagnostics", management_cs)
        self.assertIn("169.254", management_cs)
        self.assertIn("以太网", management_cs)
        self.assertIn("LastScanDiagnostics", form1_cs)

    def test_host_tool_has_persistent_scan_logging(self):
        all_cs = self._read_all_csharp()
        management_cs = (PROJECT / "Management.cs").read_text(encoding="utf-8")
        form1_cs = (PROJECT / "Form1.cs").read_text(encoding="utf-8")
        designer_cs = (PROJECT / "Form1.Designer.cs").read_text(encoding="utf-8")

        self.assertIn("static class AppLogger", all_cs)
        self.assertIn("LogDirectory", all_cs)
        self.assertIn("LogFilePath", all_cs)
        self.assertIn("logs", all_cs)
        self.assertIn("ScanBegin", management_cs)
        self.assertIn("ScanCandidate", management_cs)
        self.assertIn("ScanSuccess", management_cs)
        self.assertIn("ScanFailure", management_cs)
        self.assertIn("ScanEnd", management_cs)
        self.assertIn("AppLogger.Info", form1_cs)
        self.assertIn("打开日志目录", designer_cs)
        self.assertIn("buttonOpenLogs", designer_cs)
        self.assertIn("OpenLogDirectory", form1_cs)
        self.assertIn("AppLogger.LogFilePath", form1_cs)

    def test_scan_reports_devices_to_ui_while_background_scan_continues(self):
        management_cs = (PROJECT / "Management.cs").read_text(encoding="utf-8")
        form1_cs = (PROJECT / "Form1.cs").read_text(encoding="utf-8")

        self.assertRegex(management_cs, r"ScanConcurrencyLimit\s*=\s*5")
        self.assertIn("Action<Management> onDeviceFound = null", management_cs)
        self.assertIn("NotifyDeviceFound(onDeviceFound, management)", management_cs)
        self.assertIn("BuildScanCandidateIps", management_cs)
        self.assertIn("GetLocalIpv4Prefixes", management_cs)
        self.assertNotIn("if (managements.Count > 0)\n            {\n                managements.Sort", management_cs)

        self.assertIn("OnDeviceFoundDuringScan", form1_cs)
        self.assertIn("BeginInvoke(new Action", form1_cs)
        self.assertIn("AddOrUpdateDevice", form1_cs)
        self.assertIn("Management.Scan(scanResults, OnDeviceFoundDuringScan, OnScanProgress, scanCancellation.Token)", form1_cs)

    def test_scan_reports_progress_percentage_remaining_time_and_five_workers(self):
        management_cs = (PROJECT / "Management.cs").read_text(encoding="utf-8")
        form1_cs = (PROJECT / "Form1.cs").read_text(encoding="utf-8")
        designer_cs = (PROJECT / "Form1.Designer.cs").read_text(encoding="utf-8")

        self.assertIn("public sealed class ScanProgressInfo", management_cs)
        self.assertIn("Action<ScanProgressInfo> onProgress = null", management_cs)
        self.assertIn("ReportScanProgress", management_cs)
        self.assertIn("Percent", management_cs)
        self.assertIn("EstimatedRemaining", management_cs)
        self.assertRegex(management_cs, r"ScanConcurrencyLimit\s*=\s*5")

        for expected in (
            "progressScan",
            "labelScanCurrent",
            "labelScanProgress",
            "OnScanProgress",
            "UpdateScanProgress",
            "预计剩余",
            "五线程并发",
        ):
            self.assertIn(expected, form1_cs + designer_cs)
        self.assertIn("ProgressBar", designer_cs)

    def test_scan_can_be_cancelled_from_stop_scan_button(self):
        management_cs = (PROJECT / "Management.cs").read_text(encoding="utf-8")
        form1_cs = (PROJECT / "Form1.cs").read_text(encoding="utf-8")
        designer_cs = (PROJECT / "Form1.Designer.cs").read_text(encoding="utf-8")
        ui_language_cs = (PROJECT / "UiLanguage.cs").read_text(encoding="utf-8")

        self.assertIn("CancellationToken", management_cs)
        self.assertIn("cancellationToken.ThrowIfCancellationRequested()", management_cs)
        self.assertIn("scanLimiter.WaitAsync(cancellationToken)", management_cs)
        self.assertIn("buttonStopSearch", designer_cs)
        self.assertIn('this.buttonStopSearch.Text = "结束查找";', designer_cs)
        self.assertIn("buttonStopSearch_Click", form1_cs)
        self.assertIn("scanCancellation", form1_cs)
        self.assertIn("scanCancellation.Cancel();", form1_cs)
        self.assertIn("Management.Scan(scanResults, OnDeviceFoundDuringScan, OnScanProgress, scanCancellation.Token)", form1_cs)
        self.assertIn("catch (OperationCanceledException)", form1_cs)
        self.assertIn("SetScanning(false)", form1_cs)
        self.assertIn('{ "结束查找", "Stop Scan" }', ui_language_cs)

    def test_advanced_settings_topic_inputs_do_not_overlap_topic_labels(self):
        advanced_settings_cs = (PROJECT / "AdvancedSettingsForm.cs").read_text(encoding="utf-8")

        self.assertIn("TopicLabelWidth", advanced_settings_cs)
        self.assertIn("TopicTextX", advanced_settings_cs)
        self.assertIn("label.Size = new Size(TopicLabelWidth, 23)", advanced_settings_cs)
        self.assertIn("label.AutoEllipsis = true", advanced_settings_cs)
        self.assertIn("topicBoxes[i] = AddTextBox(serverGroup, TopicTextX, y, TopicTextWidth, defaults[i]);", advanced_settings_cs)
        self.assertNotIn("topicBoxes[i] = AddTextBox(serverGroup, 100, y, 570, defaults[i]);", advanced_settings_cs)

    def test_host_tool_embeds_mqtt_client_panel_with_selectable_topics(self):
        csproj = (PROJECT / "mqttv5_tool.csproj").read_text(encoding="utf-8")
        all_cs = self._read_all_csharp()
        form1_cs = (PROJECT / "Form1.cs").read_text(encoding="utf-8")
        mqtt_client_form = (PROJECT / "MqttClientForm.cs").read_text(encoding="utf-8")
        host_settings = (PROJECT / "HostToolSettings.cs").read_text(encoding="utf-8")

        self.assertIn('<PackageReference Include="MQTTnet" Version="4.3.7.1207"', csproj)
        self.assertIn("buttonMqttClient", all_cs)
        self.assertIn("HostToolSettings.Load()", form1_cs)
        self.assertIn("MqttClientEnabled", host_settings)
        self.assertIn("MqttStatusTopic", host_settings)
        self.assertIn("MqttStatusSubscribe", host_settings)
        self.assertIn("host_tool_settings.ini", host_settings)
        self.assertIn("MqttProtocolVersion.V500", mqtt_client_form)
        self.assertIn("topicSubscribeChecks", mqtt_client_form)
        self.assertIn("topicTextBoxes", mqtt_client_form)
        self.assertIn("checkStatusSubscribe", mqtt_client_form)
        self.assertIn("textStatusTopic", mqtt_client_form)
        self.assertIn("BuildEditableTopicRows", mqtt_client_form)
        self.assertIn("订阅候选来自五个完整业务主题和状态主题", mqtt_client_form)
        self.assertIn("LoadTopicsFromDevice", mqtt_client_form)
        self.assertIn("SaveCurrentToSettings", mqtt_client_form)
        self.assertIn("comboPublishTopic.DropDownStyle = ComboBoxStyle.DropDownList", mqtt_client_form)
        self.assertIn("不能向包含 + 或 # 的订阅通配符主题发布", mqtt_client_form)
        self.assertNotIn("comboPublishTopic.DropDownStyle = ComboBoxStyle.DropDown;", mqtt_client_form)
        self.assertNotIn("BuildCityDeviceTopics", mqtt_client_form)

    def test_mqtt_client_decodes_messages_into_table_details_and_field_meanings(self):
        mqtt_client_form = (PROJECT / "MqttClientForm.cs").read_text(encoding="utf-8")
        host_settings = (PROJECT / "HostToolSettings.cs").read_text(encoding="utf-8")

        for expected in (
            "dataGridMessages",
            "tabMain",
            "tabConnectionSettings",
            "tabTopicPublish",
            "tabMessageBrowse",
            "tabMessageDetails",
            "tabMessages",
            "tabTelemetry",
            "tabEvent",
            "tabCommand",
            "tabOta",
            "tabDebug",
            "tabStatus",
            "dataGridFields",
            "textRawPayload",
            "textFormattedJson",
            "textMessageMeaning",
            "topicSendButtons",
            "AppendMessageRecord",
            "BuildMessageRecord",
            "ShowMessageDetails",
            "ExplainPayload",
            "BuildFieldRows",
            "FormatJson",
            "DescribeField",
            "MqttMessageRecord",
            "FieldRow",
            "发送到此主题",
            "原始报文",
            "格式化 JSON",
            "字段解释",
            "数据含义",
            "上行遥测",
            "事件告警",
            "下行/get",
            "OTA",
            "调试/兼容",
            "状态/LWT",
        ):
            self.assertIn(expected, mqtt_client_form)

        self.assertIn("MqttConnectTimeoutSeconds", host_settings)
        self.assertIn("numericConnectTimeoutSeconds", mqtt_client_form)
        self.assertIn("CancelAfter(TimeSpan.FromSeconds", mqtt_client_form)

    def test_mqtt_client_topic_layout_keeps_action_buttons_below_topic_rows(self):
        mqtt_client_form = (PROJECT / "MqttClientForm.cs").read_text(encoding="utf-8")

        self.assertIn("topicGroup.Size = new Size(1228, 286)", mqtt_client_form)
        self.assertIn("int y = 48 + i * 28", mqtt_client_form)
        self.assertIn("BuildStatusTopicRow", mqtt_client_form)
        self.assertIn("int y = 198", mqtt_client_form)
        self.assertIn("buttonLoadDevice.Location = new Point(16, 246)", mqtt_client_form)
        self.assertIn("buttonRefreshTopics.Location = new Point(146, 246)", mqtt_client_form)
        self.assertIn("publishGroup.Location = new Point(12, 310)", mqtt_client_form)
        self.assertIn('textPassword = AddText(connectionGroup, "Password", 390, 68, 480, 220)', mqtt_client_form)
        self.assertIn("tabMessageBrowse.Controls.Add(tabMessages)", mqtt_client_form)
        self.assertIn("tabMessageDetails.Controls.Add(detailGroup)", mqtt_client_form)
        self.assertIn("tabDetails.TabPages.Add(tabRawLog)", mqtt_client_form)
        self.assertNotIn("tabMessages.TabPages.Add(tabRawLog)", mqtt_client_form)

    def test_mqtt_client_closes_without_blocking_ui_thread(self):
        mqtt_client_form = (PROJECT / "MqttClientForm.cs").read_text(encoding="utf-8")

        self.assertIn("DisposeMqttClientImmediately", mqtt_client_form)
        self.assertIn("isClosing = true", mqtt_client_form)
        self.assertIn("ApplicationMessageReceivedAsync -= OnMqttMessageReceived", mqtt_client_form)
        self.assertIn("DisconnectedAsync -= OnMqttDisconnected", mqtt_client_form)
        self.assertIn("Task.WhenAny(disconnectTask, Task.Delay", mqtt_client_form)
        self.assertNotIn("DisconnectMqttAsync().GetAwaiter().GetResult()", mqtt_client_form)

    def test_mqtt_client_does_not_steal_selected_message_on_new_rx(self):
        mqtt_client_form = (PROJECT / "MqttClientForm.cs").read_text(encoding="utf-8")

        for expected in (
            "checkAutoFollowLatest",
            "自动跟随最新报文",
            "buttonSelectLatest",
            "SelectLatestRecord",
            "selectedMessageRecord",
            "suppressGridSelectionChanged",
            "ShouldAutoFollowLatest",
            "RestoreGridSelection",
            "GetSelectedRecord",
            "SelectGridRow",
        ):
            self.assertIn(expected, mqtt_client_form)

        self.assertNotIn("grid.ClearSelection();\n                grid.Rows[row].Selected = true;", mqtt_client_form)
        self.assertNotIn("ShowMessageDetails(record);\n        }\n\n        private void AddRecordToGrid", mqtt_client_form)

    def test_host_tool_and_embedded_mqtt_client_support_chinese_english_ui(self):
        all_cs = self._read_all_csharp()
        host_settings = (PROJECT / "HostToolSettings.cs").read_text(encoding="utf-8")
        form1_cs = (PROJECT / "Form1.cs").read_text(encoding="utf-8")
        mqtt_client_form = (PROJECT / "MqttClientForm.cs").read_text(encoding="utf-8")

        for expected in (
            "public string Language",
            "language=",
            "UiLanguage",
            "UiText",
            "zh-CN",
            "en-US",
            "English",
            "中文",
        ):
            self.assertIn(expected, all_cs + host_settings)

        for expected in (
            "ApplyLanguage",
            "comboBoxLanguage.SelectedIndexChanged",
            "settings.Language",
            "Language",
            "Find",
            "Advanced Settings",
            "Device List",
            "Start Scan",
            "Open Log Folder",
            "Five workers",
        ):
            self.assertIn(expected, form1_cs + all_cs)

        for expected in (
            "ApplyLanguage",
            "EMQX Client",
            "Connection Parameters",
            "Topics / Publish",
            "Message Browser",
            "Message Details / Logs",
            "Telemetry",
            "Event / Alarm",
            "Command / get",
            "Raw Payload",
            "Formatted JSON",
            "Field Meaning",
            "Subscribe",
            "Send",
            "Save Config",
        ):
            self.assertIn(expected, mqtt_client_form + all_cs)

    def test_main_grid_translates_dynamic_status_mode_and_scan_text_in_english(self):
        form1_cs = (PROJECT / "Form1.cs").read_text(encoding="utf-8")
        management_cs = (PROJECT / "Management.cs").read_text(encoding="utf-8")
        ui_language_cs = (PROJECT / "UiLanguage.cs").read_text(encoding="utf-8")

        self.assertIn("TranslateDeviceStatus", form1_cs)
        self.assertIn("TranslateIpAssignText", form1_cs)
        self.assertIn("TranslateScanStage", form1_cs)
        self.assertIn("TranslateScanDiagnostics", form1_cs)
        self.assertIn("RefreshDeviceRows();", form1_cs)
        self.assertNotIn("item.Status,\n                    item.EthIp", form1_cs)
        self.assertNotIn("item.IpAssignText,\n                    FormatMac", form1_cs)
        self.assertIn("TranslateDeviceStatus(item.Status)", form1_cs)
        self.assertIn("TranslateIpAssignText(item)", form1_cs)
        self.assertIn("TranslateScanStage(progress.Stage)", form1_cs)
        self.assertIn("TranslateScanDiagnostics(Management.LastScanDiagnostics)", form1_cs)

        for expected in (
            '{ "正常运行", "Running" }',
            '{ "设置成功", "Settings written" }',
            '{ "设置成功，设备重启中", "Settings written; rebooting" }',
            '{ "静态", "Static" }',
            '{ "动态", "Dynamic" }',
            '{ "扫描完成", "scan completed" }',
        ):
            self.assertIn(expected, ui_language_cs)
        self.assertIn('get { return Mode == 0 ? "静态" : "动态"; }', management_cs)

    def test_static_ip_write_waits_for_new_management_address_before_refreshing_grid(self):
        form1_cs = (PROJECT / "Form1.cs").read_text(encoding="utf-8")
        management_cs = (PROJECT / "Management.cs").read_text(encoding="utf-8")

        self.assertIn("WaitForDevice", management_cs)
        self.assertIn("CopyConnectionStateFrom", management_cs)
        self.assertIn("SaveRestartReconnectTimeoutMs", management_cs)
        self.assertIn("GetExpectedManagementIpAfterWrite", form1_cs)
        self.assertIn("Management.WaitForDevice(expectedIp, SaveRestartReconnectTimeoutMs)", form1_cs)
        self.assertIn("selected.CopyConnectionStateFrom(reconnected);", form1_cs)
        self.assertIn("WaitForDeviceAfterSaveAndRestart", form1_cs)
        self.assertIn("SaveAndRestartConnectedDevice(device)", form1_cs)

    def test_independent_emqx_device_emulator_supports_chinese_english_ui(self):
        emulator_cs = "\n".join(path.read_text(encoding="utf-8") for path in EMULATOR_APP.glob("*.cs"))

        for expected in (
            "public string Language",
            "language=",
            "UiLanguage",
            "UiText",
            "zh-CN",
            "en-US",
            "English",
            "中文",
            "ApplyLanguage",
            "EMQX Device Emulator",
            "Connection / Topics",
            "Downlink / get",
            "OTA Flow",
            "Responses",
            "Status / LWT",
            "Raw Logs",
            "Message Details",
            "Connect",
            "Disconnect",
            "Save Config",
            "Auto-follow latest message",
        ):
            self.assertIn(expected, emulator_cs)

    def test_default_topics_match_server_request_response_contract(self):
        all_cs = self._read_all_csharp()
        management_cs = (PROJECT / "Management.cs").read_text(encoding="utf-8")

        self.assertIn(DOCUMENTED_REQUEST_TOPIC, management_cs)
        self.assertIn(DOCUMENTED_RESPONSE_TOPIC, management_cs)
        self.assertIn("BuildDefaultTopics", management_cs)
        self.assertRegex(
            management_cs,
            r"topics\[0\]\s*=\s*responseTopic[\s\S]+"
            r"topics\[1\]\s*=\s*requestTopic[\s\S]+"
            r"topics\[2\]\s*=\s*responseTopic[\s\S]+"
            r"topics\[3\]\s*=\s*requestTopic[\s\S]+"
            r"topics\[4\]\s*=\s*responseTopic",
        )
        self.assertNotIn("city/{city_id}/pole/{pole_id}/device/{device_name}", management_cs)
        self.assertNotIn("/local_pk/hc32f460_dev/user", all_cs)
        self.assertIn("v1/devices/status/+", all_cs)

        for label in (
            "上行/遥测",
            "下行/控制",
            "事件/告警",
            "OTA在线升级",
            "调试/兼容",
        ):
            self.assertIn(label, all_cs)

    def _read_all_csharp(self):
        return "\n".join(path.read_text(encoding="utf-8") for path in PROJECT.glob("*.cs"))


if __name__ == "__main__":
    unittest.main()
