using System;
using System.Collections.Generic;
using System.Text;

namespace EmqxDeviceEmulator.Protocol
{
    public static class PayloadDecoder
    {
        public static MessageRecord Decode(string topic, byte[] payload)
        {
            return Decode(topic, payload, null);
        }

        public static MessageRecord Decode(string topic, byte[] payload, TopicSet topics)
        {
            byte[] bytes = payload ?? new byte[0];
            string rawText = Encoding.UTF8.GetString(bytes);
            Dictionary<string, object> json = JsonUtil.TryParseObject(rawText);
            MessageRecord record = new MessageRecord();
            record.Time = System.DateTime.Now;
            record.Topic = topic ?? string.Empty;
            record.TopicKey = topics == null ? TopicSet.CreateDefault().Classify(topic) : topics.Classify(topic);
            record.RawText = rawText;
            record.RawHex = LegacyFrame.Hex(bytes);
            record.IsJson = json != null;
            record.Json = json;
            record.FormattedJson = JsonUtil.Format(rawText);
            if (json != null)
            {
                record.Id = JsonUtil.GetString(json, "id");
                record.Schema = JsonUtil.GetString(json, "schema");
                record.Command = JsonUtil.GetString(json, "cmd", "command", "data.cmd");
                record.EventName = JsonUtil.GetString(json, "event", "event_name", "data.event");
                record.Fields.AddRange(JsonUtil.BuildFields(json));
            }

            record.LegacyFrame = DecodeLegacyFrame(json, bytes);
            DecodeEnvelope(record, json);
            if (record.LegacyFrame != null && string.IsNullOrWhiteSpace(record.Command))
                record.Command = "legacy_frame";
            if (record.IsEnvelope && string.IsNullOrWhiteSpace(record.Command))
                record.Command = string.IsNullOrWhiteSpace(record.InnerCommand) ? "mqtt_envelope" : record.InnerCommand;
            record.Summary = BuildSummary(record);
            record.Meaning = BuildMeaning(record);
            return record;
        }

        private static void DecodeEnvelope(MessageRecord record, Dictionary<string, object> json)
        {
            if (record == null || json == null)
                return;
            if (!string.IsNullOrWhiteSpace(record.Command) || record.LegacyFrame != null)
                return;

            string encodedPayload = JsonUtil.GetString(json, "payload");
            if (string.IsNullOrWhiteSpace(encodedPayload))
                return;

            byte[] hexPayload;
            if (LegacyFrame.TryDecodeHex(encodedPayload, out hexPayload))
            {
                record.IsEnvelope = true;
                record.EnvelopePayloadText = Encoding.UTF8.GetString(hexPayload);
                record.EnvelopePayloadHex = LegacyFrame.Hex(hexPayload);
                record.Fields.Add(new FieldRow { Path = "payload_decoded_hex", Value = record.EnvelopePayloadHex, Meaning = "payload字段按十六进制解码后的原始业务数据" });
                return;
            }

            byte[] decoded;
            try
            {
                decoded = Convert.FromBase64String(encodedPayload);
            }
            catch
            {
                return;
            }

            record.IsEnvelope = true;
            record.EnvelopePayloadText = Encoding.UTF8.GetString(decoded);
            record.EnvelopePayloadHex = LegacyFrame.Hex(decoded);
            record.InnerJson = JsonUtil.TryParseObject(record.EnvelopePayloadText);
            if (record.InnerJson != null)
            {
                record.InnerId = JsonUtil.GetString(record.InnerJson, "id");
                record.InnerCommand = JsonUtil.GetString(record.InnerJson, "cmd", "command", "data.cmd");
            }
            record.Fields.Add(new FieldRow { Path = "payload_decoded_text", Value = record.EnvelopePayloadText, Meaning = "payload字段Base64解码后的文本" });
            record.Fields.Add(new FieldRow { Path = "payload_decoded_hex", Value = record.EnvelopePayloadHex, Meaning = "payload字段Base64解码后的十六进制" });
            if (!string.IsNullOrWhiteSpace(record.InnerCommand))
                record.Fields.Add(new FieldRow { Path = "payload_decoded_cmd", Value = record.InnerCommand, Meaning = "payload内部业务命令" });
        }

        private static LegacyFrameInfo DecodeLegacyFrame(Dictionary<string, object> json, byte[] rawPayload)
        {
            byte[] frameBytes;
            string frameText = string.Empty;
            if (json != null)
            {
                frameText = JsonUtil.GetString(json, "payload", "data.payload");
                if (LegacyFrame.TryDecodeHex(frameText, out frameBytes))
                {
                    LegacyFrameInfo info;
                    if (LegacyFrame.TryParseAny(frameBytes, out info))
                        return info;
                }

                frameText = JsonUtil.GetString(json, "frame", "frame_hex", "data.frame", "data.frame_hex");
                if (LegacyFrame.TryDecodeTextFrame(frameText, out frameBytes))
                {
                    LegacyFrameInfo info;
                    if (LegacyFrame.TryParseAny(frameBytes, out info))
                        return info;
                }
            }

            if (rawPayload != null && rawPayload.Length > 0 && rawPayload[0] == 0xFE)
            {
                LegacyFrameInfo info;
                if (LegacyFrame.TryParseAny(rawPayload, out info))
                    return info;
            }
            return null;
        }

        private static string BuildSummary(MessageRecord record)
        {
            if (record.LegacyFrame != null)
                return "legacy " + record.LegacyFrame.CommandHex + " " + record.LegacyFrame.Summary;
            if (record.IsEnvelope)
                return string.IsNullOrWhiteSpace(record.InnerCommand) ? "MQTT envelope payload" : "MQTT envelope -> cmd " + record.InnerCommand;
            if (!string.IsNullOrWhiteSpace(record.Schema))
                return record.Schema;
            if (!string.IsNullOrWhiteSpace(record.Command))
                return "cmd " + record.Command;
            if (record.IsJson)
                return "JSON报文";
            return "非JSON原始报文";
        }

        private static string BuildMeaning(MessageRecord record)
        {
            if (record.LegacyFrame != null)
                return record.LegacyFrame.Summary + " 原始帧=" + record.LegacyFrame.RawHex + "，业务载荷=" + record.LegacyFrame.PayloadHex + "。";
            if (record.IsEnvelope)
                return "MQTT客户端/EMQX envelope报文：payload字段已解码。解码文本=" + record.EnvelopePayloadText + "，HEX=" + record.EnvelopePayloadHex + "。";
            if (record.TopicKey == "status")
                return "设备上下线状态主题：online由设备连接成功后主动发布，offline由EMQX根据设备CONNECT时登记的遗嘱消息发布。";
            if (record.TopicKey == "cmd_down")
                return "设备下行/get主题收到的控制或查询命令。命令=" + (record.Command ?? string.Empty) + "。";
            if (record.TopicKey == "ota_down")
                return "设备OTA主题收到的升级命令或旧业务升级帧。命令=" + (record.Command ?? string.Empty) + "。";
            if (record.TopicKey == "event_up")
                return "事件/响应主题报文。";
            if (record.TopicKey == "debug_up")
                return "调试/兼容主题报文。";
            if (record.TopicKey == "telemetry_up")
                return "遥测上行主题报文。";
            return "未分类MQTT报文。";
        }
    }
}
