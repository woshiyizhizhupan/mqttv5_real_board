using MQTTnet;
using MQTTnet.Client;
using MQTTnet.Formatter;
using MQTTnet.Protocol;
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using EmqxDeviceEmulator.Protocol;

namespace EmqxDeviceEmulator.App
{
    public sealed class DeviceEmulatorForm : Form
    {
        private const int MaxRows = 2000;
        private static readonly JavaScriptSerializer JsonSerializer = new JavaScriptSerializer();

        private readonly bool autoConnectOnShown;
        private DeviceEmulatorSettings settings;
        private DeviceProtocolEmulator emulator;
        private IMqttClient mqttClient;
        private bool isClosing;
        private bool suppressSelectionChanged;
        private MessageRecord selectedRecord;

        private TextBox textHost;
        private TextBox textPort;
        private TextBox textClientId;
        private TextBox textUsername;
        private TextBox textPassword;
        private TextBox textDeviceId;
        private TextBox textFirmwareVersion;
        private TextBox textTelemetryTopic;
        private TextBox textCommandTopic;
        private TextBox textEventTopic;
        private TextBox textOtaTopic;
        private TextBox textDebugTopic;
        private TextBox textStatusTopic;
        private TextBox textExtraSubscriptions;
        private TextBox textRawLog;
        private TextBox textMeaning;
        private TextBox textRawPayload;
        private TextBox textFormattedJson;
        private NumericUpDown numericQos;
        private NumericUpDown numericTimeout;
        private CheckBox checkTls;
        private CheckBox checkAutoFollow;
        private ComboBox comboLanguage;
        private Button buttonConnect;
        private Button buttonDisconnect;
        private Label labelStatus;
        private DataGridView gridDownlink;
        private DataGridView gridOta;
        private DataGridView gridResponses;
        private DataGridView gridStatus;
        private DataGridView gridFields;

        public DeviceEmulatorForm(bool autoConnect)
        {
            autoConnectOnShown = autoConnect;
            settings = DeviceEmulatorSettings.Load();
            emulator = new DeviceProtocolEmulator(settings.Topics, settings.DeviceId, settings.FirmwareVersion);
            InitializeComponent();
            LoadSettingsToUi();
            ApplyLanguage();
        }

        private UiLanguage CurrentLanguage
        {
            get { return UiText.Parse(settings.Language); }
        }

        private string T(string chinese, string english)
        {
            return UiText.T(CurrentLanguage, chinese, english);
        }

        private void ApplyLanguage()
        {
            UiLanguage language = CurrentLanguage;
            UiText.Apply(this, language);
            comboLanguage.SelectedIndexChanged -= comboLanguage_SelectedIndexChanged;
            comboLanguage.Items.Clear();
            comboLanguage.Items.Add(UiText.LanguageDisplay(UiLanguage.Chinese));
            comboLanguage.Items.Add(UiText.LanguageDisplay(UiLanguage.English));
            comboLanguage.SelectedIndex = language == UiLanguage.English ? 1 : 0;
            comboLanguage.SelectedIndexChanged += comboLanguage_SelectedIndexChanged;
        }

        private void comboLanguage_SelectedIndexChanged(object sender, EventArgs e)
        {
            settings.Language = comboLanguage.SelectedIndex == 1 ? "en-US" : "zh-CN";
            settings.Save();
            ApplyLanguage();
        }

        protected override async void OnShown(EventArgs e)
        {
            base.OnShown(e);
            if (autoConnectOnShown)
                await ConnectMqttAsync();
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            isClosing = true;
            DisposeMqttClientImmediately();
            base.OnFormClosing(e);
        }

