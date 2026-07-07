using System;
using System.Drawing;
using System.Net;
using System.Net.NetworkInformation;
using System.Text;
using System.Windows.Forms;

namespace mqttv5_tool
{
    public sealed class Form2 : Form
    {
        private TextBox textDeviceId;
        private RadioButton radioDhcp;
        private RadioButton radioStatic;
        private TextBox textIp;
        private TextBox textSn;
        private TextBox textGw;
        private TextBox textDns;
        private TextBox textMac;
        private TextBox textHost;
        private TextBox textPort;
        private TextBox textUsername;
        private TextBox textPassword;
        private readonly TextBox[] topicBoxes = new TextBox[Management.TopicCount];
        private readonly NumericUpDown[] qosBoxes = new NumericUpDown[Management.TopicCount];
        private TextBox textNtpServer;
        private Panel staticIpPanel;
        private readonly UiLanguage language;
        private PhysicalAddress loadedMac = PhysicalAddress.None;

        public Form2()
            : this(null)
        {
        }

        public Form2(string languageCode)
        {
            language = UiText.Parse(languageCode);
            InitializeComponent();
            ApplyLanguage();
        }

        private void ApplyLanguage()
        {
            UiText.Apply(this, language);
        }

        public void LoadFromManagement(Management management)
        {
            textDeviceId.Text = management.DeviceId;
            radioDhcp.Checked = management.Mode != 0;
            radioStatic.Checked = management.Mode == 0;
            textIp.Text = management.Ip.ToString();
            textSn.Text = management.Sn.ToString();
            textGw.Text = management.Gw.ToString();
            textDns.Text = management.Dns.ToString();
            textMac.Text = FormatMac(management.Mac);
            loadedMac = management.Mac;
            textHost.Text = management.Host;
            textPort.Text = management.Port.ToString();
            textUsername.Text = management.Username;
            textPassword.Text = management.Password;
            for (int i = 0; i < Management.TopicCount; i++)
            {
                topicBoxes[i].Text = management.Topics[i];
                qosBoxes[i].Value = management.Qos[i];
            }
            textNtpServer.Text = management.NtpServer;
            UpdateStaticIpEnabled();
        }

        public void ApplyToManagement(Management management)
        {
            IPAddress parsed;
            ushort port;
            management.DeviceId = textDeviceId.Text.Trim();
            management.Mode = radioDhcp.Checked ? (byte)1 : (byte)0;
            if (IPAddress.TryParse(textIp.Text, out parsed))
                management.Ip = parsed;
            if (IPAddress.TryParse(textSn.Text, out parsed))
                management.Sn = parsed;
            if (IPAddress.TryParse(textGw.Text, out parsed))
                management.Gw = parsed;
            if (IPAddress.TryParse(textDns.Text, out parsed))
                management.Dns = parsed;
            if (ushort.TryParse(textPort.Text, out port))
                management.Port = port;
            management.Host = textHost.Text.Trim();
            management.Username = textUsername.Text.Trim();
            management.Password = textPassword.Text;
            for (int i = 0; i < Management.TopicCount; i++)
            {
                management.Topics[i] = topicBoxes[i].Text.Trim();
                management.Qos[i] = (byte)qosBoxes[i].Value;
            }
            management.NtpServer = textNtpServer.Text.Trim();
        }

        private void buttonOk_Click(object sender, EventArgs e)
        {
            if (!ValidateForWrite())
                return;
            DialogResult = DialogResult.OK;
        }

        private void buttonCancel_Click(object sender, EventArgs e)
        {
            DialogResult = DialogResult.Cancel;
        }

        private void radioIpMode_CheckedChanged(object sender, EventArgs e)
        {
            UpdateStaticIpEnabled();
        }

        private void UpdateStaticIpEnabled()
        {
            if (staticIpPanel != null)
                staticIpPanel.Enabled = radioStatic.Checked;
        }

