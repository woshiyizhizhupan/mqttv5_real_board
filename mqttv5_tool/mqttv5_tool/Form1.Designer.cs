using System.Drawing;
using System.Windows.Forms;

namespace mqttv5_tool
{
    partial class Form1
    {
        private System.ComponentModel.IContainer components = null;

        protected override void Dispose(bool disposing)
        {
            if (disposing && components != null)
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        private void InitializeComponent()
        {
            this.tableLayoutPanelMain = new TableLayoutPanel();
            this.panelTop = new Panel();
            this.buttonFindNav = new Button();
            this.buttonAdvancedNav = new Button();
            this.buttonMqttClient = new Button();
            this.labelLanguage = new Label();
            this.comboBoxLanguage = new ComboBox();
            this.groupBoxDevices = new GroupBox();
            this.dataGridViewDevices = new DataGridView();
            this.panelBottom = new Panel();
            this.labelFound = new Label();
            this.labelScanCurrent = new Label();
            this.labelScanProgress = new Label();
            this.progressScan = new ProgressBar();
            this.labelVersion = new Label();
            this.buttonOpenLogs = new Button();
            this.buttonRestart = new Button();
            this.buttonSettings = new Button();
            this.buttonStopSearch = new Button();
            this.buttonStartSearch = new Button();
            this.tableLayoutPanelMain.SuspendLayout();
            this.panelTop.SuspendLayout();
            this.groupBoxDevices.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this.dataGridViewDevices)).BeginInit();
            this.panelBottom.SuspendLayout();
            this.SuspendLayout();
            // 
            // tableLayoutPanelMain
            // 
            this.tableLayoutPanelMain.ColumnCount = 1;
            this.tableLayoutPanelMain.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            this.tableLayoutPanelMain.Controls.Add(this.panelTop, 0, 0);
            this.tableLayoutPanelMain.Controls.Add(this.groupBoxDevices, 0, 1);
            this.tableLayoutPanelMain.Controls.Add(this.panelBottom, 0, 2);
            this.tableLayoutPanelMain.Dock = DockStyle.Fill;
            this.tableLayoutPanelMain.Location = new Point(0, 0);
            this.tableLayoutPanelMain.Name = "tableLayoutPanelMain";
            this.tableLayoutPanelMain.RowCount = 3;
            this.tableLayoutPanelMain.RowStyles.Add(new RowStyle(SizeType.Absolute, 86F));
            this.tableLayoutPanelMain.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
            this.tableLayoutPanelMain.RowStyles.Add(new RowStyle(SizeType.Absolute, 96F));
            this.tableLayoutPanelMain.Size = new Size(1024, 650);
            this.tableLayoutPanelMain.TabIndex = 0;
            // 
            // panelTop
            // 
            this.panelTop.BorderStyle = BorderStyle.FixedSingle;
            this.panelTop.Controls.Add(this.buttonFindNav);
            this.panelTop.Controls.Add(this.buttonAdvancedNav);
            this.panelTop.Controls.Add(this.buttonMqttClient);
            this.panelTop.Controls.Add(this.labelLanguage);
            this.panelTop.Controls.Add(this.comboBoxLanguage);
            this.panelTop.Dock = DockStyle.Fill;
            this.panelTop.Location = new Point(8, 8);
            this.panelTop.Margin = new Padding(8);
            this.panelTop.Name = "panelTop";
            this.panelTop.Size = new Size(1008, 70);
            this.panelTop.TabIndex = 0;
            // 
            // buttonFindNav
            // 
            this.buttonFindNav.Font = new Font("Microsoft YaHei UI", 9F, FontStyle.Regular, GraphicsUnit.Point, ((byte)(134)));
            this.buttonFindNav.Location = new Point(12, 12);
            this.buttonFindNav.Name = "buttonFindNav";
            this.buttonFindNav.Size = new Size(82, 46);
            this.buttonFindNav.TabIndex = 0;
            this.buttonFindNav.Text = "查找";
            this.buttonFindNav.UseVisualStyleBackColor = true;
            this.buttonFindNav.Click += new System.EventHandler(this.buttonFindNav_Click);
            // 
            // buttonAdvancedNav
            // 
            this.buttonAdvancedNav.Font = new Font("Microsoft YaHei UI", 9F, FontStyle.Regular, GraphicsUnit.Point, ((byte)(134)));
            this.buttonAdvancedNav.Location = new Point(104, 12);
            this.buttonAdvancedNav.Name = "buttonAdvancedNav";
            this.buttonAdvancedNav.Size = new Size(92, 46);
            this.buttonAdvancedNav.TabIndex = 1;
            this.buttonAdvancedNav.Text = "高级设置";
            this.buttonAdvancedNav.UseVisualStyleBackColor = true;
            this.buttonAdvancedNav.Click += new System.EventHandler(this.buttonAdvancedNav_Click);
            // 
            // buttonMqttClient
            // 
            this.buttonMqttClient.Font = new Font("Microsoft YaHei UI", 9F, FontStyle.Regular, GraphicsUnit.Point, ((byte)(134)));
            this.buttonMqttClient.Location = new Point(206, 12);
            this.buttonMqttClient.Name = "buttonMqttClient";
            this.buttonMqttClient.Size = new Size(112, 46);
            this.buttonMqttClient.TabIndex = 2;
            this.buttonMqttClient.Text = "EMQX客户端";
            this.buttonMqttClient.UseVisualStyleBackColor = true;
            this.buttonMqttClient.Click += new System.EventHandler(this.buttonMqttClient_Click);
            // 
            // labelLanguage
            // 
            this.labelLanguage.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            this.labelLanguage.AutoSize = true;
            this.labelLanguage.Location = new Point(781, 27);
            this.labelLanguage.Name = "labelLanguage";
            this.labelLanguage.Size = new Size(65, 17);
            this.labelLanguage.TabIndex = 3;
            this.labelLanguage.Text = "语言选择";
            // 
            // comboBoxLanguage
            // 
            this.comboBoxLanguage.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            this.comboBoxLanguage.DropDownStyle = ComboBoxStyle.DropDownList;
            this.comboBoxLanguage.FormattingEnabled = true;
            this.comboBoxLanguage.Items.AddRange(new object[] {
            "中文",
            "English"});
            this.comboBoxLanguage.Location = new Point(852, 23);
            this.comboBoxLanguage.Name = "comboBoxLanguage";
            this.comboBoxLanguage.Size = new Size(135, 25);
            this.comboBoxLanguage.TabIndex = 4;
            // 
            // groupBoxDevices
            // 
            this.groupBoxDevices.Controls.Add(this.dataGridViewDevices);
            this.groupBoxDevices.Dock = DockStyle.Fill;
            this.groupBoxDevices.Location = new Point(8, 89);
            this.groupBoxDevices.Margin = new Padding(8, 3, 8, 3);
            this.groupBoxDevices.Name = "groupBoxDevices";
            this.groupBoxDevices.Padding = new Padding(8);
            this.groupBoxDevices.Size = new Size(1008, 462);
            this.groupBoxDevices.TabIndex = 1;
            this.groupBoxDevices.TabStop = false;
            this.groupBoxDevices.Text = "设备列表";
            // 
            // dataGridViewDevices
            // 
            this.dataGridViewDevices.AllowUserToAddRows = false;
            this.dataGridViewDevices.AllowUserToDeleteRows = false;
            this.dataGridViewDevices.ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.AutoSize;
            this.dataGridViewDevices.Dock = DockStyle.Fill;
            this.dataGridViewDevices.Location = new Point(8, 24);
            this.dataGridViewDevices.Name = "dataGridViewDevices";
            this.dataGridViewDevices.ReadOnly = true;
            this.dataGridViewDevices.RowTemplate.Height = 27;
            this.dataGridViewDevices.Size = new Size(992, 430);
            this.dataGridViewDevices.TabIndex = 0;
            // 
            // panelBottom
            // 
            this.panelBottom.Controls.Add(this.labelFound);
            this.panelBottom.Controls.Add(this.labelScanCurrent);
            this.panelBottom.Controls.Add(this.labelScanProgress);
            this.panelBottom.Controls.Add(this.progressScan);
            this.panelBottom.Controls.Add(this.labelVersion);
            this.panelBottom.Controls.Add(this.buttonOpenLogs);
            this.panelBottom.Controls.Add(this.buttonRestart);
            this.panelBottom.Controls.Add(this.buttonSettings);
            this.panelBottom.Controls.Add(this.buttonStopSearch);
            this.panelBottom.Controls.Add(this.buttonStartSearch);
            this.panelBottom.Dock = DockStyle.Fill;
            this.panelBottom.Location = new Point(8, 557);
            this.panelBottom.Margin = new Padding(8, 3, 8, 8);
            this.panelBottom.Name = "panelBottom";
            this.panelBottom.Size = new Size(1008, 85);
            this.panelBottom.TabIndex = 2;
            // 
            // labelFound
            // 
            this.labelFound.AutoSize = false;
            this.labelFound.Location = new Point(4, 3);
            this.labelFound.Name = "labelFound";
            this.labelFound.Size = new Size(560, 20);
            this.labelFound.TabIndex = 0;
            this.labelFound.Text = "找到0个设备";
            // 
            // labelScanCurrent
            // 
            this.labelScanCurrent.AutoSize = false;
            this.labelScanCurrent.Location = new Point(4, 24);
            this.labelScanCurrent.Name = "labelScanCurrent";
            this.labelScanCurrent.Size = new Size(560, 20);
            this.labelScanCurrent.TabIndex = 1;
            this.labelScanCurrent.Text = "扫描状态：等待开始";
            // 
            // labelScanProgress
            // 
            this.labelScanProgress.AutoSize = false;
            this.labelScanProgress.Location = new Point(4, 64);
            this.labelScanProgress.Name = "labelScanProgress";
            this.labelScanProgress.Size = new Size(560, 18);
            this.labelScanProgress.TabIndex = 2;
            this.labelScanProgress.Text = "进度：0%，五线程并发，预计剩余 --";
            // 
            // progressScan
            // 
            this.progressScan.Location = new Point(4, 46);
            this.progressScan.Maximum = 100;
            this.progressScan.Name = "progressScan";
            this.progressScan.Size = new Size(560, 12);
            this.progressScan.Style = ProgressBarStyle.Continuous;
            this.progressScan.TabIndex = 3;
            // 
            // labelVersion
            // 
            this.labelVersion.AutoSize = true;
            this.labelVersion.Location = new Point(584, 8);
            this.labelVersion.Name = "labelVersion";
            this.labelVersion.Size = new Size(170, 17);
            this.labelVersion.TabIndex = 4;
            this.labelVersion.Text = "版本：2.3.8 稳定性修复版";
            // 
            // buttonOpenLogs
            // 
            this.buttonOpenLogs.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            this.buttonOpenLogs.Location = new Point(584, 44);
            this.buttonOpenLogs.Name = "buttonOpenLogs";
            this.buttonOpenLogs.Size = new Size(86, 30);
            this.buttonOpenLogs.TabIndex = 5;
            this.buttonOpenLogs.Text = "打开日志目录";
            this.buttonOpenLogs.UseVisualStyleBackColor = true;
            this.buttonOpenLogs.Click += new System.EventHandler(this.buttonOpenLogs_Click);
            // 
            // buttonRestart
            // 
            this.buttonRestart.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            this.buttonRestart.Location = new Point(676, 44);
            this.buttonRestart.Name = "buttonRestart";
            this.buttonRestart.Size = new Size(78, 30);
            this.buttonRestart.TabIndex = 6;
            this.buttonRestart.Text = "重启设备";
            this.buttonRestart.UseVisualStyleBackColor = true;
            this.buttonRestart.Click += new System.EventHandler(this.buttonRestart_Click);
            // 
            // buttonSettings
            // 
            this.buttonSettings.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            this.buttonSettings.Location = new Point(760, 44);
            this.buttonSettings.Name = "buttonSettings";
            this.buttonSettings.Size = new Size(78, 30);
            this.buttonSettings.TabIndex = 7;
            this.buttonSettings.Text = "设置";
            this.buttonSettings.UseVisualStyleBackColor = true;
            this.buttonSettings.Click += new System.EventHandler(this.buttonSettings_Click);
            // 
            // buttonStopSearch
            // 
            this.buttonStopSearch.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            this.buttonStopSearch.Enabled = false;
            this.buttonStopSearch.Location = new Point(844, 44);
            this.buttonStopSearch.Name = "buttonStopSearch";
            this.buttonStopSearch.Size = new Size(78, 30);
            this.buttonStopSearch.TabIndex = 8;
            this.buttonStopSearch.Text = "结束查找";
            this.buttonStopSearch.UseVisualStyleBackColor = true;
            this.buttonStopSearch.Click += new System.EventHandler(this.buttonStopSearch_Click);
            // 
            // buttonStartSearch
            // 
            this.buttonStartSearch.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            this.buttonStartSearch.Location = new Point(928, 44);
            this.buttonStartSearch.Name = "buttonStartSearch";
            this.buttonStartSearch.Size = new Size(75, 30);
            this.buttonStartSearch.TabIndex = 9;
            this.buttonStartSearch.Text = "开始查找";
            this.buttonStartSearch.UseVisualStyleBackColor = true;
            this.buttonStartSearch.Click += new System.EventHandler(this.buttonStartSearch_Click);
            // 
            // Form1
            // 
            this.AutoScaleDimensions = new SizeF(7F, 17F);
            this.AutoScaleMode = AutoScaleMode.Font;
            this.ClientSize = new Size(1024, 650);
            this.Controls.Add(this.tableLayoutPanelMain);
            this.Font = new Font("Microsoft YaHei UI", 9F, FontStyle.Regular, GraphicsUnit.Point, ((byte)(134)));
            this.MinimumSize = new Size(960, 560);
            this.Name = "Form1";
            this.Text = "Gateway EMQX QueryTool";
            this.tableLayoutPanelMain.ResumeLayout(false);
            this.panelTop.ResumeLayout(false);
            this.panelTop.PerformLayout();
            this.groupBoxDevices.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)(this.dataGridViewDevices)).EndInit();
            this.panelBottom.ResumeLayout(false);
            this.panelBottom.PerformLayout();
            this.ResumeLayout(false);
        }

        private TableLayoutPanel tableLayoutPanelMain;
        private Panel panelTop;
        private Button buttonFindNav;
        private Button buttonAdvancedNav;
        private Button buttonMqttClient;
        private Label labelLanguage;
        private ComboBox comboBoxLanguage;
        private GroupBox groupBoxDevices;
        private DataGridView dataGridViewDevices;
        private Panel panelBottom;
        private Label labelFound;
        private Label labelScanCurrent;
        private Label labelScanProgress;
        private ProgressBar progressScan;
        private Label labelVersion;
        private Button buttonOpenLogs;
        private Button buttonRestart;
        private Button buttonSettings;
        private Button buttonStopSearch;
        private Button buttonStartSearch;
    }
}