        private void InitializeComponent()
        {
            Font = new Font("Microsoft YaHei UI", 9F, FontStyle.Regular, GraphicsUnit.Point, 134);
            Text = "EMQX 模拟设备端客户端";
            Size = new Size(1380, 900);
            MinimumSize = new Size(1180, 760);

            TabControl tabs = new TabControl();
            tabs.Dock = DockStyle.Fill;
            Controls.Add(tabs);

            TabPage tabConnection = new TabPage("连接/主题");
            TabPage tabDownlink = new TabPage("下行/get");
            TabPage tabOta = new TabPage("OTA流程");
            TabPage tabResponses = new TabPage("响应上报");
            TabPage tabStatus = new TabPage("状态/LWT");
            TabPage tabRaw = new TabPage("原始日志");
            TabPage tabDetails = new TabPage("报文详情");
            tabs.TabPages.Add(tabConnection);
            tabs.TabPages.Add(tabDownlink);
            tabs.TabPages.Add(tabOta);
            tabs.TabPages.Add(tabResponses);
            tabs.TabPages.Add(tabStatus);
            tabs.TabPages.Add(tabRaw);
            tabs.TabPages.Add(tabDetails);

            BuildConnectionTab(tabConnection);
            gridDownlink = BuildGrid();
            tabDownlink.Controls.Add(gridDownlink);
            gridOta = BuildGrid();
            tabOta.Controls.Add(gridOta);
            gridResponses = BuildGrid();
            tabResponses.Controls.Add(gridResponses);
            gridStatus = BuildGrid();
            tabStatus.Controls.Add(gridStatus);

            textRawLog = new TextBox();
            textRawLog.Multiline = true;
            textRawLog.ReadOnly = true;
            textRawLog.ScrollBars = ScrollBars.Both;
            textRawLog.WordWrap = false;
            textRawLog.Dock = DockStyle.Fill;
            tabRaw.Controls.Add(textRawLog);

            BuildDetailsTab(tabDetails);
        }

        private void BuildConnectionTab(TabPage page)
        {
            Panel root = new Panel();
            root.Dock = DockStyle.Fill;
            root.AutoScroll = true;
            page.Controls.Add(root);

            GroupBox connectionGroup = new GroupBox();
            connectionGroup.Text = "EMQX连接";
            connectionGroup.Location = new Point(12, 12);
            connectionGroup.Size = new Size(1300, 160);
            connectionGroup.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            root.Controls.Add(connectionGroup);

            textHost = AddText(connectionGroup, "Host", 16, 30, 100, 260);
            textPort = AddText(connectionGroup, "Port", 400, 30, 470, 80);
            textClientId = AddText(connectionGroup, "ClientId", 590, 30, 670, 260);
            textUsername = AddText(connectionGroup, "Username", 16, 70, 100, 260);
            textPassword = AddText(connectionGroup, "Password", 400, 70, 500, 220);
            textDeviceId = AddText(connectionGroup, "DeviceId", 760, 70, 835, 200);
            textFirmwareVersion = AddText(connectionGroup, "固件版本", 16, 110, 100, 300);

            connectionGroup.Controls.Add(MakeLabel("QoS", 430, 113, 40));
            numericQos = new NumericUpDown();
            numericQos.Minimum = 0;
            numericQos.Maximum = 2;
            numericQos.Location = new Point(470, 110);
            numericQos.Width = 55;
            connectionGroup.Controls.Add(numericQos);

            connectionGroup.Controls.Add(MakeLabel("超时(s)", 550, 113, 65));
            numericTimeout = new NumericUpDown();
            numericTimeout.Minimum = 2;
            numericTimeout.Maximum = 30;
            numericTimeout.Location = new Point(615, 110);
            numericTimeout.Width = 65;
            connectionGroup.Controls.Add(numericTimeout);

            checkTls = new CheckBox();
            checkTls.Text = "TLS";
            checkTls.Location = new Point(705, 112);
            checkTls.AutoSize = true;
            connectionGroup.Controls.Add(checkTls);

            connectionGroup.Controls.Add(MakeLabel("语言", 760, 113, 50));
            comboLanguage = new ComboBox();
            comboLanguage.DropDownStyle = ComboBoxStyle.DropDownList;
            comboLanguage.Location = new Point(810, 110);
            comboLanguage.Size = new Size(130, 25);
            comboLanguage.SelectedIndexChanged += comboLanguage_SelectedIndexChanged;
            connectionGroup.Controls.Add(comboLanguage);

            buttonConnect = new Button();
            buttonConnect.Text = "连接";
            buttonConnect.Location = new Point(950, 106);
            buttonConnect.Size = new Size(90, 30);
            buttonConnect.Click += async (sender, args) => await ConnectMqttAsync();
            connectionGroup.Controls.Add(buttonConnect);

            buttonDisconnect = new Button();
            buttonDisconnect.Text = "断开";
            buttonDisconnect.Location = new Point(1050, 106);
            buttonDisconnect.Size = new Size(90, 30);
            buttonDisconnect.Click += async (sender, args) => await DisconnectMqttAsync();
            connectionGroup.Controls.Add(buttonDisconnect);

            Button buttonSave = new Button();
            buttonSave.Text = "保存配置";
            buttonSave.Location = new Point(1150, 106);
            buttonSave.Size = new Size(95, 30);
            buttonSave.Click += (sender, args) => SaveUiToSettings();
            connectionGroup.Controls.Add(buttonSave);

            Button buttonClear = new Button();
            buttonClear.Text = "清空日志";
            buttonClear.Location = new Point(1150, 68);
            buttonClear.Size = new Size(95, 30);
            buttonClear.Click += (sender, args) => ClearLogs();
            connectionGroup.Controls.Add(buttonClear);

            labelStatus = MakeLabel("未连接", 1110, 32, 160);
            labelStatus.ForeColor = Color.DarkRed;
            connectionGroup.Controls.Add(labelStatus);

            GroupBox topicGroup = new GroupBox();
            topicGroup.Text = "当前业务主题（单request/response/status，兼容五主题）";
            topicGroup.Location = new Point(12, 184);
            topicGroup.Size = new Size(1300, 292);
            topicGroup.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            root.Controls.Add(topicGroup);

            textTelemetryTopic = AddText(topicGroup, "telemetry_up 上行/响应", 16, 34, 190, 1010);
            textCommandTopic = AddText(topicGroup, "cmd_down 下行/request", 16, 72, 190, 1010);
            textEventTopic = AddText(topicGroup, "event_up 事件/响应", 16, 110, 190, 1010);
            textOtaTopic = AddText(topicGroup, "ota_down OTA/request", 16, 148, 190, 1010);
            textDebugTopic = AddText(topicGroup, "debug_up 调试/响应", 16, 186, 190, 1010);
            textStatusTopic = AddText(topicGroup, "status 状态/LWT", 16, 224, 190, 1010);

            GroupBox subGroup = new GroupBox();
            subGroup.Text = "额外订阅主题，一行一个；用于临时监听对方实际下发topic或通配符";
            subGroup.Location = new Point(12, 488);
            subGroup.Size = new Size(1300, 170);
            subGroup.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            root.Controls.Add(subGroup);

            textExtraSubscriptions = new TextBox();
            textExtraSubscriptions.Multiline = true;
            textExtraSubscriptions.ScrollBars = ScrollBars.Vertical;
            textExtraSubscriptions.Location = new Point(16, 28);
            textExtraSubscriptions.Size = new Size(1250, 90);
            textExtraSubscriptions.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            subGroup.Controls.Add(textExtraSubscriptions);

            checkAutoFollow = new CheckBox();
            checkAutoFollow.Text = "自动跟随最新报文";
            checkAutoFollow.Location = new Point(16, 128);
            checkAutoFollow.AutoSize = true;
            subGroup.Controls.Add(checkAutoFollow);
        }

