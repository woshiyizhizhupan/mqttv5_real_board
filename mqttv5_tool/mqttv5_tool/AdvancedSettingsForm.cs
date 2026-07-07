using System;
using System.Drawing;
using System.IO;
using System.Net;
using System.Windows.Forms;

namespace mqttv5_tool
{
    public sealed class AdvancedSettingsForm : Form
    {
        private const int TopicLabelWidth = 165;
        private const int TopicTextX = 220;
        private const int TopicTextWidth = 450;
        private CheckBox checkDhcp;
        private CheckBox checkHost;
        private CheckBox checkPort;
        private CheckBox checkUsername;
        private CheckBox checkPassword;
        private CheckBox checkTopics;
        private CheckBox checkNtp;
        private TextBox textHost;
        private TextBox textPort;
        private TextBox textUsername;
        private TextBox textPassword;
        private readonly TextBox[] topicBoxes = new TextBox[Management.TopicCount];
        private readonly NumericUpDown[] qosBoxes = new NumericUpDown[Management.TopicCount];
        private TextBox textNtpServer;
        private readonly UiLanguage language;

        public AdvancedSettingsForm()
            : this(null)
        {
        }

        public AdvancedSettingsForm(string languageCode)
        {
            language = UiText.Parse(languageCode);
            InitializeComponent();
            ApplyLanguage();
        }

        private void ApplyLanguage()
        {
            UiText.Apply(this, language);
        }

        public void ApplyTo(Management device)
        {
            ushort port;
            if (checkDhcp.Checked)
                device.Mode = 1;
            if (checkHost.Checked)
                device.Host = textHost.Text.Trim();
            if (checkPort.Checked && ushort.TryParse(textPort.Text, out port))
                device.Port = port;
            if (checkUsername.Checked)
                device.Username = textUsername.Text.Trim();
            if (checkPassword.Checked)
                device.Password = textPassword.Text;
            if (checkTopics.Checked)
            {
                for (int i = 0; i < Management.TopicCount; i++)
                {
                    device.Topics[i] = topicBoxes[i].Text.Trim();
                    device.Qos[i] = (byte)qosBoxes[i].Value;
                }
            }
            if (checkNtp.Checked)
                device.NtpServer = textNtpServer.Text.Trim();
        }

        private void buttonApply_Click(object sender, EventArgs e)
        {
            DialogResult = DialogResult.OK;
        }

        private void buttonCancel_Click(object sender, EventArgs e)
        {
            DialogResult = DialogResult.Cancel;
        }

        private void buttonSave_Click(object sender, EventArgs e)
        {
            using (SaveFileDialog dialog = new SaveFileDialog())
            {
                dialog.Filter = "INI配置文件|*.ini";
                dialog.FileName = "gateway_emqx.ini";
                if (dialog.ShowDialog(this) != DialogResult.OK)
                    return;
                File.WriteAllLines(dialog.FileName, BuildIniLines());
            }
        }

        private void buttonLoad_Click(object sender, EventArgs e)
        {
            using (OpenFileDialog dialog = new OpenFileDialog())
            {
                dialog.Filter = "INI配置文件|*.ini";
                if (dialog.ShowDialog(this) != DialogResult.OK)
                    return;
                LoadIniLines(File.ReadAllLines(dialog.FileName));
            }
        }

        private string[] BuildIniLines()
        {
            string[] lines = new string[10 + Management.TopicCount * 2];
            int index = 0;
            lines[index++] = "dhcp=" + checkDhcp.Checked;
            lines[index++] = "host=" + textHost.Text;
            lines[index++] = "port=" + textPort.Text;
            lines[index++] = "username=" + textUsername.Text;
            lines[index++] = "password=" + textPassword.Text;
            for (int i = 0; i < Management.TopicCount; i++)
            {
                lines[index++] = "topic" + (i + 1) + "=" + topicBoxes[i].Text;
                lines[index++] = "qos" + (i + 1) + "=" + qosBoxes[i].Value;
            }
            lines[index++] = "ntp=" + textNtpServer.Text;
            Array.Resize(ref lines, index);
            return lines;
        }

        private void LoadIniLines(string[] lines)
        {
            foreach (string line in lines)
            {
                int split = line.IndexOf('=');
                if (split <= 0)
                    continue;
                string key = line.Substring(0, split).Trim().ToLowerInvariant();
                string value = line.Substring(split + 1);
                if (key == "dhcp")
                    checkDhcp.Checked = value.Equals("true", StringComparison.OrdinalIgnoreCase);
                else if (key == "host")
                    textHost.Text = value;
                else if (key == "port")
                    textPort.Text = value;
                else if (key == "username")
                    textUsername.Text = value;
                else if (key == "password")
                    textPassword.Text = value;
                else if (key == "ntp")
                    textNtpServer.Text = value;
                else
                {
                    for (int i = 0; i < Management.TopicCount; i++)
                    {
                        if (key == "topic" + (i + 1))
                            topicBoxes[i].Text = value;
                        if (key == "qos" + (i + 1) && byte.TryParse(value, out byte qos))
                            qosBoxes[i].Value = Math.Min((byte)2, qos);
                    }
                }
            }
        }

