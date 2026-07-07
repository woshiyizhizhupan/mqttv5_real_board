using MQTTnet;
using MQTTnet.Client;
using MQTTnet.Formatter;
using MQTTnet.Protocol;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;

namespace mqttv5_tool
{
    public sealed class MqttClientForm : Form
    {
        private const string PageAll = "all";
        private const string PageTelemetry = "telemetry_up";
        private const string PageCommand = "cmd_down";
        private const string PageEvent = "event_up";
        private const string PageOta = "ota_down";
        private const string PageDebug = "debug_up";
        private const string PageStatus = "status";
        private const int MaxMessageRecords = 1000;

        private static readonly JavaScriptSerializer JsonSerializer = new JavaScriptSerializer();

        private static readonly string[] TopicKeys = new string[]
        {
            "telemetry_up",
            "cmd_down",
            "event_up",
            "ota_down",
            "debug_up"
        };

        private static readonly string[] TopicLabels = new string[]
        {
            "telemetry_up 上行",
            "cmd_down 下行/get",
            "event_up 事件/告警",
            "ota_down OTA",
            "debug_up 调试"
        };

        private readonly HostToolSettings settings;
        private readonly Management selectedDevice;
        private readonly TextBox[] topicTextBoxes = new TextBox[Management.TopicCount];
        private readonly CheckBox[] topicSubscribeChecks = new CheckBox[Management.TopicCount];
        private readonly Button[] topicSendButtons = new Button[Management.TopicCount];
        private readonly Dictionary<string, DataGridView> messageGrids = new Dictionary<string, DataGridView>();
        private readonly List<MqttMessageRecord> messageRecords = new List<MqttMessageRecord>();

        private TextBox textHost;
        private TextBox textPort;
        private TextBox textUsername;
        private TextBox textPassword;
        private NumericUpDown numericQos;
        private NumericUpDown numericConnectTimeoutSeconds;
        private CheckBox checkTls;
        private ComboBox comboPublishTopic;
        private TextBox textPayload;
        private TextBox textLog;
        private TextBox textStatusTopic;
        private CheckBox checkAutoFollowLatest;
        private CheckBox checkStatusSubscribe;
        private Button buttonConnect;
        private Button buttonDisconnect;
        private TabControl tabMain;
        private TabPage tabConnectionSettings;
        private TabPage tabTopicPublish;
        private TabPage tabMessageBrowse;
        private TabPage tabMessageDetails;
        private TabControl tabMessages;
        private TabPage tabOverview;
        private TabPage tabTelemetry;
        private TabPage tabEvent;
        private TabPage tabCommand;
        private TabPage tabOta;
        private TabPage tabDebug;
        private TabPage tabStatus;
        private TabPage tabRawLog;
        private DataGridView dataGridMessages;
        private DataGridView dataGridTelemetry;
        private DataGridView dataGridEvent;
        private DataGridView dataGridCommand;
        private DataGridView dataGridOta;
        private DataGridView dataGridDebug;
        private DataGridView dataGridStatus;
        private DataGridView dataGridFields;
        private TextBox textRawPayload;
        private TextBox textFormattedJson;
        private TextBox textMessageMeaning;
        private IMqttClient mqttClient;
        private MqttMessageRecord selectedMessageRecord;
        private bool suppressGridSelectionChanged;
        private bool isClosing;

