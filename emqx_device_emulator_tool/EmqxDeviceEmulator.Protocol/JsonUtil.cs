using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using System.Web.Script.Serialization;

namespace EmqxDeviceEmulator.Protocol
{
    internal static class JsonUtil
    {
        private static readonly JavaScriptSerializer Serializer = new JavaScriptSerializer();

        public static Dictionary<string, object> TryParseObject(string text)
        {
            if (string.IsNullOrWhiteSpace(text))
                return null;
            try
            {
                return Serializer.DeserializeObject(text) as Dictionary<string, object>;
            }
            catch
            {
                return null;
            }
        }

        public static string Serialize(Dictionary<string, object> value)
        {
            return Serializer.Serialize(value ?? new Dictionary<string, object>());
        }

        public static string Format(string text)
        {
            Dictionary<string, object> json = TryParseObject(text);
            if (json == null)
                return string.Empty;
            StringBuilder builder = new StringBuilder();
            WriteValue(builder, json, 0);
            return builder.ToString();
        }

        public static string GetString(Dictionary<string, object> root, params string[] paths)
        {
            object value = GetValue(root, paths);
            if (value == null)
                return string.Empty;
            return Convert.ToString(value, CultureInfo.InvariantCulture);
        }

        public static Dictionary<string, object> GetObject(Dictionary<string, object> root, string path)
        {
            return GetValue(root, new[] { path }) as Dictionary<string, object>;
        }

        public static bool TryGetInt(Dictionary<string, object> root, string path, out int value)
        {
            object raw = GetValue(root, new[] { path });
            if (raw == null)
            {
                value = 0;
                return false;
            }
            try
            {
                value = Convert.ToInt32(raw, CultureInfo.InvariantCulture);
                return true;
            }
            catch
            {
                value = 0;
                return false;
            }
        }

        public static bool TryGetUInt(Dictionary<string, object> root, string path, out uint value)
        {
            object raw = GetValue(root, new[] { path });
            if (raw == null)
            {
                value = 0;
                return false;
            }
            try
            {
                value = Convert.ToUInt32(raw, CultureInfo.InvariantCulture);
                return true;
            }
            catch
            {
                value = 0;
                return false;
            }
        }

        public static object GetValue(Dictionary<string, object> root, params string[] paths)
        {
            if (root == null || paths == null)
                return null;
            foreach (string path in paths)
            {
                object value = GetValueByPath(root, path);
                if (value != null)
                    return value;
            }
            return null;
        }

        public static List<FieldRow> BuildFields(Dictionary<string, object> root)
        {
            List<FieldRow> rows = new List<FieldRow>();
            if (root != null)
                AppendFields(rows, string.Empty, root);
            return rows;
        }

        private static object GetValueByPath(Dictionary<string, object> root, string path)
        {
            if (root == null || string.IsNullOrWhiteSpace(path))
                return null;
            object current = root;
            string[] parts = path.Split('.');
            foreach (string part in parts)
            {
                Dictionary<string, object> dict = current as Dictionary<string, object>;
                if (dict == null || !dict.ContainsKey(part))
                    return null;
                current = dict[part];
            }
            return current;
        }

        private static void AppendFields(List<FieldRow> rows, string prefix, object value)
        {
            Dictionary<string, object> dict = value as Dictionary<string, object>;
            if (dict != null)
            {
                foreach (KeyValuePair<string, object> pair in dict)
                    AppendFields(rows, string.IsNullOrEmpty(prefix) ? pair.Key : prefix + "." + pair.Key, pair.Value);
                return;
            }

            object[] array = value as object[];
            if (array != null)
            {
                for (int i = 0; i < array.Length; i++)
                    AppendFields(rows, prefix + "[" + i.ToString(CultureInfo.InvariantCulture) + "]", array[i]);
                return;
            }

            rows.Add(new FieldRow { Path = prefix, Value = ScalarToString(value), Meaning = ExplainField(prefix) });
        }

        private static string ExplainField(string path)
        {
            switch ((path ?? string.Empty).ToLowerInvariant())
            {
                case "schema": return "协议/报文类型";
                case "id": return "请求或响应ID";
                case "cmd": return "下行命令";
                case "ok": return "执行是否成功";
                case "code": return "执行结果码";
                case "message": return "执行说明";
                case "payload": return "服务端封装的业务载荷；当前兼容十六进制旧业务帧和Base64 envelope";
                case "frame": return "Base64编码的旧业务帧";
                case "data.frame": return "Base64编码的旧业务响应帧";
                case "status": return "设备在线状态，online/offline";
                case "reason": return "状态变化原因";
                case "device_id": return "设备ID";
                case "params.session_id": return "OTA会话ID";
                case "params.file_len": return "OTA文件长度";
                case "params.chunk_size": return "OTA分包大小";
                case "params.chunk_count": return "OTA分包数量";
                case "params.file_crc32": return "OTA整包CRC32";
                case "params.index": return "OTA分包序号";
                case "params.offset": return "OTA分包偏移";
                case "params.chunk_crc32": return "OTA分包CRC32";
                case "params.data": return "Base64编码的OTA分包数据";
                default: return string.Empty;
            }
        }

        private static string ScalarToString(object value)
        {
            if (value == null)
                return string.Empty;
            bool boolValue;
            if (value is bool)
            {
                boolValue = (bool)value;
                return boolValue ? "true" : "false";
            }
            return Convert.ToString(value, CultureInfo.InvariantCulture);
        }

        private static void WriteValue(StringBuilder builder, object value, int indent)
        {
            Dictionary<string, object> dict = value as Dictionary<string, object>;
            if (dict != null)
            {
                builder.AppendLine("{");
                int index = 0;
                foreach (KeyValuePair<string, object> pair in dict)
                {
                    Indent(builder, indent + 2);
                    builder.Append('"').Append(pair.Key).Append("\": ");
                    WriteValue(builder, pair.Value, indent + 2);
                    index++;
                    if (index < dict.Count)
                        builder.Append(',');
                    builder.AppendLine();
                }
                Indent(builder, indent);
                builder.Append('}');
                return;
            }

            IEnumerable enumerable = value as IEnumerable;
            if (enumerable != null && !(value is string))
            {
                builder.Append('[');
                bool first = true;
                foreach (object item in enumerable)
                {
                    if (!first)
                        builder.Append(", ");
                    WriteValue(builder, item, indent);
                    first = false;
                }
                builder.Append(']');
                return;
            }

            if (value is string)
                builder.Append('"').Append(((string)value).Replace("\\", "\\\\").Replace("\"", "\\\"")).Append('"');
            else if (value is bool)
                builder.Append((bool)value ? "true" : "false");
            else if (value == null)
                builder.Append("null");
            else
                builder.Append(Convert.ToString(value, CultureInfo.InvariantCulture));
        }

        private static void Indent(StringBuilder builder, int indent)
        {
            builder.Append(' ', indent);
        }
    }
}