        private void BuildDetailsTab(TabPage page)
        {
            SplitContainer split = new SplitContainer();
            split.Dock = DockStyle.Fill;
            split.Orientation = Orientation.Horizontal;
            split.SplitterDistance = 260;
            page.Controls.Add(split);

            TabControl textTabs = new TabControl();
            textTabs.Dock = DockStyle.Fill;
            split.Panel1.Controls.Add(textTabs);

            TabPage meaningPage = new TabPage("数据含义");
            textMeaning = BuildReadOnlyText();
            meaningPage.Controls.Add(textMeaning);
            textTabs.TabPages.Add(meaningPage);

            TabPage rawPage = new TabPage("原始报文");
            textRawPayload = BuildReadOnlyText();
            rawPage.Controls.Add(textRawPayload);
            textTabs.TabPages.Add(rawPage);

            TabPage jsonPage = new TabPage("格式化JSON");
            textFormattedJson = BuildReadOnlyText();
            jsonPage.Controls.Add(textFormattedJson);
            textTabs.TabPages.Add(jsonPage);

            gridFields = new DataGridView();
            gridFields.AllowUserToAddRows = false;
            gridFields.AllowUserToDeleteRows = false;
            gridFields.ReadOnly = true;
            gridFields.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
            gridFields.MultiSelect = false;
            gridFields.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill;
            gridFields.Dock = DockStyle.Fill;
            gridFields.Columns.Add("Path", "字段");
            gridFields.Columns.Add("Value", "值");
            gridFields.Columns.Add("Meaning", "含义");
            split.Panel2.Controls.Add(gridFields);
        }

