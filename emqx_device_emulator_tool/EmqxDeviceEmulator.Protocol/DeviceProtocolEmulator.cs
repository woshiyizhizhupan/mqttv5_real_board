using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace EmqxDeviceEmulator.Protocol
{
    public sealed class DeviceProtocolEmulator
    {
        public TopicSet Topics { get; private set; }
        public string DeviceId { get; private set; }
        public string FirmwareVersion { get; private set; }
        public OtaSessionState Ota { get; private set; }

        public DeviceProtocolEmulator(TopicSet topics, string deviceId, string firmwareVersion)
        {
            Topics = topics ?? TopicSet.CreateDefault();
            DeviceId = string.IsNullOrWhiteSpace(deviceId) ? "GM400-SIM" : deviceId;
            FirmwareVersion = string.IsNullOrWhiteSpace(firmwareVersion) ? "emqx-device-emulator-csharp" : firmwareVersion;
            Ota = new OtaSessionState();
        }

        public DeviceProcessingResult ProcessIncoming(string topic, byte[] payload)
        {
            MessageRecord received = PayloadDecoder.Decode(topic, payload, Topics);
            received.Direction = "RX";
            DeviceProcessingResult result = new DeviceProcessingResult { Received = received };

            string responseId;
            string responseCommand;
            CommandExecution execution = Execute(received, out responseId, out responseCommand);
            result.ResponseCode = execution.Code;
            result.ResponseMessage = execution.Message;
            if (execution.Handled)
            {
                string responsePayload = BuildResponse(responseId, responseCommand, execution);
                result.Responses.Add(new OutgoingMessage
                {
                    Topic = Topics.EventUp,
                    TopicKey = "event_up",
                    PayloadText = responsePayload
                });
            }
            return result;
        }

        public MessageRecord BuildTxRecord(OutgoingMessage message)
        {
            MessageRecord record = PayloadDecoder.Decode(message == null ? string.Empty : message.Topic, Encoding.UTF8.GetBytes(message == null ? string.Empty : message.PayloadText), Topics);
            record.Direction = "TX";
            return record;
        }

        private CommandExecution Execute(MessageRecord record, out string responseId, out string responseCommand)
        {
            responseId = record == null ? string.Empty : record.Id;
            responseCommand = record == null || string.IsNullOrWhiteSpace(record.Command) ? "unknown" : record.Command;
            if (record != null && record.IsEnvelope)
            {
                CommandExecution envelopeExecution = ExecuteEnvelope(record, out responseId, out responseCommand);
                if (envelopeExecution != null)
                    return envelopeExecution;
            }

            if (record.LegacyFrame != null && (string.Equals(record.Command, "legacy_frame", StringComparison.Ordinal) || record.LegacyFrame.CommandType >= 0xB0))
                return Ota.HandleLegacy(record.LegacyFrame.CommandType, record.LegacyFrame.Payload);

            if (!record.IsJson || record.Json == null)
                return new CommandExecution { Handled = true, Ok = false, Code = 400, Message = "invalid JSON", Data = new Dictionary<string, object> { { "raw_hex", record.RawHex }, { "raw_text", record.RawText } } };

            string cmd = record.Command ?? string.Empty;
            if (string.Equals(cmd, "ping", StringComparison.Ordinal))
                return new CommandExecution { Handled = true, Ok = true, Code = 0, Message = "ok", Data = new Dictionary<string, object> { { "pong", true }, { "device_id", DeviceId } } };

            if (string.Equals(cmd, "get_status", StringComparison.Ordinal))
                return new CommandExecution { Handled = true, Ok = true, Code = 0, Message = "ok", Data = BuildStatusData() };

            if (string.Equals(cmd, "get_config", StringComparison.Ordinal))
                return new CommandExecution { Handled = true, Ok = true, Code = 0, Message = "ok", Data = BuildConfigData() };

            if (string.Equals(cmd, "set_config", StringComparison.Ordinal))
                return new CommandExecution { Handled = true, Ok = true, Code = 0, Message = "configuration accepted by emulator", Data = new Dictionary<string, object> { { "restart_required", true }, { "emulator_only", true } } };

            if (string.Equals(cmd, "reboot", StringComparison.Ordinal))
                return new CommandExecution { Handled = true, Ok = true, Code = 0, Message = "reboot scheduled by emulator", Data = new Dictionary<string, object> { { "reboot_scheduled", true } }, RebootRequested = true };

            if (cmd.StartsWith("ota_", StringComparison.Ordinal))
                return Ota.HandleJsonCommand(cmd, record.Json);

            return new CommandExecution { Handled = true, Ok = true, Code = 0, Message = "unsupported command received by emulator", Data = new Dictionary<string, object> { { "received", true }, { "cmd", cmd } } };
        }

        private CommandExecution ExecuteEnvelope(MessageRecord record, out string responseId, out string responseCommand)
        {
            responseId = string.IsNullOrWhiteSpace(record.InnerId) ? record.Id : record.InnerId;
            responseCommand = string.IsNullOrWhiteSpace(record.InnerCommand) ? "mqtt_envelope" : record.InnerCommand;

            if (record.InnerJson != null && !string.IsNullOrWhiteSpace(record.InnerCommand))
            {
                MessageRecord inner = new MessageRecord
                {
                    Time = record.Time,
                    Direction = record.Direction,
                    Topic = record.Topic,
                    TopicKey = record.TopicKey,
                    Id = record.InnerId,
                    Command = record.InnerCommand,
                    RawText = record.EnvelopePayloadText,
                    RawHex = record.EnvelopePayloadHex,
                    IsJson = true,
                    Json = record.InnerJson,
                    FormattedJson = JsonUtil.Format(record.EnvelopePayloadText)
                };
                CommandExecution innerExecution = ExecutePlainCommand(inner);
                innerExecution.Data["envelope"] = BuildEnvelopeData(record);
                return innerExecution;
            }

            return new CommandExecution
            {
                Handled = true,
                Ok = true,
                Code = 0,
                Message = "mqtt envelope payload received",
                Data = BuildEnvelopeData(record)
            };
        }

        private CommandExecution ExecutePlainCommand(MessageRecord record)
        {
            if (record.LegacyFrame != null && (string.Equals(record.Command, "legacy_frame", StringComparison.Ordinal) || record.LegacyFrame.CommandType >= 0xB0))
                return Ota.HandleLegacy(record.LegacyFrame.CommandType, record.LegacyFrame.Payload);

            if (!record.IsJson || record.Json == null)
                return new CommandExecution { Handled = true, Ok = false, Code = 400, Message = "invalid JSON", Data = new Dictionary<string, object> { { "raw_hex", record.RawHex }, { "raw_text", record.RawText } } };

            string cmd = record.Command ?? string.Empty;
            if (string.Equals(cmd, "ping", StringComparison.Ordinal))
                return new CommandExecution { Handled = true, Ok = true, Code = 0, Message = "ok", Data = new Dictionary<string, object> { { "pong", true }, { "device_id", DeviceId } } };

            if (string.Equals(cmd, "get_status", StringComparison.Ordinal))
                return new CommandExecution { Handled = true, Ok = true, Code = 0, Message = "ok", Data = BuildStatusData() };

            if (string.Equals(cmd, "get_config", StringComparison.Ordinal))
                return new CommandExecution { Handled = true, Ok = true, Code = 0, Message = "ok", Data = BuildConfigData() };

            if (string.Equals(cmd, "set_config", StringComparison.Ordinal))
                return new CommandExecution { Handled = true, Ok = true, Code = 0, Message = "configuration accepted by emulator", Data = new Dictionary<string, object> { { "restart_required", true }, { "emulator_only", true } } };

            if (string.Equals(cmd, "reboot", StringComparison.Ordinal))
                return new CommandExecution { Handled = true, Ok = true, Code = 0, Message = "reboot scheduled by emulator", Data = new Dictionary<string, object> { { "reboot_scheduled", true } }, RebootRequested = true };

            if (cmd.StartsWith("ota_", StringComparison.Ordinal))
                return Ota.HandleJsonCommand(cmd, record.Json);

            return new CommandExecution { Handled = true, Ok = true, Code = 0, Message = "unsupported command received by emulator", Data = new Dictionary<string, object> { { "received", true }, { "cmd", cmd } } };
        }

        private Dictionary<string, object> BuildEnvelopeData(MessageRecord record)
        {
            return new Dictionary<string, object>
            {
                { "envelope", true },
                { "message_id", JsonUtil.GetString(record.Json, "messageId") },
                { "qos", JsonUtil.GetString(record.Json, "qos") },
                { "retained", JsonUtil.GetString(record.Json, "retained") },
                { "dup", JsonUtil.GetString(record.Json, "dup") },
                { "payload_text", record.EnvelopePayloadText ?? string.Empty },
                { "payload_hex", record.EnvelopePayloadHex ?? string.Empty },
                { "inner_cmd", record.InnerCommand ?? string.Empty }
            };
        }

        private Dictionary<string, object> BuildStatusData()
        {
            return new Dictionary<string, object>
            {
                { "device_id", DeviceId },
                { "firmware_version", FirmwareVersion },
                { "mqtt_state", "connected" },
                { "business_mode", "device_emulator" },
                { "meter_hlw8112", new Dictionary<string, object> { { "simulated", true }, { "voltage_mv", 220000 }, { "current_ma", 1200 }, { "power_mw", 260000 } } },
                { "environment", new Dictionary<string, object> { { "simulated", true }, { "temperature_c_x10", 258 }, { "humidity_rh_x10", 621 } } },
                { "rs485", new Dictionary<string, object> { { "simulated", true }, { "online", true }, { "device_count", 1 } } },
                { "ota", Ota.StatusData() }
            };
        }

        private Dictionary<string, object> BuildConfigData()
        {
            return new Dictionary<string, object>
            {
                { "device_id", DeviceId },
                { "firmware_version", FirmwareVersion },
                { "topics", new Dictionary<string, object>
                    {
                        { "telemetry_up", Topics.TelemetryUp },
                        { "cmd_down", Topics.CommandDown },
                        { "event_up", Topics.EventUp },
                        { "ota_down", Topics.OtaDown },
                        { "debug_up", Topics.DebugUp },
                        { "status", Topics.Status }
                    }
                },
                { "emulator_only", true }
            };
        }

        private string BuildResponse(string id, string cmd, CommandExecution execution)
        {
            Dictionary<string, object> payload = new Dictionary<string, object>
            {
                { "schema", "emqx-gateway.response.v1" },
                { "id", string.IsNullOrWhiteSpace(id) ? "emulator-" + DateTime.Now.ToString("yyyyMMddHHmmssfff", CultureInfo.InvariantCulture) : id },
                { "cmd", cmd ?? string.Empty },
                { "ok", execution.Ok },
                { "code", execution.Code },
                { "message", execution.Message ?? string.Empty },
                { "device_id", DeviceId },
                { "firmware_version", FirmwareVersion },
                { "ts", DateTime.Now.ToString("yyyy-MM-ddTHH:mm:ss.fffK", CultureInfo.InvariantCulture) },
                { "data", execution.Data ?? new Dictionary<string, object>() }
            };
            return JsonUtil.Serialize(payload);
        }
    }
}
