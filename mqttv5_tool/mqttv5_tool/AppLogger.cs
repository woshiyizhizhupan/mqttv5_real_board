using System;
using System.IO;
using System.Text;

namespace mqttv5_tool
{
    internal static class AppLogger
    {
        private static readonly object SyncRoot = new object();

        public static string LogDirectory
        {
            get
            {
                return Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "logs");
            }
        }

        public static string LogFilePath
        {
            get
            {
                return Path.Combine(LogDirectory, "host_tool_" + DateTime.Now.ToString("yyyyMMdd") + ".log");
            }
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

        private static void Write(string level, string message, Exception exception)
        {
            try
            {
                lock (SyncRoot)
                {
                    Directory.CreateDirectory(LogDirectory);
                    var builder = new StringBuilder();
                    builder.Append(DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff"));
                    builder.Append(" [");
                    builder.Append(level);
                    builder.Append("] ");
                    builder.AppendLine(message ?? string.Empty);
                    if (exception != null)
                    {
                        builder.AppendLine(exception.GetType().FullName + ": " + exception.Message);
                        builder.AppendLine(exception.StackTrace ?? string.Empty);
                    }
                    File.AppendAllText(LogFilePath, builder.ToString(), Encoding.UTF8);
                }
            }
            catch
            {
                // Logging must never block field operation.
            }
        }
    }
}
