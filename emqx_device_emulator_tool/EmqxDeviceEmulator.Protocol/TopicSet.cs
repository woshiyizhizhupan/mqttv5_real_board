using System;

namespace EmqxDeviceEmulator.Protocol
{
    public sealed class TopicSet
    {
        public const string CurrentDeviceName = "GM400-452089";
        public const string RequestPrefix = "v1/devices/request/";
        public const string ResponsePrefix = "v1/devices/response/";
        public const string StatusPrefix = "v1/devices/status/";
        public const string StatusWildcard = "v1/devices/status/+";

        public string TelemetryUp { get; set; }
        public string CommandDown { get; set; }
        public string EventUp { get; set; }
        public string OtaDown { get; set; }
        public string DebugUp { get; set; }
        public string Status { get; set; }

        public static TopicSet CreateDefault()
        {
            return CreateServerContractDefault(CurrentDeviceName);
        }

        public static TopicSet CreateServerContractDefault(string deviceName)
        {
            return new TopicSet
            {
                TelemetryUp = ResponseTopicFor(deviceName),
                CommandDown = RequestTopicFor(deviceName),
                EventUp = ResponseTopicFor(deviceName),
                OtaDown = RequestTopicFor(deviceName),
                DebugUp = ResponseTopicFor(deviceName),
                Status = StatusTopicFor(deviceName)
            };
        }

        public static string RequestTopicFor(string deviceName)
        {
            return RequestPrefix + NormalizeDeviceName(deviceName);
        }

        public static string ResponseTopicFor(string deviceName)
        {
            return ResponsePrefix + NormalizeDeviceName(deviceName);
        }

        public static string StatusTopicFor(string deviceName)
        {
            return StatusPrefix + NormalizeDeviceName(deviceName);
        }

        public string Classify(string topic)
        {
            string value = topic ?? string.Empty;
            if (EqualsTopic(value, Status) || value.StartsWith(StatusPrefix, StringComparison.Ordinal))
                return "status";
            if (EqualsTopic(value, TelemetryUp))
                return "telemetry_up";
            if (EqualsTopic(value, CommandDown))
                return "cmd_down";
            if (EqualsTopic(value, EventUp))
                return "event_up";
            if (EqualsTopic(value, OtaDown))
                return "ota_down";
            if (EqualsTopic(value, DebugUp))
                return "debug_up";
            if (value.StartsWith(RequestPrefix, StringComparison.Ordinal))
                return "cmd_down";
            if (value.StartsWith(ResponsePrefix, StringComparison.Ordinal))
                return "telemetry_up";
            if (value.EndsWith("/get", StringComparison.OrdinalIgnoreCase))
                return "cmd_down";
            if (value.EndsWith("/ota", StringComparison.OrdinalIgnoreCase))
                return "ota_down";
            if (value.EndsWith("/event", StringComparison.OrdinalIgnoreCase))
                return "event_up";
            if (value.EndsWith("/debug", StringComparison.OrdinalIgnoreCase))
                return "debug_up";
            if (value.EndsWith("/", StringComparison.Ordinal))
                return "telemetry_up";
            return "unknown";
        }

        private static string NormalizeDeviceName(string deviceName)
        {
            return string.IsNullOrWhiteSpace(deviceName) ? CurrentDeviceName : deviceName.Trim();
        }

        private static bool EqualsTopic(string left, string right)
        {
            return string.Equals((left ?? string.Empty).Trim(), (right ?? string.Empty).Trim(), StringComparison.Ordinal);
        }
    }
}
