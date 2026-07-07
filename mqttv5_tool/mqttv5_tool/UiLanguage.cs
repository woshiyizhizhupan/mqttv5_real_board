using System;
using System.Collections.Generic;
using System.Windows.Forms;

namespace mqttv5_tool
{
    public enum UiLanguage
    {
        Chinese,
        English
    }

    public static class UiText
    {
        private static readonly string[,] Entries = new string[,]
        {
            { "中文", "Chinese" },
            { "English", "English" },
            { "查找", "Find" },
            { "高级设置", "Advanced Settings" },
            { "EMQX客户端", "EMQX Client" },
            { "EMQX 客户端", "EMQX Client" },
            { "语言选择", "Language" },
            { "设备列表", "Device List" },
            { "找到0个设备", "Found 0 devices" },
            { "正常运行", "Running" },
            { "设置成功", "Settings written" },
            { "设置成功，设备重启中", "Settings written; rebooting" },
            { "设置成功，设备已重新连接", "Settings written; device online" },
            { "设置失败", "Settings failed" },
            { "静态", "Static" },
            { "动态", "Dynamic" },
            { "扫描状态：等待开始", "Scan status: waiting" },
            { "扫描状态：准备扫描", "Scan status: preparing" },
            { "准备扫描", "preparing" },
            { "扫描中", "scanning" },
            { "已完成", "completed" },
            { "扫描完成", "scan completed" },
            { "进度：0%，五线程并发，预计剩余 --", "Progress: 0%, Five workers, remaining --" },
            { "进度：0%，五线程并发，预计剩余 计算中", "Progress: 0%, Five workers, remaining calculating" },
            { "版本：2.3.8 稳定性修复版", "Version: 2.3.8 Stability Fix" },
            { "打开日志目录", "Open Log Folder" },
            { "重启设备", "Restart Device" },
            { "设置", "Settings" },
            { "开始查找", "Start Scan" },
            { "结束查找", "Stop Scan" },
            { "查找已停止", "Scan stopped" },
            { "正在停止查找...", "Stopping scan..." },
            { "局域网设置", "LAN Settings" },
            { "服务器设置", "Server Settings" },
            { "确定", "OK" },
            { "取消", "Cancel" },
            { "描述", "Description" },
            { "设备ID", "Device ID" },
            { "设备型号 GM400 不可更改；设备ID可更改。", "Device model GM400 is fixed; device ID can be changed." },
            { "动态IP：DHCP", "Dynamic IP: DHCP" },
            { "静态IP", "Static IP" },
            { "IP地址", "IP Address" },
            { "子网掩码", "Subnet Mask" },
            { "默认网关", "Default Gateway" },
            { "DNS服务器", "DNS Server" },
            { "MAC地址", "MAC Address" },
            { "服务器IP或域名", "Server IP or Domain" },
            { "端口", "Port" },
            { "用户名", "Username" },
            { "登录密码", "Password" },
            { "主题1：上行/遥测", "Topic 1: Telemetry Up" },
            { "主题2：下行/控制", "Topic 2: Command Down" },
            { "主题3：事件/告警", "Topic 3: Event / Alarm" },
            { "主题4：OTA在线升级", "Topic 4: OTA Upgrade" },
            { "主题5：调试/兼容", "Topic 5: Debug / Compatible" },
            { "NTP地址（RTC校时）", "NTP Server (RTC Sync)" },
            { "一键设置所有设备为动态IP：DHCP（批量设置时静态IP不可选）", "Set all devices to dynamic IP: DHCP (static IP disabled in batch mode)" },
            { "主题和Qos", "Topics and QoS" },
            { "主题1 上行/遥测", "Topic 1 Telemetry Up" },
            { "主题2 下行/控制", "Topic 2 Command Down" },
            { "主题3 事件/告警", "Topic 3 Event / Alarm" },
            { "主题4 OTA在线升级", "Topic 4 OTA Upgrade" },
            { "主题5 调试/兼容", "Topic 5 Debug / Compatible" },
            { "NTP地址", "NTP Server" },
            { "注：单项功能前面打勾，即选择该项设置。点击“一键设置”会逐台下发到设备。", "Note: check an item to include it. Apply All sends selected settings to each device." },
            { "一键设置", "Apply All" },
            { "设置保存", "Save Settings" },
            { "设置加载", "Load Settings" },
            { "连接参数", "Connection Parameters" },
            { "主题/发布", "Topics / Publish" },
            { "报文浏览", "Message Browser" },
            { "报文详情/日志", "Message Details / Logs" },
            { "连接", "Connect" },
            { "连接超时(s)", "Timeout (s)" },
            { "断开", "Disconnect" },
            { "保存配置", "Save Config" },
            { "业务主题与状态主题", "Business and Status Topics" },
            { "订阅候选来自五个完整业务主题和状态主题，主题可逐项编辑；MQTT 标准客户端不能直接枚举服务端全部历史 topic。", "Subscription candidates come from the five business topics and the status topic. Topics are editable; a standard MQTT client cannot enumerate all historical broker topics." },
            { "从当前设备载入", "Load From Device" },
            { "刷新发布列表", "Refresh Publish List" },
            { "发布", "Publish" },
            { "自动跟随最新报文", "Auto-follow latest message" },
            { "跳到最新", "Latest" },
            { "总览", "Overview" },
            { "上行遥测", "Telemetry" },
            { "事件告警", "Event / Alarm" },
            { "下行/get", "Command / get" },
            { "调试/兼容", "Debug / Compatible" },
            { "状态/LWT", "Status / LWT" },
            { "选中报文详情", "Selected Message Details" },
            { "数据含义", "Data Meaning" },
            { "原始报文", "Raw Payload" },
            { "格式化 JSON", "Formatted JSON" },
            { "字段解释", "Field Meaning" },
            { "原始日志", "Raw Logs" },
            { "订阅", "Subscribe" },
            { "发送", "Send" },
            { "status 状态/LWT", "status Status / LWT" },
            { "时间", "Time" },
            { "方向", "Direction" },
            { "主题类型", "Topic Type" },
            { "命令/事件", "Command / Event" },
            { "结果", "Result" },
            { "主题", "Topic" },
            { "字段", "Field" },
            { "值", "Value" },
            { "含义", "Meaning" },
            { "telemetry_up 上行", "telemetry_up Telemetry" },
            { "cmd_down 下行/get", "cmd_down Command / get" },
            { "event_up 事件/告警", "event_up Event / Alarm" },
            { "ota_down OTA", "ota_down OTA" },
            { "debug_up 调试", "debug_up Debug" }
        };