        private TextBox AddText(Control parent, string label, int labelX, int y, int textX, int width)
        {
            parent.Controls.Add(MakeLabel(label, labelX, y + 3, textX - labelX - 6));
            TextBox text = new TextBox();
            text.Location = new Point(textX, y);
            text.Size = new Size(width, 24);
            text.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            parent.Controls.Add(text);
            return text;
        }

        private static Label MakeLabel(string text, int x, int y, int width)
        {
            Label label = new Label();
            label.Text = text;
            label.Location = new Point(x, y);
            label.Size = new Size(width, 24);
            return label;
        }

        private static TextBox BuildReadOnlyText()
        {
            TextBox text = new TextBox();
            text.Multiline = true;
            text.ReadOnly = true;
            text.ScrollBars = ScrollBars.Both;
            text.WordWrap = false;
            text.Dock = DockStyle.Fill;
            return text;
        }

        private DataGridView BuildGrid()
        {
            DataGridView grid = new DataGridView();
            grid.AllowUserToAddRows = false;
            grid.AllowUserToDeleteRows = false;
            grid.ReadOnly = true;
            grid.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
            grid.MultiSelect = false;
            grid.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill;
            grid.Dock = DockStyle.Fill;
            grid.RowTemplate.Height = 24;
            grid.Columns.Add("Time", "时间");
            grid.Columns.Add("Direction", "方向");
            grid.Columns.Add("TopicKey", "主题类型");
            grid.Columns.Add("Command", "命令/事件");
            grid.Columns.Add("Result", "结果");
            grid.Columns.Add("Summary", "数据含义");
            grid.Columns.Add("Topic", "主题");
            grid.SelectionChanged += GridSelectionChanged;
            return grid;
        }

        private void LoadSettingsToUi()
        {
            textHost.Text = settings.Host;
            textPort.Text = settings.Port.ToString(CultureInfo.InvariantCulture);
            textClientId.Text = settings.ClientId;
            textUsername.Text = settings.Username;
            textPassword.Text = settings.Password;
            textDeviceId.Text = settings.DeviceId;
            textFirmwareVersion.Text = settings.FirmwareVersion;
            numericQos.Value = Math.Max(0, Math.Min(2, settings.Qos));
            numericTimeout.Value = Math.Max(2, Math.Min(30, settings.ConnectTimeoutSeconds));
            checkTls.Checked = settings.UseTls;
            checkAutoFollow.Checked = settings.AutoFollowLatest;
            comboLanguage.SelectedIndexChanged -= comboLanguage_SelectedIndexChanged;
            comboLanguage.Items.Clear();
            comboLanguage.Items.Add(UiText.LanguageDisplay(UiLanguage.Chinese));
            comboLanguage.Items.Add(UiText.LanguageDisplay(UiLanguage.English));
            comboLanguage.SelectedIndex = CurrentLanguage == UiLanguage.English ? 1 : 0;
            comboLanguage.SelectedIndexChanged += comboLanguage_SelectedIndexChanged;
            textTelemetryTopic.Text = settings.Topics.TelemetryUp;
            textCommandTopic.Text = settings.Topics.CommandDown;
            textEventTopic.Text = settings.Topics.EventUp;
            textOtaTopic.Text = settings.Topics.OtaDown;
            textDebugTopic.Text = settings.Topics.DebugUp;
            textStatusTopic.Text = string.IsNullOrWhiteSpace(settings.Topics.Status) ? TopicSet.StatusTopicFor(settings.DeviceId) : settings.Topics.Status;
            textExtraSubscriptions.Text = settings.ExtraSubscriptions;
        }