        public MqttClientForm(Management selectedDevice, HostToolSettings settings)
        {
            this.selectedDevice = selectedDevice;
            this.settings = settings ?? HostToolSettings.Load();
            InitializeComponent();
            LoadFromSettings();
            if (selectedDevice != null)
                LoadTopicsFromDevice(selectedDevice);
            RefreshPublishTopics();
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

        public void ApplyLanguage(UiLanguage language)
        {
            settings.Language = UiText.Code(language);
            UiText.Apply(this, language);
            RefreshMessageRowsLanguage();
        }

        private void ApplyLanguage()
        {
            ApplyLanguage(CurrentLanguage);
        }

        private void RefreshMessageRowsLanguage()
        {
            foreach (DataGridView grid in messageGrids.Values)
            {
                foreach (DataGridViewRow row in grid.Rows)
                {
                    MqttMessageRecord record = row.Tag as MqttMessageRecord;
                    if (record != null && row.Cells.Count > 2)
                        row.Cells[2].Value = TopicKeyToLabel(record.TopicKey);
                }
            }
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
            Text = "EMQX 客户端";
            Size = new Size(1320, 900);
            MinimumSize = new Size(1120, 760);

            tabMain = new TabControl();
            tabMain.Location = new Point(12, 8);
            tabMain.Size = new Size(1274, 838);
            tabMain.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            Controls.Add(tabMain);

            tabConnectionSettings = new TabPage("连接参数");
            tabTopicPublish = new TabPage("主题/发布");
            tabMessageBrowse = new TabPage("报文浏览");
            tabMessageDetails = new TabPage("报文详情/日志");
            tabMain.TabPages.Add(tabConnectionSettings);
            tabMain.TabPages.Add(tabTopicPublish);
            tabMain.TabPages.Add(tabMessageBrowse);
            tabMain.TabPages.Add(tabMessageDetails);

            GroupBox connectionGroup = new GroupBox();
            connectionGroup.Text = "连接";
            connectionGroup.Location = new Point(12, 12);
            connectionGroup.Size = new Size(1228, 130);
            connectionGroup.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            tabConnectionSettings.Controls.Add(connectionGroup);

            textHost = AddText(connectionGroup, "Host", 16, 28, 96, 260);
            textPort = AddText(connectionGroup, "Port", 390, 28, 450, 90);
            textUsername = AddText(connectionGroup, "Username", 16, 68, 96, 260);
            textPassword = AddText(connectionGroup, "Password", 390, 68, 480, 220);

            connectionGroup.Controls.Add(MakeLabel("QoS", 730, 31, 38));
            numericQos = new NumericUpDown();
            numericQos.Minimum = 0;
            numericQos.Maximum = 2;
            numericQos.Location = new Point(770, 28);
            numericQos.Width = 55;
            connectionGroup.Controls.Add(numericQos);

            connectionGroup.Controls.Add(MakeLabel("连接超时(s)", 850, 31, 90));
            numericConnectTimeoutSeconds = new NumericUpDown();
            numericConnectTimeoutSeconds.Minimum = 2;
            numericConnectTimeoutSeconds.Maximum = 30;
            numericConnectTimeoutSeconds.Location = new Point(945, 28);
            numericConnectTimeoutSeconds.Width = 60;
            connectionGroup.Controls.Add(numericConnectTimeoutSeconds);

            checkTls = new CheckBox();
            checkTls.Text = "TLS";
            checkTls.Location = new Point(1030, 30);
            checkTls.AutoSize = true;
            connectionGroup.Controls.Add(checkTls);

            buttonConnect = new Button();
            buttonConnect.Text = "连接";
            buttonConnect.Location = new Point(730, 68);
            buttonConnect.Size = new Size(85, 30);
            buttonConnect.Click += async (sender, args) => await ConnectMqttAsync();
            connectionGroup.Controls.Add(buttonConnect);

            buttonDisconnect = new Button();
            buttonDisconnect.Text = "断开";
            buttonDisconnect.Location = new Point(825, 68);
            buttonDisconnect.Size = new Size(85, 30);
            buttonDisconnect.Click += async (sender, args) => await DisconnectMqttAsync();
            connectionGroup.Controls.Add(buttonDisconnect);

            Button buttonSave = new Button();
            buttonSave.Text = "保存配置";
            buttonSave.Location = new Point(920, 68);
            buttonSave.Size = new Size(95, 30);
            buttonSave.Click += (sender, args) => SaveCurrentToSettings();
            connectionGroup.Controls.Add(buttonSave);

            GroupBox topicGroup = new GroupBox();
            topicGroup.Text = "业务主题与状态主题";
            topicGroup.Location = new Point(12, 12);
            topicGroup.Size = new Size(1228, 286);
            topicGroup.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            tabTopicPublish.Controls.Add(topicGroup);

            Label note = new Label();
            note.Text = "订阅候选来自五个完整业务主题和状态主题，主题可逐项编辑；MQTT 标准客户端不能直接枚举服务端全部历史 topic。";
            note.Location = new Point(16, 24);
            note.AutoSize = true;
            topicGroup.Controls.Add(note);

            BuildEditableTopicRows(topicGroup);
            BuildStatusTopicRow(topicGroup);

            Button buttonLoadDevice = new Button();
            buttonLoadDevice.Text = "从当前设备载入";
            buttonLoadDevice.Location = new Point(16, 246);
            buttonLoadDevice.Size = new Size(120, 28);
            buttonLoadDevice.Enabled = selectedDevice != null;
            buttonLoadDevice.Click += (sender, args) => LoadTopicsFromDevice(selectedDevice);
            topicGroup.Controls.Add(buttonLoadDevice);

            Button buttonRefreshTopics = new Button();
            buttonRefreshTopics.Text = "刷新发布列表";
            buttonRefreshTopics.Location = new Point(146, 246);
            buttonRefreshTopics.Size = new Size(110, 28);
            buttonRefreshTopics.Click += (sender, args) => RefreshPublishTopics();
            topicGroup.Controls.Add(buttonRefreshTopics);

            GroupBox publishGroup = new GroupBox();
            publishGroup.Text = "发布";
            publishGroup.Location = new Point(12, 310);
            publishGroup.Size = new Size(1228, 170);
            publishGroup.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            tabTopicPublish.Controls.Add(publishGroup);

            comboPublishTopic = new ComboBox();
            comboPublishTopic.DropDownStyle = ComboBoxStyle.DropDownList;
            comboPublishTopic.Location = new Point(16, 28);
            comboPublishTopic.Size = new Size(760, 25);
            publishGroup.Controls.Add(comboPublishTopic);

            Button buttonPublish = new Button();
            buttonPublish.Text = "发布";
            buttonPublish.Location = new Point(790, 26);
            buttonPublish.Size = new Size(85, 30);
            buttonPublish.Click += async (sender, args) => await PublishAsync();
            publishGroup.Controls.Add(buttonPublish);

            AddPresetButton(publishGroup, "ping", 890, 26, "{\"id\":\"gui-ping\",\"cmd\":\"ping\"}", 1);
            AddPresetButton(publishGroup, "get_status", 980, 26, "{\"id\":\"gui-status\",\"cmd\":\"get_status\"}", 1);
            AddPresetButton(publishGroup, "ota_status", 1080, 26, "{\"id\":\"gui-ota-status\",\"cmd\":\"ota_status\"}", 3);

            textPayload = new TextBox();
            textPayload.Multiline = true;
            textPayload.ScrollBars = ScrollBars.Vertical;
            textPayload.Location = new Point(16, 62);
            textPayload.Size = new Size(1192, 90);
            textPayload.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            textPayload.Text = "{\"id\":\"gui-ping\",\"cmd\":\"ping\"}";
            publishGroup.Controls.Add(textPayload);

            Panel browseToolbar = new Panel();
            browseToolbar.Dock = DockStyle.Top;
            browseToolbar.Height = 36;
            tabMessageBrowse.Controls.Add(browseToolbar);

            checkAutoFollowLatest = new CheckBox();
            checkAutoFollowLatest.Text = "自动跟随最新报文";
            checkAutoFollowLatest.Checked = false;
            checkAutoFollowLatest.AutoSize = true;
            checkAutoFollowLatest.Location = new Point(12, 9);
            browseToolbar.Controls.Add(checkAutoFollowLatest);

            Button buttonSelectLatest = new Button();
            buttonSelectLatest.Text = "跳到最新";
            buttonSelectLatest.Location = new Point(150, 5);
            buttonSelectLatest.Size = new Size(82, 26);
            buttonSelectLatest.Click += (sender, args) => SelectLatestRecord();
            browseToolbar.Controls.Add(buttonSelectLatest);

            tabMessages = new TabControl();
            tabMessages.Dock = DockStyle.Fill;
            tabMessageBrowse.Controls.Add(tabMessages);
            browseToolbar.BringToFront();

            tabOverview = AddMessageTab("总览", PageAll, out dataGridMessages);
            tabTelemetry = AddMessageTab("上行遥测", PageTelemetry, out dataGridTelemetry);
            tabEvent = AddMessageTab("事件告警", PageEvent, out dataGridEvent);
            tabCommand = AddMessageTab("下行/get", PageCommand, out dataGridCommand);
            tabOta = AddMessageTab("OTA", PageOta, out dataGridOta);
            tabDebug = AddMessageTab("调试/兼容", PageDebug, out dataGridDebug);
            tabStatus = AddMessageTab("状态/LWT", PageStatus, out dataGridStatus);
            GroupBox detailGroup = new GroupBox();
            detailGroup.Text = "选中报文详情";
            detailGroup.Location = new Point(12, 12);
            detailGroup.Size = new Size(1228, 760);
            detailGroup.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            tabMessageDetails.Controls.Add(detailGroup);

            TabControl tabDetails = new TabControl();
            tabDetails.Dock = DockStyle.Fill;
            detailGroup.Controls.Add(tabDetails);

            TabPage tabMeaning = new TabPage("数据含义");
            textMessageMeaning = BuildReadOnlyMultilineTextBox();
            tabMeaning.Controls.Add(textMessageMeaning);
            tabDetails.TabPages.Add(tabMeaning);

            TabPage tabRaw = new TabPage("原始报文");
            textRawPayload = BuildReadOnlyMultilineTextBox();
            tabRaw.Controls.Add(textRawPayload);
            tabDetails.TabPages.Add(tabRaw);

            TabPage tabJson = new TabPage("格式化 JSON");
            textFormattedJson = BuildReadOnlyMultilineTextBox();
            tabJson.Controls.Add(textFormattedJson);
            tabDetails.TabPages.Add(tabJson);

            TabPage tabFields = new TabPage("字段解释");
            dataGridFields = new DataGridView();
            dataGridFields.AllowUserToAddRows = false;
            dataGridFields.AllowUserToDeleteRows = false;
            dataGridFields.ReadOnly = true;
            dataGridFields.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
            dataGridFields.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill;
            dataGridFields.Dock = DockStyle.Fill;
            dataGridFields.Columns.Add("Path", "字段");
            dataGridFields.Columns.Add("Value", "值");
            dataGridFields.Columns.Add("Meaning", "含义");
            tabFields.Controls.Add(dataGridFields);
            tabDetails.TabPages.Add(tabFields);

            tabRawLog = new TabPage("原始日志");
            textLog = new TextBox();
            textLog.Multiline = true;
            textLog.ReadOnly = true;
            textLog.ScrollBars = ScrollBars.Vertical;
            textLog.Dock = DockStyle.Fill;
            tabRawLog.Controls.Add(textLog);
            tabDetails.TabPages.Add(tabRawLog);
        }

        private void BuildEditableTopicRows(Control parent)
        {
            for (int i = 0; i < Management.TopicCount; i++)
            {
                int y = 48 + i * 28;
                topicSubscribeChecks[i] = new CheckBox();
                topicSubscribeChecks[i].Text = "订阅";
                topicSubscribeChecks[i].Location = new Point(16, y);
                topicSubscribeChecks[i].AutoSize = true;
                parent.Controls.Add(topicSubscribeChecks[i]);

                Label label = MakeLabel(TopicLabels[i], 90, y + 3, 150);
                parent.Controls.Add(label);

                topicTextBoxes[i] = new TextBox();
                topicTextBoxes[i].Location = new Point(240, y);
                topicTextBoxes[i].Size = new Size(820, 23);
                topicTextBoxes[i].Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
                topicTextBoxes[i].TextChanged += (sender, args) => RefreshPublishTopics();
                parent.Controls.Add(topicTextBoxes[i]);

                int topicIndex = i;
                topicSendButtons[i] = new Button();
                topicSendButtons[i].Text = "发送";
                topicSendButtons[i].Tag = "发送到此主题";
                topicSendButtons[i].AccessibleName = "发送到此主题";
                topicSendButtons[i].Location = new Point(1070, y - 1);
                topicSendButtons[i].Size = new Size(75, 25);
                topicSendButtons[i].Anchor = AnchorStyles.Top | AnchorStyles.Right;
                topicSendButtons[i].Click += async (sender, args) => await PublishTopicAsync(topicIndex);
                parent.Controls.Add(topicSendButtons[i]);
            }
        }

        private void BuildStatusTopicRow(Control parent)
        {
            int y = 198;
            checkStatusSubscribe = new CheckBox();
            checkStatusSubscribe.Text = "订阅";
            checkStatusSubscribe.Location = new Point(16, y);
            checkStatusSubscribe.AutoSize = true;
            parent.Controls.Add(checkStatusSubscribe);

            Label label = MakeLabel("status 状态/LWT", 90, y + 3, 150);
            parent.Controls.Add(label);

            textStatusTopic = new TextBox();
            textStatusTopic.Location = new Point(240, y);
            textStatusTopic.Size = new Size(820, 23);
            textStatusTopic.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            textStatusTopic.TextChanged += (sender, args) => RefreshPublishTopics();
            parent.Controls.Add(textStatusTopic);

            Button buttonStatusSend = new Button();
            buttonStatusSend.Text = "发送";
            buttonStatusSend.Tag = "发送到此主题";
            buttonStatusSend.AccessibleName = "发送到此主题";
            buttonStatusSend.Location = new Point(1070, y - 1);
            buttonStatusSend.Size = new Size(75, 25);
            buttonStatusSend.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            buttonStatusSend.Click += async (sender, args) => await PublishTopicAsync(textStatusTopic.Text.Trim());
            parent.Controls.Add(buttonStatusSend);
        }

        private TabPage AddMessageTab(string title, string pageKey, out DataGridView grid)
        {
            TabPage page = new TabPage(title);
            grid = BuildMessageGrid();
            page.Controls.Add(grid);
            tabMessages.TabPages.Add(page);
            messageGrids[pageKey] = grid;
            return page;
        }

        private DataGridView BuildMessageGrid()
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
            grid.Columns.Add("Name", "命令/事件");
            grid.Columns.Add("Result", "结果");
            grid.Columns.Add("Summary", "数据含义");
            grid.Columns.Add("Topic", "主题");
            grid.SelectionChanged += DataGridMessages_SelectionChanged;
            return grid;
        }