        private void InitializeComponent()
        {
            Font = new Font("Microsoft YaHei UI", 9F, FontStyle.Regular, GraphicsUnit.Point, 134);
            Text = "高级设置";
            Size = new Size(980, 660);
            MinimumSize = new Size(880, 580);

            GroupBox lanGroup = new GroupBox();
            lanGroup.Text = "局域网设置";
            lanGroup.Location = new Point(16, 16);
            lanGroup.Size = new Size(930, 78);
            Controls.Add(lanGroup);

            checkDhcp = new CheckBox();
            checkDhcp.Text = "一键设置所有设备为动态IP：DHCP（批量设置时静态IP不可选）";
            checkDhcp.Location = new Point(18, 32);
            checkDhcp.AutoSize = true;
            lanGroup.Controls.Add(checkDhcp);

            GroupBox serverGroup = new GroupBox();
            serverGroup.Text = "服务器设置";
            serverGroup.Location = new Point(16, 106);
            serverGroup.Size = new Size(930, 430);
            Controls.Add(serverGroup);

            checkHost = AddCheckBox(serverGroup, "服务器IP或域名", 18, 32);
            textHost = AddTextBox(serverGroup, 170, 30, 300, "192.168.0.110");
            checkPort = AddCheckBox(serverGroup, "端口", 500, 32);
            textPort = AddTextBox(serverGroup, 560, 30, 100, "1883");

            checkUsername = AddCheckBox(serverGroup, "用户名", 18, 72);
            textUsername = AddTextBox(serverGroup, 170, 70, 220, string.Empty);
            checkPassword = AddCheckBox(serverGroup, "登录密码", 500, 72);
            textPassword = AddTextBox(serverGroup, 590, 70, 220, string.Empty);

            checkTopics = AddCheckBox(serverGroup, "主题和Qos", 18, 112);
            string[] defaults = Management.BuildDefaultTopics();
            string[] topicLabels =
            {
                "主题1 上行/遥测",
                "主题2 下行/控制",
                "主题3 事件/告警",
                "主题4 OTA在线升级",
                "主题5 调试/兼容"
            };
            for (int i = 0; i < Management.TopicCount; i++)
            {
                int y = 150 + i * 42;
                Label label = new Label();
                label.Text = topicLabels[i];
                label.Location = new Point(44, y + 4);
                label.Size = new Size(TopicLabelWidth, 23);
                label.AutoSize = false;
                label.AutoEllipsis = true;
                label.TextAlign = ContentAlignment.MiddleLeft;
                serverGroup.Controls.Add(label);
                topicBoxes[i] = AddTextBox(serverGroup, TopicTextX, y, TopicTextWidth, defaults[i]);
                Label qosLabel = new Label();
                qosLabel.Text = "Qos";
                qosLabel.Location = new Point(690, y + 4);
                qosLabel.AutoSize = true;
                serverGroup.Controls.Add(qosLabel);
                qosBoxes[i] = new NumericUpDown();
                qosBoxes[i].Minimum = 0;
                qosBoxes[i].Maximum = 2;
                qosBoxes[i].Location = new Point(730, y);
                qosBoxes[i].Width = 55;
                serverGroup.Controls.Add(qosBoxes[i]);
            }

            checkNtp = AddCheckBox(serverGroup, "NTP地址", 18, 366);
            textNtpServer = AddTextBox(serverGroup, 100, 364, 300, "pool.ntp.org");

            Label note = new Label();
            note.Text = "注：单项功能前面打勾，即选择该项设置。点击“一键设置”会逐台下发到设备。";
            note.Location = new Point(18, 550);
            note.AutoSize = true;
            Controls.Add(note);

            Button buttonApply = new Button();
            buttonApply.Text = "一键设置";
            buttonApply.Location = new Point(560, 582);
            buttonApply.Size = new Size(95, 30);
            buttonApply.Click += buttonApply_Click;
            Controls.Add(buttonApply);

            Button buttonSave = new Button();
            buttonSave.Text = "设置保存";
            buttonSave.Location = new Point(665, 582);
            buttonSave.Size = new Size(95, 30);
            buttonSave.Click += buttonSave_Click;
            Controls.Add(buttonSave);

            Button buttonLoad = new Button();
            buttonLoad.Text = "设置加载";
            buttonLoad.Location = new Point(770, 582);
            buttonLoad.Size = new Size(95, 30);
            buttonLoad.Click += buttonLoad_Click;
            Controls.Add(buttonLoad);

            Button buttonCancel = new Button();
            buttonCancel.Text = "取消";
            buttonCancel.Location = new Point(875, 582);
            buttonCancel.Size = new Size(70, 30);
            buttonCancel.Click += buttonCancel_Click;
            Controls.Add(buttonCancel);
        }

        private static CheckBox AddCheckBox(Control parent, string text, int x, int y)
        {
            CheckBox checkBox = new CheckBox();
            checkBox.Text = text;
            checkBox.Location = new Point(x, y);
            checkBox.AutoSize = true;
            parent.Controls.Add(checkBox);
            return checkBox;
        }

        private static TextBox AddTextBox(Control parent, int x, int y, int width, string text)
        {
            TextBox textBox = new TextBox();
            textBox.Location = new Point(x, y);
            textBox.Width = width;
            textBox.Text = text;
            parent.Controls.Add(textBox);
            return textBox;
        }
    }
}