        private void SaveUiToSettings()
        {
            ushort port;
            settings.Host = textHost.Text.Trim();
            if (ushort.TryParse(textPort.Text.Trim(), out port) && port > 0)
                settings.Port = port;
            settings.ClientId = textClientId.Text.Trim();
            settings.Username = textUsername.Text.Trim();
            settings.Password = textPassword.Text;
            settings.DeviceId = textDeviceId.Text.Trim();
            settings.FirmwareVersion = textFirmwareVersion.Text.Trim();
            settings.Qos = (int)numericQos.Value;
            settings.ConnectTimeoutSeconds = (int)numericTimeout.Value;
            settings.UseTls = checkTls.Checked;
            settings.AutoFollowLatest = checkAutoFollow.Checked;
            settings.Language = comboLanguage.SelectedIndex == 1 ? "en-US" : "zh-CN";
            settings.Topics = CurrentTopics();
            settings.ExtraSubscriptions = textExtraSubscriptions.Text;
            settings.Save();
            emulator = new DeviceProtocolEmulator(settings.Topics, settings.DeviceId, settings.FirmwareVersion);
            ApplyLanguage();
            AppendRawLog("INFO", T("配置已保存到 ", "Configuration saved to ") + DeviceEmulatorSettings.SettingsPath);
        }

        private TopicSet CurrentTopics()
        {
            return new TopicSet
            {
                TelemetryUp = textTelemetryTopic.Text.Trim(),
                CommandDown = textCommandTopic.Text.Trim(),
                EventUp = textEventTopic.Text.Trim(),
                OtaDown = textOtaTopic.Text.Trim(),
                DebugUp = textDebugTopic.Text.Trim(),
                Status = textStatusTopic.Text.Trim()
            };
        }

        private async Task ConnectMqttAsync()
        {
            if (mqttClient != null && mqttClient.IsConnected)
            {
                AppendRawLog("WARN", T("MQTT 已连接", "MQTT already connected"));
                return;
            }

            SaveUiToSettings();
            ushort port;
            if (!ushort.TryParse(textPort.Text.Trim(), out port) || port == 0)
            {
                AppendRawLog("ERROR", "端口格式不正确");
                return;
            }

            MqttFactory factory = new MqttFactory();
            mqttClient = factory.CreateMqttClient();
            mqttClient.ApplicationMessageReceivedAsync += OnMqttMessageReceived;
            mqttClient.DisconnectedAsync += OnMqttDisconnected;

            MqttClientOptionsBuilder builder = new MqttClientOptionsBuilder()
                .WithClientId(string.IsNullOrWhiteSpace(textClientId.Text) ? "device-emulator-" + DateTime.Now.ToString("yyyyMMddHHmmss", CultureInfo.InvariantCulture) : textClientId.Text.Trim())
                .WithTcpServer(textHost.Text.Trim(), port)
                .WithProtocolVersion(MqttProtocolVersion.V500)
                .WithCleanSession();

            if (!string.IsNullOrWhiteSpace(textUsername.Text))
                builder.WithCredentials(textUsername.Text.Trim(), textPassword.Text);
            if (checkTls.Checked)
                builder.WithTlsOptions(options => options.UseTls());
            string statusTopic = CurrentTopics().Status;
            if (!string.IsNullOrWhiteSpace(statusTopic))
            {
                builder.WithWillTopic(statusTopic)
                    .WithWillPayload(BuildStatusPayload("offline", "mqtt_lwt"))
                    .WithWillQualityOfServiceLevel(MqttQualityOfServiceLevel.AtMostOnce)
                    .WithWillRetain(true);
            }

            SetConnectedUi(false, T("连接中...", "Connecting..."));
            CancellationTokenSource timeout = new CancellationTokenSource();
            timeout.CancelAfter(TimeSpan.FromSeconds((double)numericTimeout.Value));
            try
            {
                await mqttClient.ConnectAsync(builder.Build(), timeout.Token);
                SetConnectedUi(true, T("已连接", "Connected"));
                AppendRawLog("INFO", T("MQTT5 已连接 ", "MQTT5 connected ") + textHost.Text.Trim() + ":" + port);
                AppLogger.Info("MQTT5 connected " + textHost.Text.Trim() + ":" + port);
                await SubscribeTopicsAsync();
                await PublishStatusAsync("online", "mqtt_connected");
            }
            catch (Exception ex)
            {
                SetConnectedUi(false, T("连接失败", "Connect failed"));
                AppendRawLog("ERROR", T("MQTT 连接失败: ", "MQTT connect failed: ") + ex.Message);
                AppLogger.Error("MQTT connect failed", ex);
                DisposeMqttClientImmediately();
            }
            finally
            {
                timeout.Dispose();
            }
        }