        private static TextBox BuildReadOnlyMultilineTextBox()
        {
            TextBox textBox = new TextBox();
            textBox.Multiline = true;
            textBox.ReadOnly = true;
            textBox.ScrollBars = ScrollBars.Vertical;
            textBox.Dock = DockStyle.Fill;
            return textBox;
        }

        private void LoadFromSettings()
        {
            textHost.Text = settings.MqttHost;
            textPort.Text = settings.MqttPort.ToString(CultureInfo.InvariantCulture);
            textUsername.Text = settings.MqttUsername;
            textPassword.Text = settings.MqttPassword;
            numericQos.Value = Math.Max(0, Math.Min(2, settings.MqttQos));
            numericConnectTimeoutSeconds.Value = Math.Max(2, Math.Min(30, settings.MqttConnectTimeoutSeconds <= 0 ? 6 : settings.MqttConnectTimeoutSeconds));
            checkTls.Checked = settings.MqttUseTls;
            textStatusTopic.Text = string.IsNullOrWhiteSpace(settings.MqttStatusTopic) ? "v1/devices/status/+" : settings.MqttStatusTopic;
            checkStatusSubscribe.Checked = settings.MqttStatusSubscribe;
            for (int i = 0; i < Management.TopicCount; i++)
            {
                topicTextBoxes[i].Text = settings.MqttTopics[i];
                topicSubscribeChecks[i].Checked = settings.MqttSubscribe[i];
            }
        }

        private void LoadTopicsFromDevice(Management device)
        {
            if (device == null)
                return;

            textHost.Text = device.Host;
            textPort.Text = device.Port.ToString(CultureInfo.InvariantCulture);
            textUsername.Text = device.Username;
            textPassword.Text = device.Password;
            if (device.Qos != null && device.Qos.Length > 0)
            {
                int qos = Math.Max(0, Math.Min(2, (int)device.Qos[0]));
                numericQos.Value = qos;
            }
            checkTls.Checked = device.TlsMode != 0;
            for (int i = 0; i < Management.TopicCount; i++)
                topicTextBoxes[i].Text = device.Topics[i];
            textStatusTopic.Text = "v1/devices/status/+";
            checkStatusSubscribe.Checked = true;
            RefreshPublishTopics();
            AppendLog("INFO", "已从当前设备载入 MQTT 参数、五个完整主题和状态订阅主题");
        }