        private bool ValidateForWrite()
        {
            ushort port;
            if (string.IsNullOrWhiteSpace(textDeviceId.Text))
                return ShowValidationError("设备ID不能为空。");
            if (!ValidateTextLength("设备ID", textDeviceId.Text, Management.DeviceIdLength))
                return false;
            if (radioStatic.Checked)
            {
                IPAddress ip;
                IPAddress sn;
                IPAddress gw;
                IPAddress dns;
                string error;
                if (!IPAddress.TryParse(textIp.Text, out ip))
                    return ShowValidationError("IP地址格式不正确。");
                if (!IPAddress.TryParse(textSn.Text, out sn))
                    return ShowValidationError("子网掩码格式不正确。");
                if (!IPAddress.TryParse(textGw.Text, out gw))
                    return ShowValidationError("默认网关格式不正确。");
                if (!IPAddress.TryParse(textDns.Text, out dns))
                    return ShowValidationError("DNS服务器格式不正确。");
                error = Management.ValidateStaticNetworkFields(ip, sn, gw, dns, loadedMac);
                if (error != null)
                    return ShowValidationError(error);
            }
            if (string.IsNullOrWhiteSpace(textHost.Text))
                return ShowValidationError("服务器IP或域名不能为空。");
            if (!ValidateTextLength("服务器IP或域名", textHost.Text, Management.HostLength))
                return false;
            if (!ushort.TryParse(textPort.Text, out port) || port == 0)
                return ShowValidationError("端口必须是 1-65535。");
            if (!ValidateTextLength("用户名", textUsername.Text, Management.UsernameLength))
                return false;
            if (!ValidateTextLength("登录密码", textPassword.Text, Management.PasswordLength))
                return false;
            for (int i = 0; i < Management.TopicCount; i++)
            {
                if (string.IsNullOrWhiteSpace(topicBoxes[i].Text))
                    return ShowValidationError("主题" + (i + 1) + "不能为空。");
                if (!ValidateTextLength("主题" + (i + 1), topicBoxes[i].Text, Management.TopicLength))
                    return false;
            }
            if (!ValidateTextLength("NTP地址", textNtpServer.Text, Management.NtpServerLength))
                return false;
            return true;
        }

        private static bool ValidateTextLength(string name, string value, int length)
        {
            if (Encoding.UTF8.GetByteCount(value ?? string.Empty) < length)
                return true;
            return ShowValidationError(name + "过长，UTF-8 编码后必须小于 " + length + " 字节。");
        }

