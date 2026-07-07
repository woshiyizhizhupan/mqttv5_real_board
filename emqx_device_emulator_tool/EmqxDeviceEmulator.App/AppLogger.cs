using System;
using System.IO;
using System.Text;

namespace EmqxDeviceEmulator.App
{
    internal static class AppLogger
    {
        private static readonly object SyncRoot = new object();

        public static string LogDirectory
        {
            get { return Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "logs"); }
        }

        public static string TextLogPath
        {
            get { return Path.Combine(LogDirectory, "device_emulator_" + DateTime.Now.ToString("yyyyMMdd") + ".log"); }
        }

        public static string JsonlLogPath
        {
            get { return Path.Combine(LogDirectory, "device_emulator_" + DateTime.Now.ToString("yyyyMMdd") + ".jsonl"); }
        }

        public static void Info(string message)
        {
            Write("INFO", message, null);
        }

        public static void Warn(string message)
        {
            Write("WARN", message, null);
        }

        public static void Error(string message, Exception exception)
        {
            Write("ERROR", message, exception);
        }

        public static void PacketJsonl(string direction, string topic, string topicKey, string command, string payloadText, string rawHex, string result)
        {
            try
            {
                lock (SyncRoot)
                {
                    Directory.CreateDirectory(LogDirectory);
                    string line = "{\"ts\":\"" + Escape(DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff")) +
                        "\",\"direction\":\"" + Escape(direction) +
                        "\",\"topic\":\"" + Escape(topic) +
                        "\",\"topic_key\":\"" + Escape(topicKey) +
                        "\",\"command\":\"" + Escape(command) +
                        "\",\"result\":\"" + Escape(result) +
                        "\",\"payload\":\"" + Escape(payloadText) +
                        "\",\"raw_hex\":\"" + Escape(rawHex) + "\"}";
                    File.AppendAllText(JsonlLogPath, line + Environment.NewLine, Encoding.UTF8);
                }
            }
            catch
            {
            }
        }

        private static void Write(string level, string message, Exception exception)
        {
            try
            {
                lock (SyncRoot)
                {
                    Directory.CreateDirectory(LogDirectory);
                    StringBuilder builder = new StringBuilder();
                    builder.Append(DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff"));
                    builder.Append(" [").Append(level).Append("] ");
                    builder.AppendLine(message ?? string.Empty);
                    if (exception != null)
                    {
                        builder.AppendLine(exception.GetType().FullName + ": " + exception.Message);
                        builder.AppendLine(exception.StackTrace ?? string.Empty);
                    }
                    File.AppendAllText(TextLogPath, builder.ToString(), Encoding.UTF8);
                }
            }
            catch
            {
            }
        }

        private static string Escape(string value)
        {
            return (value ?? string.Empty).Replace("\\", "\\\\").Replace("\"", "\\\"").Replace("\r", "\\r").Replace("\n", "\\n");
        }
    }
}