        private void SaveCurrentToSettings()
        {
            ushort port;
            settings.MqttHost = textHost.Text.Trim();
            if (ushort.TryParse(textPort.Text.Trim(), out port) && port > 0)
                settings.MqttPort = port;
            settings.MqttUsername = textUsername.Text.Trim();
            settings.MqttPassword = textPassword.Text;
            settings.MqttQos = (int)numericQos.Value;
            settings.MqttUseTls = checkTls.Checked;
            settings.MqttConnectTimeoutSeconds = (int)numericConnectTimeoutSeconds.Value;
            settings.MqttStatusTopic = textStatusTopic.Text.Trim();
            settings.MqttStatusSubscribe = checkStatusSubscribe.Checked;
            for (int i = 0; i < Management.TopicCount; i++)
            {
                settings.MqttTopics[i] = topicTextBoxes[i].Text.Trim();
                settings.MqttSubscribe[i] = topicSubscribeChecks[i].Checked;
            }
            settings.Save();
            AppendLog("INFO", "已保存到 " + HostToolSettings.SettingsPath);
        }

        private async Task ConnectMqttAsync()
        {
            if (mqttClient != null && mqttClient.IsConnected)
            {
                AppendLog("WARN", "MQTT 已连接");
                return;
            }

            RefreshPublishTopics();
            SaveCurrentToSettings();
            ushort port;
            if (!ushort.TryParse(textPort.Text.Trim(), out port) || port == 0)
            {
                AppendLog("ERROR", "端口格式不正确");
                return;
            }

            MqttFactory factory = new MqttFactory();
            mqttClient = factory.CreateMqttClient();
            mqttClient.ApplicationMessageReceivedAsync += OnMqttMessageReceived;
            mqttClient.DisconnectedAsync += OnMqttDisconnected;

            MqttClientOptionsBuilder builder = new MqttClientOptionsBuilder()
                .WithClientId("pc-host-tool-" + DateTime.Now.ToString("yyyyMMddHHmmss", CultureInfo.InvariantCulture))
                .WithTcpServer(textHost.Text.Trim(), port)
                .WithProtocolVersion(MqttProtocolVersion.V500)
                .WithCleanSession();

            if (!string.IsNullOrWhiteSpace(textUsername.Text))
                builder.WithCredentials(textUsername.Text.Trim(), textPassword.Text);
            if (checkTls.Checked)
                builder.WithTlsOptions(options => options.UseTls());

            CancellationTokenSource timeout = new CancellationTokenSource();
            timeout.CancelAfter(TimeSpan.FromSeconds((double)numericConnectTimeoutSeconds.Value));
            try
            {
                await mqttClient.ConnectAsync(builder.Build(), timeout.Token);
                AppendLog("INFO", "MQTT5 已连接 " + textHost.Text.Trim() + ":" + port);
                await SubscribeSelectedTopicsAsync();
            }
            catch (Exception ex)
            {
                AppendLog("ERROR", "MQTT 连接失败: " + ex.Message);
                AppLogger.Error("MQTT client connect failed", ex);
                if (mqttClient != null)
                {
                    mqttClient.Dispose();
                    mqttClient = null;
                }
            }
            finally
            {
                timeout.Dispose();
            }
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
                    Task disconnectTask = client.DisconnectAsync();
                    if (await Task.WhenAny(disconnectTask, Task.Delay(TimeSpan.FromSeconds(2))) == disconnectTask)
                        await disconnectTask;
                    else
                    {
                        AppLogger.Warn("MQTT client disconnect timeout; disposing client.");
                        _ = disconnectTask.ContinueWith(task => AppLogger.Error("MQTT client delayed disconnect failed", task.Exception), TaskContinuationOptions.OnlyOnFaulted);
                    }
                }
                client.Dispose();
            }
            catch (Exception ex)
            {
                AppLogger.Error("MQTT client disconnect failed", ex);
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
                AppLogger.Error("MQTT client immediate dispose failed", ex);
            }
        }

        private async Task SubscribeSelectedTopicsAsync()
        {
            if (mqttClient == null || !mqttClient.IsConnected)
                return;

            for (int i = 0; i < Management.TopicCount; i++)
            {
                if (!topicSubscribeChecks[i].Checked)
                    continue;
                string topic = topicTextBoxes[i].Text.Trim();
                if (string.IsNullOrWhiteSpace(topic))
                    continue;

                MqttClientSubscribeOptions options = new MqttClientSubscribeOptionsBuilder()
                    .WithTopicFilter(filter => filter
                        .WithTopic(topic)
                        .WithQualityOfServiceLevel(CurrentQosLevel()))
                    .Build();
                await mqttClient.SubscribeAsync(options, CancellationToken.None);
                AppendLog("SUB", TopicKeys[i] + " " + topic);
            }

            if (checkStatusSubscribe != null && checkStatusSubscribe.Checked)
            {
                string statusTopic = textStatusTopic.Text.Trim();
                if (!string.IsNullOrWhiteSpace(statusTopic))
                {
                    MqttClientSubscribeOptions options = new MqttClientSubscribeOptionsBuilder()
                        .WithTopicFilter(filter => filter
                            .WithTopic(statusTopic)
                            .WithQualityOfServiceLevel(CurrentQosLevel()))
                        .Build();
                    await mqttClient.SubscribeAsync(options, CancellationToken.None);
                    AppendLog("SUB", PageStatus + " " + statusTopic);
                }
            }
        }

        private async Task PublishAsync()
        {
            await PublishTopicAsync(comboPublishTopic.Text.Trim());
        }

        private async Task PublishTopicAsync(int topicIndex)
        {
            if (topicIndex < 0 || topicIndex >= topicTextBoxes.Length)
                return;
            string topic = topicTextBoxes[topicIndex].Text.Trim();
            if (comboPublishTopic.Items.Contains(topic))
                comboPublishTopic.SelectedItem = topic;
            await PublishTopicAsync(topic);
        }

        private async Task PublishTopicAsync(string topic)
        {
            if (mqttClient == null || !mqttClient.IsConnected)
            {
                AppendLog("ERROR", "尚未连接 MQTT");
                return;
            }

            string payload = textPayload.Text.Trim();
            if (string.IsNullOrWhiteSpace(topic))
            {
                AppendLog("ERROR", "请选择发布主题");
                return;
            }
            if (IsTopicFilter(topic))
            {
                AppendLog("ERROR", "不能向包含 + 或 # 的订阅通配符主题发布，请改为具体设备主题");
                return;
            }

            MqttApplicationMessage message = new MqttApplicationMessageBuilder()
                .WithTopic(topic)
                .WithPayload(payload)
                .WithQualityOfServiceLevel(CurrentQosLevel())
                .Build();
            await mqttClient.PublishAsync(message, CancellationToken.None);
            AppendLog("TX", topic + " " + payload);
            AppendMessageRecord(BuildMessageRecord("TX", topic, payload));
        }

        private Task OnMqttMessageReceived(MqttApplicationMessageReceivedEventArgs args)
        {
            ArraySegment<byte> payload = args.ApplicationMessage.PayloadSegment;
            string text = payload.Array == null ? string.Empty : Encoding.UTF8.GetString(payload.Array, payload.Offset, payload.Count);
            AppendMqttMessageFromThread("RX", args.ApplicationMessage.Topic, text);
            return Task.CompletedTask;
        }

        private Task OnMqttDisconnected(MqttClientDisconnectedEventArgs args)
        {
            AppendLogFromThread("INFO", "MQTT 已断开: " + args.Reason);
            return Task.CompletedTask;
        }

        private void AppendMqttMessageFromThread(string direction, string topic, string payload)
        {
            if (isClosing || IsDisposed)
                return;
            if (InvokeRequired)
            {
                try
                {
                    BeginInvoke(new Action(() => AppendMqttMessage(direction, topic, payload)));
                }
                catch (InvalidOperationException)
                {
                }
                return;
            }
            AppendMqttMessage(direction, topic, payload);
        }

        private void AppendMqttMessage(string direction, string topic, string payload)
        {
            AppendLog(direction, topic + " " + payload);
            AppendMessageRecord(BuildMessageRecord(direction, topic, payload));
        }

        private MqttMessageRecord BuildMessageRecord(string direction, string topic, string payload)
        {
            IDictionary<string, object> json = TryParseJsonObject(payload);
            string topicKey = ClassifyTopic(topic);
            string schema = json == null ? string.Empty : GetAnyString(json, "schema");
            string command = json == null ? string.Empty : GetAnyString(json, "cmd", "command", "data.cmd");
            string eventName = json == null ? string.Empty : GetAnyString(json, "event", "event_name", "data.event");
            string ok = json == null ? string.Empty : GetAnyString(json, "ok", "data.ok");
            string status = json == null ? string.Empty : GetAnyString(json, "status", "data.status");

            MqttMessageRecord record = new MqttMessageRecord();
            record.Time = DateTime.Now;
            record.Direction = direction;
            record.Topic = topic ?? string.Empty;
            record.TopicKey = topicKey;
            record.Schema = schema;
            record.Command = command;
            record.EventName = eventName;
            record.StatusText = status;
            record.OkText = ok;
            record.RawPayload = payload ?? string.Empty;
            record.FormattedJson = FormatJson(record.RawPayload);
            record.Fields = BuildFieldRows(json);
            record.PageKey = SelectPageKey(direction, topicKey, schema);
            record.Meaning = ExplainPayload(record, json);
            record.Summary = BuildSummary(record);
            return record;
        }

        private void AppendMessageRecord(MqttMessageRecord record)
        {
            if (record == null)
                return;

            bool shouldSelectNew = ShouldAutoFollowLatest() || selectedMessageRecord == null;
            messageRecords.Add(record);
            while (messageRecords.Count > MaxMessageRecords)
                messageRecords.RemoveAt(0);

            AddRecordToGrid(dataGridMessages, record, shouldSelectNew);
            DataGridView categoryGrid;
            if (messageGrids.TryGetValue(record.PageKey, out categoryGrid) && categoryGrid != dataGridMessages)
                AddRecordToGrid(categoryGrid, record, false);
            if (shouldSelectNew)
            {
                selectedMessageRecord = record;
                ShowMessageDetails(record);
            }
        }

        private void AddRecordToGrid(DataGridView grid, MqttMessageRecord record, bool selectNew)
        {
            MqttMessageRecord selectedBefore = GetSelectedRecord(grid);
            int firstDisplayedRow = -1;
            try
            {
                firstDisplayedRow = grid.FirstDisplayedScrollingRowIndex;
            }
            catch (InvalidOperationException)
            {
            }

            suppressGridSelectionChanged = true;
            int row = grid.Rows.Add(
                record.Time.ToString("HH:mm:ss", CultureInfo.InvariantCulture),
                record.Direction,
                TopicKeyToLabel(record.TopicKey),
                record.DisplayName,
                record.OkText,
                record.Summary,
                record.Topic);
            grid.Rows[row].Tag = record;

            while (grid.Rows.Count > MaxMessageRecords)
                grid.Rows.RemoveAt(0);

            if (selectNew)
            {
                SelectGridRow(grid, record, true);
            }
            else
            {
                RestoreGridSelection(grid, selectedBefore, firstDisplayedRow);
            }
            suppressGridSelectionChanged = false;
        }

        private bool ShouldAutoFollowLatest()
        {
            return checkAutoFollowLatest != null && checkAutoFollowLatest.Checked;
        }

        private void SelectLatestRecord()
        {
            if (messageRecords.Count == 0)
                return;

            MqttMessageRecord latest = messageRecords[messageRecords.Count - 1];
            selectedMessageRecord = latest;
            SelectGridRow(dataGridMessages, latest, true);
            ShowMessageDetails(latest);
            tabMessages.SelectedTab = tabOverview;
        }

        private static MqttMessageRecord GetSelectedRecord(DataGridView grid)
        {
            if (grid == null || grid.CurrentRow == null)
                return null;
            return grid.CurrentRow.Tag as MqttMessageRecord;
        }

        private void RestoreGridSelection(DataGridView grid, MqttMessageRecord selectedBefore, int firstDisplayedRow)
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

        private void SelectGridRow(DataGridView grid, MqttMessageRecord record, bool scrollToRow)
        {
            if (grid == null || record == null)
                return;

            for (int i = 0; i < grid.Rows.Count; i++)
            {
                if (!ReferenceEquals(grid.Rows[i].Tag, record))
                    continue;

                bool oldSuppress = suppressGridSelectionChanged;
                suppressGridSelectionChanged = true;
                grid.ClearSelection();
                grid.Rows[i].Selected = true;
                grid.CurrentCell = grid.Rows[i].Cells[0];
                if (scrollToRow)
                {
                    try
                    {
                        grid.FirstDisplayedScrollingRowIndex = i;
                    }
                    catch (InvalidOperationException)
                    {
                    }
                }
                suppressGridSelectionChanged = oldSuppress;
                return;
            }
        }

        private void DataGridMessages_SelectionChanged(object sender, EventArgs e)
        {
            if (suppressGridSelectionChanged)
                return;
            DataGridView grid = sender as DataGridView;
            if (grid == null || grid.CurrentRow == null)
                return;
            MqttMessageRecord record = grid.CurrentRow.Tag as MqttMessageRecord;
            if (record != null)
            {
                selectedMessageRecord = record;
                ShowMessageDetails(record);
            }
        }

        private void ShowMessageDetails(MqttMessageRecord record)
        {
            if (record == null)
            {
                textMessageMeaning.Text = string.Empty;
                textRawPayload.Text = string.Empty;
                textFormattedJson.Text = string.Empty;
                dataGridFields.Rows.Clear();
                return;
            }

            textMessageMeaning.Text = record.Meaning;
            textRawPayload.Text = record.RawPayload;
            textFormattedJson.Text = record.FormattedJson;
            dataGridFields.Rows.Clear();
            foreach (FieldRow field in record.Fields)
                dataGridFields.Rows.Add(field.Path, field.Value, field.Meaning);
        }

        private string ExplainPayload(MqttMessageRecord record, IDictionary<string, object> json)
        {
            if (record.Direction == "TX")
                return ExplainTx(record, json);

            string schema = (record.Schema ?? string.Empty).ToLowerInvariant();
            if (schema.Contains("status") || record.PageKey == PageStatus)
            {
                string deviceId = GetAnyString(json, "device_id", "data.device_id");
                string reason = GetAnyString(json, "reason", "data.reason");
                return "设备状态：online 表示设备连接成功后主动上报，offline 通常由 EMQX 根据设备遗嘱消息代发。设备=" + BlankAsDash(deviceId) +
                    "，状态=" + BlankAsDash(record.StatusText) +
                    "，原因=" + BlankAsDash(reason) + "。";
            }

            if (schema.Contains("telemetry") || record.PageKey == PageTelemetry)
            {
                string deviceId = GetAnyString(json, "device_id", "data.device_id");
                string voltage = GetAnyString(json, "meter_hlw8112.voltage_mv", "data.meter_hlw8112.voltage_mv");
                string temperature = GetAnyString(json, "environment.temperature_c_x10", "data.environment.temperature_c_x10");
                string rs485Online = GetAnyString(json, "rs485.online", "data.rs485.online");
                return "上行遥测：设备周期上报运行数据。设备=" + BlankAsDash(deviceId) +
                    "，电表电压(mV)=" + BlankAsDash(voltage) +
                    "，环境温度(x0.1C)=" + BlankAsDash(temperature) +
                    "，RS485在线=" + BlankAsDash(rs485Online) + "。";
            }

            if (schema.Contains("event") || record.PageKey == PageEvent)
            {
                string level = GetAnyString(json, "level", "data.level");
                string message = GetAnyString(json, "message", "data.message");
                return "事件告警：设备主动上报异常、恢复或状态变化。事件=" + BlankAsDash(record.EventName) +
                    "，级别=" + BlankAsDash(level) +
                    "，说明=" + BlankAsDash(message) + "。";
            }

            if (schema.Contains("response") || record.PageKey == PageCommand)
            {
                string code = GetAnyString(json, "code", "data.code");
                string message = GetAnyString(json, "message", "data.message");
                return "下行/get响应：设备对服务器或上位机下发命令的执行反馈。命令=" + BlankAsDash(record.Command) +
                    "，成功=" + BlankAsDash(record.OkText) +
                    "，代码=" + BlankAsDash(code) +
                    "，说明=" + BlankAsDash(message) + "。";
            }

            if (schema.Contains("ota") || record.PageKey == PageOta)
                return "OTA报文：用于固件升级查询、下载、校验或切换状态跟踪。请结合字段解释查看版本、URL、大小、校验值和状态。";

            if (record.PageKey == PageDebug)
                return "调试/兼容报文：用于旧业务十六进制负载、兼容透传或临时调试数据观察。";

            return "MQTT报文：当前主题或schema未命中内置分类，可通过原始报文和字段解释确认业务含义。";
        }

        private string ExplainTx(MqttMessageRecord record, IDictionary<string, object> json)
        {
            string command = record.Command;
            if (string.IsNullOrWhiteSpace(command) && json != null)
                command = GetAnyString(json, "cmd", "command");

            if (record.PageKey == PageCommand)
                return "下行/get：向设备发送控制或查询命令。命令=" + BlankAsDash(command) + "，主题=" + record.Topic + "。";
            if (record.PageKey == PageOta)
                return "OTA下行：向设备发送升级查询、下载、校验或切换命令。命令=" + BlankAsDash(command) + "，主题=" + record.Topic + "。";
            if (record.PageKey == PageTelemetry)
                return "上行主题发布：当前是测试客户端向上行主题发布载荷，通常用于服务端接收链路验证。";
            if (record.PageKey == PageEvent)
                return "事件主题发布：当前是测试客户端向事件/告警主题发布载荷，通常用于服务端规则或告警链路验证。";
            if (record.PageKey == PageDebug)
                return "调试/兼容发布：用于兼容透传或临时调试。";
            if (record.PageKey == PageStatus)
                return "状态主题发布：用于测试 online/offline 状态报文。生产设备通常由设备自动发布 online，由 EMQX LWT 发布 offline。";
            return "MQTT发布：已向选中主题发送当前载荷。";
        }

        private static List<FieldRow> BuildFieldRows(IDictionary<string, object> json)
        {
            var rows = new List<FieldRow>();
            if (json == null)
                return rows;
            AppendFieldRows(rows, string.Empty, json);
            return rows;
        }

        private static void AppendFieldRows(List<FieldRow> rows, string path, object value)
        {
            IDictionary<string, object> dict = value as IDictionary<string, object>;
            if (dict != null)
            {
                foreach (KeyValuePair<string, object> item in dict)
                {
                    string childPath = string.IsNullOrEmpty(path) ? item.Key : path + "." + item.Key;
                    AppendFieldRows(rows, childPath, item.Value);
                }
                return;
            }

            IEnumerable list = value as IEnumerable;
            if (list != null && !(value is string))
            {
                int index = 0;
                foreach (object item in list)
                {
                    AppendFieldRows(rows, path + "[" + index.ToString(CultureInfo.InvariantCulture) + "]", item);
                    index++;
                }
                return;
            }

            string fieldPath = string.IsNullOrEmpty(path) ? "$" : path;
            rows.Add(new FieldRow(fieldPath, ScalarToString(value), DescribeField(fieldPath)));
        }

        private static string FormatJson(string payload)
        {
            object value = TryParseJsonValue(payload);
            if (value == null)
                return payload ?? string.Empty;

            StringBuilder builder = new StringBuilder();
            WriteJsonValue(builder, value, 0);
            return builder.ToString();
        }

        private static void WriteJsonValue(StringBuilder builder, object value, int indent)
        {
            IDictionary<string, object> dict = value as IDictionary<string, object>;
            if (dict != null)
            {
                builder.AppendLine("{");
                int index = 0;
                foreach (KeyValuePair<string, object> item in dict)
                {
                    AppendIndent(builder, indent + 1);
                    builder.Append(JsonSerializer.Serialize(item.Key));
                    builder.Append(": ");
                    WriteJsonValue(builder, item.Value, indent + 1);
                    index++;
                    if (index < dict.Count)
                        builder.Append(",");
                    builder.AppendLine();
                }
                AppendIndent(builder, indent);
                builder.Append("}");
                return;
            }

            IEnumerable list = value as IEnumerable;
            if (list != null && !(value is string))
            {
                var items = new List<object>();
                foreach (object item in list)
                    items.Add(item);
                builder.AppendLine("[");
                for (int i = 0; i < items.Count; i++)
                {
                    AppendIndent(builder, indent + 1);
                    WriteJsonValue(builder, items[i], indent + 1);
                    if (i + 1 < items.Count)
                        builder.Append(",");
                    builder.AppendLine();
                }
                AppendIndent(builder, indent);
                builder.Append("]");
                return;
            }

            builder.Append(JsonSerializer.Serialize(value));
        }

        private static void AppendIndent(StringBuilder builder, int indent)
        {
            builder.Append(' ', indent * 2);
        }

        private static string DescribeField(string path)
        {
            string key = (path ?? string.Empty).ToLowerInvariant();
            if (key == "schema")
                return "协议/报文类型，用于区分遥测、事件、响应、OTA等";
            if (key == "id")
                return "命令或报文流水号";
            if (key == "cmd" || key.EndsWith(".cmd"))
                return "下行命令字";
            if (key == "ok" || key.EndsWith(".ok"))
                return "命令执行是否成功";
            if (key == "code" || key.EndsWith(".code"))
                return "执行结果代码";
            if (key == "message" || key.EndsWith(".message"))
                return "执行说明或告警说明";
            if (key == "device_id" || key.EndsWith(".device_id"))
                return "设备ID";
            if (key == "firmware_version" || key.EndsWith(".firmware_version"))
                return "固件版本";
            if (key == "business_mode" || key.EndsWith(".business_mode"))
                return "业务模式/真实板卡或测试模式标识";
            if (key == "status" || key.EndsWith(".status"))
                return "设备在线状态，online表示在线，offline表示离线";
            if (key == "reason" || key.EndsWith(".reason"))
                return "状态变化原因，例如mqtt_connected或mqtt_lwt";
            if (key.EndsWith(".voltage_mv"))
                return "HLW8112电表电压，单位mV";
            if (key.EndsWith(".current_ma"))
                return "HLW8112电表电流，单位mA";
            if (key.EndsWith(".active_power_mw"))
                return "HLW8112有功功率，单位mW";
            if (key.EndsWith(".reactive_power_mvar"))
                return "HLW8112无功功率，单位mvar";
            if (key.EndsWith(".power_factor_x1000"))
                return "功率因数，放大1000倍";
            if (key.EndsWith(".energy_one_wh") || key.EndsWith(".energy_total_wh"))
                return "电能数据，单位Wh";
            if (key.EndsWith(".pulse_count"))
                return "电表脉冲计数";
            if (key.EndsWith(".temperature_c_x10"))
                return "环境温度，摄氏度放大10倍";
            if (key.EndsWith(".humidity_rh_x10"))
                return "环境湿度，RH%放大10倍";
            if (key.EndsWith(".pm25_ugm3"))
                return "PM2.5浓度，单位ug/m3";
            if (key.EndsWith(".co2_ppm"))
                return "二氧化碳浓度，单位ppm";
            if (key.EndsWith(".illuminance_lux"))
                return "照度，单位lux";
            if (key.EndsWith(".rs485.online"))
                return "RS485总线或外设在线状态";
            if (key.EndsWith(".device_count"))
                return "RS485外设数量";
            if (key.EndsWith(".read_period_s"))
                return "RS485轮询周期，单位秒";
            if (key.EndsWith(".last_response_ms"))
                return "RS485最近响应耗时，单位ms";
            if (key.EndsWith(".tx_count") || key.EndsWith(".rx_count"))
                return "RS485收发计数";
            if (key.EndsWith(".error_count"))
                return "RS485通信错误计数";
            if (key.EndsWith(".brightness"))
                return "灯具亮度或调光百分比";
            if (key.EndsWith(".relay"))
                return "继电器状态";
            if (key.EndsWith(".event") || key == "event")
                return "事件/告警类型";
            if (key.EndsWith(".level") || key == "level")
                return "事件级别";
            if (key.EndsWith(".url"))
                return "OTA固件下载地址";
            if (key.EndsWith(".size") || key.EndsWith(".size_bytes"))
                return "OTA文件大小";
            if (key.EndsWith(".sha256") || key.EndsWith(".crc32"))
                return "OTA文件校验值";
            if (key.EndsWith(".version"))
                return "版本号";
            return "业务字段";
        }

        private string BuildSummary(MqttMessageRecord record)
        {
            if (!string.IsNullOrWhiteSpace(record.StatusText))
                return "状态 " + record.StatusText;
            if (!string.IsNullOrWhiteSpace(record.EventName))
                return "事件 " + record.EventName;
            if (!string.IsNullOrWhiteSpace(record.Command))
                return "命令 " + record.Command;
            if (!string.IsNullOrWhiteSpace(record.Schema))
                return record.Schema;
            if (record.RawPayload.Length <= 48)
                return record.RawPayload;
            return record.RawPayload.Substring(0, 48) + "...";
        }

        private string SelectPageKey(string direction, string topicKey, string schema)
        {
            string schemaLower = (schema ?? string.Empty).ToLowerInvariant();
            if (schemaLower.Contains("status"))
                return PageStatus;
            if (schemaLower.Contains("event"))
                return PageEvent;
            if (schemaLower.Contains("telemetry"))
                return PageTelemetry;
            if (schemaLower.Contains("response"))
                return PageCommand;
            if (schemaLower.Contains("ota"))
                return PageOta;
            if (string.Equals(topicKey, PageCommand, StringComparison.Ordinal))
                return PageCommand;
            if (string.Equals(topicKey, PageEvent, StringComparison.Ordinal))
                return PageEvent;
            if (string.Equals(topicKey, PageOta, StringComparison.Ordinal))
                return PageOta;
            if (string.Equals(topicKey, PageDebug, StringComparison.Ordinal))
                return PageDebug;
            if (string.Equals(topicKey, PageStatus, StringComparison.Ordinal))
                return PageStatus;
            if (string.Equals(topicKey, PageTelemetry, StringComparison.Ordinal))
                return PageTelemetry;
            return string.Equals(direction, "TX", StringComparison.Ordinal) ? PageCommand : PageDebug;
        }

        private string ClassifyTopic(string topic)
        {
            string normalized = (topic ?? string.Empty).Trim();
            for (int i = 0; i < Management.TopicCount; i++)
            {
                string configured = topicTextBoxes[i] == null ? string.Empty : topicTextBoxes[i].Text.Trim();
                if (TopicMatchesFilter(configured, normalized))
                    return TopicKeys[i];
            }

            string statusTopic = textStatusTopic == null ? string.Empty : textStatusTopic.Text.Trim();
            if (TopicMatchesFilter(statusTopic, normalized) || normalized.StartsWith("v1/devices/status/", StringComparison.Ordinal))
                return PageStatus;
            if (normalized.StartsWith("v1/devices/request/", StringComparison.Ordinal))
                return PageCommand;
            if (normalized.StartsWith("v1/devices/response/", StringComparison.Ordinal))
                return PageTelemetry;
            if (normalized.EndsWith("/get", StringComparison.OrdinalIgnoreCase))
                return PageCommand;
            if (normalized.EndsWith("/event", StringComparison.OrdinalIgnoreCase))
                return PageEvent;
            if (normalized.EndsWith("/ota", StringComparison.OrdinalIgnoreCase))
                return PageOta;
            if (normalized.EndsWith("/debug", StringComparison.OrdinalIgnoreCase))
                return PageDebug;
            return PageTelemetry;
        }

        private string TopicKeyToLabel(string topicKey)
        {
            if (topicKey == PageTelemetry)
                return T("上行遥测", "Telemetry");
            if (topicKey == PageCommand)
                return T("下行/get", "Command / get");
            if (topicKey == PageEvent)
                return T("事件告警", "Event / Alarm");
            if (topicKey == PageOta)
                return "OTA";
            if (topicKey == PageDebug)
                return T("调试/兼容", "Debug / Compatible");
            if (topicKey == PageStatus)
                return T("状态/LWT", "Status / LWT");
            return topicKey ?? string.Empty;
        }

        private static bool TopicMatchesFilter(string filter, string topic)
        {
            string configured = (filter ?? string.Empty).Trim();
            string actual = (topic ?? string.Empty).Trim();
            if (string.IsNullOrWhiteSpace(configured) || string.IsNullOrWhiteSpace(actual))
                return false;
            if (string.Equals(configured, actual, StringComparison.Ordinal))
                return true;
            if (!IsTopicFilter(configured))
                return false;

            string[] filterLevels = configured.Split('/');
            string[] topicLevels = actual.Split('/');
            for (int i = 0; i < filterLevels.Length; i++)
            {
                string level = filterLevels[i];
                if (level == "#")
                    return i == filterLevels.Length - 1;
                if (i >= topicLevels.Length)
                    return false;
                if (level == "+")
                    continue;
                if (!string.Equals(level, topicLevels[i], StringComparison.Ordinal))
                    return false;
            }
            return filterLevels.Length == topicLevels.Length;
        }

        private static IDictionary<string, object> TryParseJsonObject(string payload)
        {
            object value = TryParseJsonValue(payload);
            return value as IDictionary<string, object>;
        }

        private static object TryParseJsonValue(string payload)
        {
            if (string.IsNullOrWhiteSpace(payload))
                return null;
            try
            {
                return JsonSerializer.DeserializeObject(payload);
            }
            catch
            {
                return null;
            }
        }

        private static string GetAnyString(IDictionary<string, object> json, params string[] paths)
        {
            if (json == null || paths == null)
                return string.Empty;
            foreach (string path in paths)
            {
                object value = GetValueByPath(json, path);
                if (value != null)
                    return ScalarToString(value);
            }
            return string.Empty;
        }

        private static object GetValueByPath(IDictionary<string, object> json, string path)
        {
            if (json == null || string.IsNullOrWhiteSpace(path))
                return null;
            object current = json;
            string[] parts = path.Split('.');
            foreach (string part in parts)
            {
                IDictionary<string, object> dict = current as IDictionary<string, object>;
                if (dict == null || !dict.TryGetValue(part, out current))
                    return null;
            }
            return current;
        }

        private static string ScalarToString(object value)
        {
            if (value == null)
                return string.Empty;
            if (value is bool)
                return ((bool)value) ? "true" : "false";
            IFormattable formattable = value as IFormattable;
            if (formattable != null)
                return formattable.ToString(null, CultureInfo.InvariantCulture);
            return value.ToString();
        }

        private static string BlankAsDash(string value)
        {
            return string.IsNullOrWhiteSpace(value) ? "--" : value;
        }

        private void RefreshPublishTopics()
        {
            if (comboPublishTopic == null)
                return;

            string selected = comboPublishTopic.Text;
            comboPublishTopic.Items.Clear();
            for (int i = 0; i < Management.TopicCount; i++)
            {
                string topic = topicTextBoxes[i] == null ? string.Empty : topicTextBoxes[i].Text.Trim();
                if (!string.IsNullOrWhiteSpace(topic) && !comboPublishTopic.Items.Contains(topic))
                    comboPublishTopic.Items.Add(topic);
            }
            string statusTopic = textStatusTopic == null ? string.Empty : textStatusTopic.Text.Trim();
            if (!string.IsNullOrWhiteSpace(statusTopic) && !IsTopicFilter(statusTopic) && !comboPublishTopic.Items.Contains(statusTopic))
                comboPublishTopic.Items.Add(statusTopic);
            if (!string.IsNullOrWhiteSpace(selected) && comboPublishTopic.Items.Contains(selected))
                comboPublishTopic.SelectedItem = selected;
            else if (comboPublishTopic.Items.Count > 1)
                comboPublishTopic.SelectedIndex = 1;
            else if (comboPublishTopic.Items.Count > 0)
                comboPublishTopic.SelectedIndex = 0;
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

        private static bool IsTopicFilter(string topic)
        {
            string value = topic ?? string.Empty;
            return value.IndexOf('+') >= 0 || value.IndexOf('#') >= 0;
        }

        private void AddPresetButton(Control parent, string text, int x, int y, string payload, int topicIndex)
        {
            Button button = new Button();
            button.Text = text;
            button.Location = new Point(x, y);
            button.Size = new Size(88, 28);
            button.Click += (sender, args) =>
            {
                if (topicIndex >= 0 && topicIndex < topicTextBoxes.Length)
                {
                    string topic = topicTextBoxes[topicIndex].Text.Trim();
                    if (comboPublishTopic.Items.Contains(topic))
                        comboPublishTopic.SelectedItem = topic;
                }
                textPayload.Text = payload;
            };
            parent.Controls.Add(button);
        }

        private static TextBox AddText(Control parent, string labelText, int labelX, int y, int textX, int width)
        {
            parent.Controls.Add(MakeLabel(labelText, labelX, y + 3, textX - labelX - 10));
            TextBox textBox = new TextBox();
            textBox.Location = new Point(textX, y);
            textBox.Size = new Size(width, 23);
            parent.Controls.Add(textBox);
            return textBox;
        }

        private static Label MakeLabel(string text, int x, int y, int width)
        {
            Label label = new Label();
            label.Text = text;
            label.Location = new Point(x, y);
            label.AutoSize = true;
            label.MaximumSize = new Size(width, 0);
            return label;
        }

        private void AppendLogFromThread(string level, string message)
        {
            if (isClosing || IsDisposed)
                return;
            if (InvokeRequired)
            {
                try
                {
                    BeginInvoke(new Action(() => AppendLog(level, message)));
                }
                catch (InvalidOperationException)
                {
                }
                return;
            }
            AppendLog(level, message);
        }

        private void AppendLog(string level, string message)
        {
            string line = DateTime.Now.ToString("HH:mm:ss", CultureInfo.InvariantCulture) + " " + level + " " + message;
            textLog.AppendText(line + Environment.NewLine);
            textLog.SelectionStart = textLog.TextLength;
            textLog.ScrollToCaret();
            AppLogger.Info("MQTT " + level + " " + message);
        }

        private sealed class MqttMessageRecord
        {
            public DateTime Time;
            public string Direction;
            public string TopicKey;
            public string PageKey;
            public string Topic;
            public string Schema;
            public string Command;
            public string EventName;
            public string StatusText;
            public string OkText;
            public string Summary;
            public string Meaning;
            public string RawPayload;
            public string FormattedJson;
            public List<FieldRow> Fields;

            public string DisplayName
            {
                get
                {
                    if (!string.IsNullOrWhiteSpace(Command))
                        return Command;
                    if (!string.IsNullOrWhiteSpace(EventName))
                        return EventName;
                    if (!string.IsNullOrWhiteSpace(StatusText))
                        return StatusText;
                    if (!string.IsNullOrWhiteSpace(Schema))
                        return Schema;
                    return "-";
                }
            }
        }

        private sealed class FieldRow
        {
            public FieldRow(string path, string value, string meaning)
            {
                Path = path ?? string.Empty;
                Value = value ?? string.Empty;
                Meaning = meaning ?? string.Empty;
            }

            public string Path { get; private set; }
            public string Value { get; private set; }
            public string Meaning { get; private set; }
        }
    }
}
