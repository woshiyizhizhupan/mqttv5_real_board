using System;
using System.Globalization;
using System.IO;
using EmqxDeviceEmulator.Protocol;

namespace EmqxDeviceEmulator.App
{
    internal sealed class DeviceEmulatorSettings
    {
        public const string FileName = "device_emulator_settings.ini";

        public string Language { get; set; }
        public string Host { get; set; }
        public ushort Port { get; set; }
        public string ClientId { get; set; }
        public string Username { get; set; }
        public string Password { get; set; }
        public int Qos { get; set; }
        public bool UseTls { get; set; }
        public int ConnectTimeoutSeconds { get; set; }
        public string DeviceId { get; set; }
        public string FirmwareVersion { get; set; }
        public TopicSet Topics { get; set; }
        public string ExtraSubscriptions { get; set; }
        public bool AutoFollowLatest { get; set; }

        public static string SettingsPath
        {
            get { return Path.Combine(AppDomain.CurrentDomain.BaseDirectory, FileName); }
        }

        public static DeviceEmulatorSettings Load()
        {
            DeviceEmulatorSettings settings = CreateDefault();
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
            string[] lines = new[]
            {
                "language=" + (string.IsNullOrWhiteSpace(Language) ? "zh-CN" : Language),
                "host=" + (Host ?? string.Empty),
                "port=" + Port.ToString(CultureInfo.InvariantCulture),
                "client_id=" + (ClientId ?? string.Empty),
                "username=" + (Username ?? string.Empty),
                "password=" + (Password ?? string.Empty),
                "qos=" + Qos.ToString(CultureInfo.InvariantCulture),
                "tls=" + UseTls.ToString(CultureInfo.InvariantCulture).ToLowerInvariant(),
                "connect_timeout_seconds=" + ConnectTimeoutSeconds.ToString(CultureInfo.InvariantCulture),
                "device_id=" + (DeviceId ?? string.Empty),
                "firmware_version=" + (FirmwareVersion ?? string.Empty),
                "topic_telemetry_up=" + (Topics == null ? string.Empty : Topics.TelemetryUp),
                "topic_cmd_down=" + (Topics == null ? string.Empty : Topics.CommandDown),
                "topic_event_up=" + (Topics == null ? string.Empty : Topics.EventUp),
                "topic_ota_down=" + (Topics == null ? string.Empty : Topics.OtaDown),
                "topic_debug_up=" + (Topics == null ? string.Empty : Topics.DebugUp),
                "topic_status=" + (Topics == null ? string.Empty : Topics.Status),
                "extra_subscriptions=" + (ExtraSubscriptions ?? string.Empty).Replace("\r", "\\r").Replace("\n", "\\n"),
                "auto_follow_latest=" + AutoFollowLatest.ToString(CultureInfo.InvariantCulture).ToLowerInvariant()
            };
            File.WriteAllLines(SettingsPath, lines);
        }

        private static DeviceEmulatorSettings CreateDefault()
        {
            return new DeviceEmulatorSettings
            {
                Host = "39.103.154.108",
                Language = "zh-CN",
                Port = 1883,
                ClientId = "GM400-452089-device-emulator",
                Username = "GM400-452089",
                Password = "public",
                Qos = 0,
                UseTls = false,
                ConnectTimeoutSeconds = 6,
                DeviceId = "GM400-452089",
                FirmwareVersion = "csharp-device-emulator-20260702-status",
                Topics = TopicSet.CreateServerContractDefault("GM400-452089"),
                ExtraSubscriptions = TopicSet.StatusWildcard,
                AutoFollowLatest = false
            };
        }

        private void ApplyValue(string key, string value)
        {
            if (Topics == null)
                Topics = TopicSet.CreateDefault();
            if (key == "language")
                Language = UiText.Code(UiText.Parse(value));
            else if (key == "host")
                Host = value;
            else if (key == "port")
            {
                ushort port;
                if (ushort.TryParse(value, out port) && port > 0)
                    Port = port;
            }
            else if (key == "client_id")
                ClientId = value;
            else if (key == "username")
                Username = value;
            else if (key == "password")
                Password = value;
            else if (key == "qos")
            {
                int qos;
                if (int.TryParse(value, out qos))
                    Qos = Math.Max(0, Math.Min(2, qos));
            }
            else if (key == "tls")
                UseTls = ParseBool(value, UseTls);
            else if (key == "connect_timeout_seconds")
            {
                int timeout;
                if (int.TryParse(value, out timeout))
                    ConnectTimeoutSeconds = Math.Max(2, Math.Min(30, timeout));
            }
            else if (key == "device_id")
                DeviceId = value;
            else if (key == "firmware_version")
                FirmwareVersion = value;
            else if (key == "topic_telemetry_up")
                Topics.TelemetryUp = value;
            else if (key == "topic_cmd_down")
                Topics.CommandDown = value;
            else if (key == "topic_event_up")
                Topics.EventUp = value;
            else if (key == "topic_ota_down")
                Topics.OtaDown = value;
            else if (key == "topic_debug_up")
                Topics.DebugUp = value;
            else if (key == "topic_status")
                Topics.Status = value;
            else if (key == "extra_subscriptions")
                ExtraSubscriptions = value.Replace("\\r", "\r").Replace("\\n", "\n");
            else if (key == "auto_follow_latest")
                AutoFollowLatest = ParseBool(value, AutoFollowLatest);
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