        private async Task SubscribeTopicsAsync()
        {
            IMqttClient client = mqttClient;
            if (client == null || !client.IsConnected)
                return;

            HashSet<string> topics = new HashSet<string>(StringComparer.Ordinal);
            AddTopic(topics, textCommandTopic.Text);
            AddTopic(topics, textOtaTopic.Text);
            foreach (string line in (textExtraSubscriptions.Text ?? string.Empty).Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries))
                AddTopic(topics, line);

            foreach (string topic in topics)
            {
                MqttClientSubscribeOptions options = new MqttClientSubscribeOptionsBuilder()
                    .WithTopicFilter(filter => filter.WithTopic(topic).WithQualityOfServiceLevel(CurrentQosLevel()))
                    .Build();
                await client.SubscribeAsync(options, CancellationToken.None);
                AppendRawLog("SUB", topic);
                AppLogger.Info("Subscribed " + topic);
            }
        }

        private static void AddTopic(HashSet<string> topics, string topic)
        {
            string value = (topic ?? string.Empty).Trim();
            if (!string.IsNullOrWhiteSpace(value))
                topics.Add(value);
        }

        private async Task PublishStatusAsync(string status, string reason)
        {
            IMqttClient client = mqttClient;
            if (client == null || !client.IsConnected)
                return;

            string topic = textStatusTopic.Text.Trim();
            if (string.IsNullOrWhiteSpace(topic))
                return;

            string payload = BuildStatusPayload(status, reason);
            MqttApplicationMessage message = new MqttApplicationMessageBuilder()
                .WithTopic(topic)
                .WithPayload(payload)
                .WithQualityOfServiceLevel(MqttQualityOfServiceLevel.AtMostOnce)
                .WithRetainFlag(true)
                .Build();
            await client.PublishAsync(message, CancellationToken.None);

            MessageRecord record = PayloadDecoder.Decode(topic, Encoding.UTF8.GetBytes(payload), CurrentTopics());
            record.Direction = "TX";
            AppendRecordFromThread(record, "status retained");
        }

        private string BuildStatusPayload(string status, string reason)
        {
            Dictionary<string, object> payload = new Dictionary<string, object>
            {
                { "schema", "emqx-gateway.status.v1" },
                { "device_id", string.IsNullOrWhiteSpace(textDeviceId.Text) ? settings.DeviceId : textDeviceId.Text.Trim() },
                { "status", status ?? string.Empty },
                { "reason", reason ?? string.Empty },
                { "firmware_version", string.IsNullOrWhiteSpace(textFirmwareVersion.Text) ? settings.FirmwareVersion : textFirmwareVersion.Text.Trim() },
                { "ts", DateTime.Now.ToString("yyyy-MM-ddTHH:mm:ss.fffK", CultureInfo.InvariantCulture) }
            };
            return JsonSerializer.Serialize(payload);
        }

        private async Task DisconnectMqttAsync()
        {
            IMqttClient client = mqttClient;
            mqttClient = null;
            if (client == null)
                return;

            try
            {
                client.ApplicationMessageReceivedAsync -= OnMqttMessageReceived;
                client.DisconnectedAsync -= OnMqttDisconnected;
                if (client.IsConnected)
                {
                    Task task = client.DisconnectAsync();
                    if (await Task.WhenAny(task, Task.Delay(TimeSpan.FromSeconds(2))) == task)
                        await task;
                }
                client.Dispose();
                AppendRawLog("INFO", T("MQTT 已断开", "MQTT disconnected"));
            }
            catch (Exception ex)
            {
                AppLogger.Error("MQTT disconnect failed", ex);
            }
            finally
            {
                SetConnectedUi(false, T("未连接", "Disconnected"));
            }
        }

        private void DisposeMqttClientImmediately()
        {
            IMqttClient client = mqttClient;
            mqttClient = null;
            if (client == null)
                return;
            try
            {
                client.ApplicationMessageReceivedAsync -= OnMqttMessageReceived;
                client.DisconnectedAsync -= OnMqttDisconnected;
                client.Dispose();
            }
            catch (Exception ex)
            {
                AppLogger.Error("MQTT immediate dispose failed", ex);
            }
        }

