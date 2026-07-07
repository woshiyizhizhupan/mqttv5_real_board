using System;
using System.Globalization;
using System.IO;

namespace mqttv5_tool
{
    public sealed class HostToolSettings
    {
        public const string FileName = "host_tool_settings.ini";

        public string Language { get; set; }
        public bool MqttClientEnabled { get; set; }
        public string MqttHost { get; set; }
        public ushort MqttPort { get; set; }
        public string MqttUsername { get; set; }
        public string MqttPassword { get; set; }
        public int MqttQos { get; set; }
        public bool MqttUseTls { get; set; }
        public int MqttConnectTimeoutSeconds { get; set; }
        public string MqttStatusTopic { get; set; }
        public bool MqttStatusSubscribe { get; set; }
        public string[] MqttTopics { get; private set; }
        public bool[] MqttSubscribe { get; private set; }

        public static string SettingsPath
        {
            get { return Path.Combine(AppDomain.CurrentDomain.BaseDirectory, FileName); }
        }

        public static HostToolSettings Load()
        {
            HostToolSettings settings = CreateDefault();
            if (!File.Exists(SettingsPath))
            {
                settings.Save();
                return settings;
            }

            foreach (string line in File.ReadAllLines(SettingsPath))
            {
                int split = line.IndexOf('=');
                if (split <= 0)
                    continue;
                string key = line.Substring(0, split).Trim().ToLowerInvariant();
                string value = line.Substring(split + 1);
                settings.ApplyValue(key, value);
            }
            return settings;
        }

        public void Save()
        {
            string[] lines = new string[12 + Management.TopicCount * 2];
            int index = 0;
            lines[index++] = "language=" + (string.IsNullOrWhiteSpace(Language) ? "zh-CN" : Language);
            lines[index++] = "mqtt_client_enabled=" + MqttClientEnabled.ToString(CultureInfo.InvariantCulture).ToLowerInvariant();
            lines[index++] = "mqtt_host=" + (MqttHost ?? string.Empty);
            lines[index++] = "mqtt_port=" + MqttPort.ToString(CultureInfo.InvariantCulture);
            lines[index++] = "mqtt_username=" + (MqttUsername ?? string.Empty);
            lines[index++] = "mqtt_password=" + (MqttPassword ?? string.Empty);
            lines[index++] = "mqtt_qos=" + MqttQos.ToString(CultureInfo.InvariantCulture);
            lines[index++] = "mqtt_tls=" + MqttUseTls.ToString(CultureInfo.InvariantCulture).ToLowerInvariant();
            lines[index++] = "mqtt_connect_timeout_seconds=" + MqttConnectTimeoutSeconds.ToString(CultureInfo.InvariantCulture);
            lines[index++] = "mqtt_status_topic=" + (MqttStatusTopic ?? string.Empty);
            lines[index++] = "mqtt_status_subscribe=" + MqttStatusSubscribe.ToString(CultureInfo.InvariantCulture).ToLowerInvariant();
            for (int i = 0; i < Management.TopicCount; i++)
            {
                lines[index++] = "mqtt_topic" + (i + 1) + "=" + (MqttTopics[i] ?? string.Empty);
                lines[index++] = "mqtt_subscribe" + (i + 1) + "=" + MqttSubscribe[i].ToString(CultureInfo.InvariantCulture).ToLowerInvariant();
            }
            Array.Resize(ref lines, index);
            File.WriteAllLines(SettingsPath, lines);
        }

        public void ApplyDevice(Management device)
        {
            if (device == null)
                return;
            MqttHost = device.Host;
            MqttPort = device.Port;
            MqttUsername = device.Username;
            MqttPassword = device.Password;
            for (int i = 0; i < Management.TopicCount; i++)
                MqttTopics[i] = device.Topics[i];
            if (device.Qos != null && device.Qos.Length > 0)
                MqttQos = Math.Max(0, Math.Min(2, (int)device.Qos[0]));
            MqttUseTls = device.TlsMode != 0;
        }

        private static HostToolSettings CreateDefault()
        {
            HostToolSettings settings = new HostToolSettings();
            settings.Language = "zh-CN";
            settings.MqttClientEnabled = true;
            settings.MqttHost = "39.103.154.108";
            settings.MqttPort = 1883;
            settings.MqttUsername = "GM400-452089";
            settings.MqttPassword = "public";
            settings.MqttQos = 2;
            settings.MqttUseTls = false;
            settings.MqttConnectTimeoutSeconds = 6;
            settings.MqttStatusTopic = "v1/devices/status/+";
            settings.MqttStatusSubscribe = true;
            settings.MqttTopics = Management.BuildDefaultTopics();
            settings.MqttSubscribe = new bool[Management.TopicCount];
            settings.MqttSubscribe[0] = true;
            settings.MqttSubscribe[1] = false;
            settings.MqttSubscribe[2] = true;
            settings.MqttSubscribe[3] = false;
            settings.MqttSubscribe[4] = true;
            return settings;
        }

        private void ApplyValue(string key, string value)
        {
            if (key == "language")
                Language = UiText.Code(UiText.Parse(value));
            else if (key == "mqtt_client_enabled")
                MqttClientEnabled = ParseBool(value, MqttClientEnabled);
            else if (key == "mqtt_host")
                MqttHost = value;
            else if (key == "mqtt_port")
            {
                ushort port;
                if (ushort.TryParse(value, out port) && port > 0)
                    MqttPort = port;
            }
            else if (key == "mqtt_username")
                MqttUsername = value;
            else if (key == "mqtt_password")
                MqttPassword = value;
            else if (key == "mqtt_qos")
            {
                int qos;
                if (int.TryParse(value, out qos))
                    MqttQos = Math.Max(0, Math.Min(2, qos));
            }
            else if (key == "mqtt_tls")
                MqttUseTls = ParseBool(value, MqttUseTls);
            else if (key == "mqtt_connect_timeout_seconds")
            {
                int seconds;
                if (int.TryParse(value, out seconds))
                    MqttConnectTimeoutSeconds = Math.Max(2, Math.Min(30, seconds));
            }
            else if (key == "mqtt_status_topic")
                MqttStatusTopic = value;
            else if (key == "mqtt_status_subscribe")
                MqttStatusSubscribe = ParseBool(value, MqttStatusSubscribe);
            else
            {
                for (int i = 0; i < Management.TopicCount; i++)
                {
                    if (key == "mqtt_topic" + (i + 1))
                        MqttTopics[i] = value;
                    if (key == "mqtt_subscribe" + (i + 1))
                        MqttSubscribe[i] = ParseBool(value, MqttSubscribe[i]);
                }
            }
        }

        private static bool ParseBool(string value, bool fallback)
        {
            bool parsed;
            if (bool.TryParse(value, out parsed))
                return parsed;
            if (string.Equals(value, "1", StringComparison.Ordinal))
                return true;
            if (string.Equals(value, "0", StringComparison.Ordinal))
                return false;
            return fallback;
        }
    }
}