        public static UiLanguage Parse(string value)
        {
            if (string.Equals(value, "en", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(value, "en-US", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(value, "English", StringComparison.OrdinalIgnoreCase))
                return UiLanguage.English;
            return UiLanguage.Chinese;
        }

        public static string Code(UiLanguage language)
        {
            return language == UiLanguage.English ? "en-US" : "zh-CN";
        }

        public static string LanguageDisplay(UiLanguage language)
        {
            return language == UiLanguage.English ? "English" : "中文";
        }

        public static string T(UiLanguage language, string chinese, string english)
        {
            return language == UiLanguage.English ? english : chinese;
        }

        public static string Translate(UiLanguage language, string text)
        {
            if (string.IsNullOrEmpty(text))
                return text;
            for (int i = 0; i < Entries.GetLength(0); i++)
            {
                if (string.Equals(text, Entries[i, 0], StringComparison.Ordinal) ||
                    string.Equals(text, Entries[i, 1], StringComparison.Ordinal))
                    return language == UiLanguage.English ? Entries[i, 1] : Entries[i, 0];
            }
            return text;
        }

        public static void Apply(Control root, UiLanguage language)
        {
            if (root == null)
                return;
            ApplyControl(root, language);
            foreach (Control child in root.Controls)
                Apply(child, language);
        }

        public static void ApplyGrid(DataGridView grid, UiLanguage language)
        {
            if (grid == null)
                return;
            foreach (DataGridViewColumn column in grid.Columns)
                column.HeaderText = Translate(language, column.HeaderText);
        }

        private static void ApplyControl(Control control, UiLanguage language)
        {
            if (ShouldTranslateControl(control))
                control.Text = Translate(language, control.Text);
            DataGridView grid = control as DataGridView;
            if (grid != null)
                ApplyGrid(grid, language);
        }

        private static bool ShouldTranslateControl(Control control)
        {
            return control is Form ||
                control is TabPage ||
                control is GroupBox ||
                control is Label ||
                control is Button ||
                control is CheckBox ||
                control is RadioButton;
        }
    }
}