        private static bool ShowValidationError(string message)
        {
            MessageBox.Show(message, "设置", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return false;
        }

        private void InitializeComponent()
        {
            Font = new Font("Microsoft YaHei UI", 9F, FontStyle.Regular, GraphicsUnit.Point, 134);
            Text = "设置";
            Size = new Size(980, 660);
            MinimumSize = new Size(940, 620);

            TabControl tabControl = new TabControl();
            tabControl.Dock = DockStyle.Top;
            tabControl.Height = 560;

            TabPage lanPage = new TabPage("局域网设置");
            TabPage serverPage = new TabPage("服务器设置");
            tabControl.TabPages.Add(lanPage);
            tabControl.TabPages.Add(serverPage);
            Controls.Add(tabControl);

            Button buttonOk = new Button();
            buttonOk.Text = "确定";
            buttonOk.Size = new Size(90, 30);
            buttonOk.Location = new Point(740, 575);
            buttonOk.Click += buttonOk_Click;
            Controls.Add(buttonOk);

            Button buttonCancel = new Button();
            buttonCancel.Text = "取消";
            buttonCancel.Size = new Size(90, 30);
            buttonCancel.Location = new Point(840, 575);
            buttonCancel.Click += buttonCancel_Click;
            Controls.Add(buttonCancel);

            BuildLanPage(lanPage);
            BuildServerPage(serverPage);
        }

        private void BuildLanPage(TabPage page)
        {
            GroupBox descGroup = new GroupBox();
            descGroup.Text = "描述";
            descGroup.Location = new Point(16, 16);
            descGroup.Size = new Size(900, 78);
            page.Controls.Add(descGroup);

            Label labelDeviceId = new Label();
            labelDeviceId.Text = "设备ID";
            labelDeviceId.Location = new Point(18, 32);
            labelDeviceId.AutoSize = true;
            descGroup.Controls.Add(labelDeviceId);

            textDeviceId = new TextBox();
            textDeviceId.Location = new Point(90, 28);
            textDeviceId.Width = 230;
            descGroup.Controls.Add(textDeviceId);

            Label hint = new Label();
            hint.Text = "设备型号 GM400 不可更改；设备ID可更改。";
            hint.Location = new Point(340, 32);
            hint.AutoSize = true;
            descGroup.Controls.Add(hint);

            radioDhcp = new RadioButton();
            radioDhcp.Text = "动态IP：DHCP";
            radioDhcp.Location = new Point(28, 112);
            radioDhcp.AutoSize = true;
            radioDhcp.CheckedChanged += radioIpMode_CheckedChanged;
            page.Controls.Add(radioDhcp);

            radioStatic = new RadioButton();
            radioStatic.Text = "静态IP";
            radioStatic.Location = new Point(160, 112);
            radioStatic.AutoSize = true;
            radioStatic.CheckedChanged += radioIpMode_CheckedChanged;
            page.Controls.Add(radioStatic);

            staticIpPanel = new Panel();
            staticIpPanel.Location = new Point(16, 150);
            staticIpPanel.Size = new Size(900, 210);
            page.Controls.Add(staticIpPanel);

            textIp = AddLabeledText(staticIpPanel, "IP地址", 12, 16, 160, 220);
            textSn = AddLabeledText(staticIpPanel, "子网掩码", 12, 56, 160, 220);
            textGw = AddLabeledText(staticIpPanel, "默认网关", 12, 96, 160, 220);
            textDns = AddLabeledText(staticIpPanel, "DNS服务器", 12, 136, 160, 220);
            textMac = AddLabeledText(staticIpPanel, "MAC地址", 12, 176, 160, 220);
            textMac.ReadOnly = true;
        }

        private void BuildServerPage(TabPage page)
        {
            textHost = AddLabeledText(page, "服务器IP或域名", 20, 24, 220, 600);
            textPort = AddLabeledText(page, "端口", 20, 64, 220, 140);
            textUsername = AddLabeledText(page, "用户名", 20, 104, 220, 260);
            textPassword = AddLabeledText(page, "登录密码", 20, 144, 220, 260);

            string[] labels =
            {
                "主题1：上行/遥测",
                "主题2：下行/控制",
                "主题3：事件/告警",
                "主题4：OTA在线升级",
                "主题5：调试/兼容"
            };

            for (int i = 0; i < Management.TopicCount; i++)
            {
                int y = 194 + i * 44;
                topicBoxes[i] = AddLabeledText(page, labels[i], 20, y, 220, 500);
                Label qosLabel = new Label();
                qosLabel.Text = "Qos";
                qosLabel.Location = new Point(740, y + 4);
                qosLabel.AutoSize = true;
                page.Controls.Add(qosLabel);

                qosBoxes[i] = new NumericUpDown();
                qosBoxes[i].Minimum = 0;
                qosBoxes[i].Maximum = 2;
                qosBoxes[i].Location = new Point(780, y);
                qosBoxes[i].Width = 50;
                page.Controls.Add(qosBoxes[i]);
            }

            textNtpServer = AddLabeledText(page, "NTP地址（RTC校时）", 20, 420, 220, 500);
        }

        private static TextBox AddLabeledText(Control parent, string labelText, int x, int y, int textX, int width)
        {
            Label label = new Label();
            label.Text = labelText;
            label.Location = new Point(x, y + 4);
            label.AutoSize = true;
            label.MaximumSize = new Size(textX - x - 10, 0);
            parent.Controls.Add(label);

            TextBox textBox = new TextBox();
            textBox.Location = new Point(textX, y);
            textBox.Width = width;
            parent.Controls.Add(textBox);
            return textBox;
        }

        private static string FormatMac(PhysicalAddress address)
        {
            byte[] bytes = address.GetAddressBytes();
            if (bytes.Length == 0)
                return string.Empty;
            return BitConverter.ToString(bytes).ToLowerInvariant();
        }
    }
}
