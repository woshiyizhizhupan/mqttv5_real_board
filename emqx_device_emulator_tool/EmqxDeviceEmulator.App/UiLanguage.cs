using System;
using System.Windows.Forms;

namespace EmqxDeviceEmulator.App
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
            { "EMQX 模拟设备端客户端", "EMQX Device Emulator" },
            { "连接/主题", "Connection / Topics" },
            { "下行/get", "Downlink / get" },
            { "OTA流程", "OTA Flow" },
            { "响应上报", "Responses" },
            { "状态/LWT", "Status / LWT" },
            { "原始日志", "Raw Logs" },
            { "报文详情", "Message Details" },
            { "EMQX连接", "EMQX Connection" },
            { "固件版本", "Firmware Version" },
            { "超时(s)", "Timeout (s)" },
            { "语言", "Language" },
            { "连接", "Connect" },
            { "断开", "Disconnect" },
            { "保存配置", "Save Config" },
            { "清空日志", "Clear Logs" },
            { "未连接", "Disconnected" },
            { "连接中...", "Connecting..." },
            { "已连接", "Connected" },
            { "连接失败", "Connect failed" },
            { "当前业务主题（单request/response/status，兼容五主题）", "Current Topics (single request/response/status, compatible with five-topic mode)" },
            { "telemetry_up 上行/响应", "telemetry_up Up/Response" },
            { "cmd_down 下行/request", "cmd_down Down/request" },
            { "event_up 事件/响应", "event_up Event/Response" },
            { "ota_down OTA/request", "ota_down OTA/request" },
            { "debug_up 调试/响应", "debug_up Debug/Response" },
            { "status 状态/LWT", "status Status/LWT" },
            { "额外订阅主题，一行一个；用于临时监听对方实际下发topic或通配符", "Extra subscriptions, one per line; used to temporarily listen to the peer's actual publish topic or wildcard" },
            { "自动跟随最新报文", "Auto-follow latest message" },
            { "数据含义", "Data Meaning" },
            { "原始报文", "Raw Payload" },
            { "格式化JSON", "Formatted JSON" },
            { "时间", "Time" },
            { "方向", "Direction" },
            { "主题类型", "Topic Type" },
            { "命令/事件", "Command / Event" },
            { "结果", "Result" },
            { "数据含义", "Data Meaning" },
            { "主题", "Topic" },
            { "字段", "Field" },
            { "值", "Value" },
            { "含义", "Meaning" }
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
            if (control is Form || control is TabPage || control is GroupBox || control is Label || control is Button || control is CheckBox || control is RadioButton)
                control.Text = Translate(language, control.Text);
            DataGridView grid = control as DataGridView;
            if (grid != null)
                ApplyGrid(grid, language);
        }
    }
}