        private async Task OnMqttMessageReceived(MqttApplicationMessageReceivedEventArgs args)
        {
            ArraySegment<byte> payload = args.ApplicationMessage.PayloadSegment;
            byte[] bytes = new byte[payload.Count];
            if (payload.Array != null && payload.Count > 0)
                Buffer.BlockCopy(payload.Array, payload.Offset, bytes, 0, payload.Count);

            DeviceProcessingResult result = emulator.ProcessIncoming(args.ApplicationMessage.Topic, bytes);
            AppendRecordFromThread(result.Received, "code=" + result.ResponseCode.ToString(CultureInfo.InvariantCulture) + " " + result.ResponseMessage);

            IMqttClient client = mqttClient;
            if (client == null || !client.IsConnected)
                return;

            foreach (OutgoingMessage response in result.Responses)
            {
                MqttApplicationMessage message = new MqttApplicationMessageBuilder()
                    .WithTopic(response.Topic)
                    .WithPayload(response.PayloadText)
                    .WithQualityOfServiceLevel(CurrentQosLevel())
                    .Build();
                await client.PublishAsync(message, CancellationToken.None);
                MessageRecord txRecord = emulator.BuildTxRecord(response);
                AppendRecordFromThread(txRecord, "published");
            }
        }

        private Task OnMqttDisconnected(MqttClientDisconnectedEventArgs args)
        {
            if (!isClosing)
                AppendRawLogFromThread("INFO", T("MQTT 已断开: ", "MQTT disconnected: ") + args.Reason);
            return Task.CompletedTask;
        }

        private MqttQualityOfServiceLevel CurrentQosLevel()
        {
            int qos = (int)numericQos.Value;
            if (qos <= 0)
                return MqttQualityOfServiceLevel.AtMostOnce;
            if (qos == 1)
                return MqttQualityOfServiceLevel.AtLeastOnce;
            return MqttQualityOfServiceLevel.ExactlyOnce;
        }

        private void AppendRecordFromThread(MessageRecord record, string result)
        {
            if (isClosing || IsDisposed)
                return;
            if (InvokeRequired)
            {
                try
                {
                    BeginInvoke(new Action(() => AppendRecord(record, result)));
                }
                catch (InvalidOperationException)
                {
                }
                return;
            }
            AppendRecord(record, result);
        }

        private void AppendRecord(MessageRecord record, string result)
        {
            if (record == null)
                return;
            string logLine = record.Direction + " " + record.Topic + " " + record.RawText;
            AppendRawLog(record.Direction, logLine);
            AppLogger.PacketJsonl(record.Direction, record.Topic, record.TopicKey, record.Command, record.RawText, record.RawHex, result);

            DataGridView grid = SelectGrid(record);
            AddRecordToGrid(grid, record, result);
        }

        private DataGridView SelectGrid(MessageRecord record)
        {
            if (record.TopicKey == "status")
                return gridStatus;
            if (record.Direction == "TX")
                return gridResponses;
            if (record.TopicKey == "ota_down" || record.LegacyFrame != null || (record.Command ?? string.Empty).StartsWith("ota_", StringComparison.Ordinal))
                return gridOta;
            return gridDownlink;
        }

        private void AddRecordToGrid(DataGridView grid, MessageRecord record, string result)
        {
            MessageRecord selectedBefore = GetSelectedRecord(grid);
            int firstDisplayedRow = -1;
            try
            {
                firstDisplayedRow = grid.FirstDisplayedScrollingRowIndex;
            }
            catch (InvalidOperationException)
            {
            }

            bool selectNew = checkAutoFollow.Checked || selectedRecord == null;
            suppressSelectionChanged = true;
            int row = grid.Rows.Add(
                record.Time.ToString("HH:mm:ss", CultureInfo.InvariantCulture),
                record.Direction,
                record.TopicKey,
                string.IsNullOrWhiteSpace(record.Command) && record.LegacyFrame != null ? "legacy_" + record.LegacyFrame.CommandHex : record.Command,
                result,
                record.Summary,
                record.Topic);
            grid.Rows[row].Tag = record;
            while (grid.Rows.Count > MaxRows)
                grid.Rows.RemoveAt(0);

            if (selectNew)
            {
                SelectGridRow(grid, record, true);
                selectedRecord = record;
                ShowDetails(record);
            }
            else
            {
                RestoreSelection(grid, selectedBefore, firstDisplayedRow);
            }
            suppressSelectionChanged = false;
        }

        private void RestoreSelection(DataGridView grid, MessageRecord selectedBefore, int firstDisplayedRow)
        {
            if (selectedBefore != null)
                SelectGridRow(grid, selectedBefore, false);
            if (firstDisplayedRow >= 0 && firstDisplayedRow < grid.Rows.Count)
            {
                try
                {
                    grid.FirstDisplayedScrollingRowIndex = firstDisplayedRow;
                }
                catch (InvalidOperationException)
                {
                }
            }
        }

        private void SelectGridRow(DataGridView grid, MessageRecord record, bool scroll)
        {
            if (grid == null || record == null)
                return;
            for (int i = 0; i < grid.Rows.Count; i++)
            {
                if (!ReferenceEquals(grid.Rows[i].Tag, record))
                    continue;
                grid.ClearSelection();
                grid.Rows[i].Selected = true;
                grid.CurrentCell = grid.Rows[i].Cells[0];
                if (scroll)
                {
                    try
                    {
                        grid.FirstDisplayedScrollingRowIndex = i;
                    }
                    catch (InvalidOperationException)
                    {
                    }
                }
                return;
            }
        }

        private static MessageRecord GetSelectedRecord(DataGridView grid)
        {
            if (grid == null || grid.CurrentRow == null)
                return null;
            return grid.CurrentRow.Tag as MessageRecord;
        }

        private void GridSelectionChanged(object sender, EventArgs e)
        {
            if (suppressSelectionChanged)
                return;
            DataGridView grid = sender as DataGridView;
            MessageRecord record = GetSelectedRecord(grid);
            if (record == null)
                return;
            selectedRecord = record;
            ShowDetails(record);
        }

        private void ShowDetails(MessageRecord record)
        {
            if (record == null)
                return;
            textMeaning.Text = record.Meaning;
            textRawPayload.Text = record.RawText + Environment.NewLine + Environment.NewLine + "HEX:" + Environment.NewLine + record.RawHex;
            textFormattedJson.Text = record.FormattedJson;
            gridFields.Rows.Clear();
            foreach (FieldRow field in record.Fields)
                gridFields.Rows.Add(field.Path, field.Value, field.Meaning);
            if (record.LegacyFrame != null)
            {
                gridFields.Rows.Add("legacy.command", record.LegacyFrame.CommandHex, "旧业务帧命令");
                gridFields.Rows.Add("legacy.payload_hex", record.LegacyFrame.PayloadHex, "旧业务帧载荷");
                gridFields.Rows.Add("legacy.outer_crc_ok", record.LegacyFrame.OuterCrcOk.ToString(CultureInfo.InvariantCulture), "外层CRC");
                gridFields.Rows.Add("legacy.inner_crc_ok", record.LegacyFrame.InnerCrcOk.ToString(CultureInfo.InvariantCulture), "内层/响应CRC");
            }
        }

        private void AppendRawLogFromThread(string level, string message)
        {
            if (isClosing || IsDisposed)
                return;
            if (InvokeRequired)
            {
                try
                {
                    BeginInvoke(new Action(() => AppendRawLog(level, message)));
                }
                catch (InvalidOperationException)
                {
                }
                return;
            }
            AppendRawLog(level, message);
        }

        private void AppendRawLog(string level, string message)
        {
            string line = DateTime.Now.ToString("HH:mm:ss.fff", CultureInfo.InvariantCulture) + " [" + level + "] " + message;
            textRawLog.AppendText(line + Environment.NewLine);
            AppLogger.Info(level + " " + message);
        }

        private void ClearLogs()
        {
            gridDownlink.Rows.Clear();
            gridOta.Rows.Clear();
            gridResponses.Rows.Clear();
            gridStatus.Rows.Clear();
            gridFields.Rows.Clear();
            textRawLog.Clear();
            textMeaning.Clear();
            textRawPayload.Clear();
            textFormattedJson.Clear();
            selectedRecord = null;
        }

        private void SetConnectedUi(bool connected, string status)
        {
            if (InvokeRequired)
            {
                BeginInvoke(new Action(() => SetConnectedUi(connected, status)));
                return;
            }
            labelStatus.Text = UiText.Translate(CurrentLanguage, status);
            labelStatus.ForeColor = connected ? Color.DarkGreen : Color.DarkRed;
            buttonConnect.Enabled = !connected;
            buttonDisconnect.Enabled = connected;
        }
    }
}
