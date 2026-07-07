using System;
using System.Collections.Generic;

namespace EmqxDeviceEmulator.Protocol
{
    public sealed class FieldRow
    {
        public string Path { get; set; }
        public string Value { get; set; }
        public string Meaning { get; set; }
    }

    public sealed class MessageRecord
    {
        public DateTime Time { get; set; }
        public string Direction { get; set; }
        public string Topic { get; set; }
        public string TopicKey { get; set; }
        public string Id { get; set; }
        public string Schema { get; set; }
        public string Command { get; set; }
        public string EventName { get; set; }
        public string RawText { get; set; }
        public string RawHex { get; set; }
        public string FormattedJson { get; set; }
        public string Summary { get; set; }
        public string Meaning { get; set; }
        public bool IsJson { get; set; }
        public bool IsEnvelope { get; set; }
        public Dictionary<string, object> Json { get; set; }
        public Dictionary<string, object> InnerJson { get; set; }
        public string EnvelopePayloadText { get; set; }
        public string EnvelopePayloadHex { get; set; }
        public string InnerId { get; set; }
        public string InnerCommand { get; set; }
        public LegacyFrameInfo LegacyFrame { get; set; }
        public List<FieldRow> Fields { get; private set; }

        public MessageRecord()
        {
            Time = DateTime.Now;
            Direction = string.Empty;
            Topic = string.Empty;
            TopicKey = string.Empty;
            Id = string.Empty;
            Schema = string.Empty;
            Command = string.Empty;
            EventName = string.Empty;
            RawText = string.Empty;
            RawHex = string.Empty;
            FormattedJson = string.Empty;
            Summary = string.Empty;
            Meaning = string.Empty;
            EnvelopePayloadText = string.Empty;
            EnvelopePayloadHex = string.Empty;
            InnerId = string.Empty;
            InnerCommand = string.Empty;
            Fields = new List<FieldRow>();
        }
    }

    public sealed class OutgoingMessage
    {
        public string Topic { get; set; }
        public string TopicKey { get; set; }
        public string PayloadText { get; set; }
    }

    public sealed class DeviceProcessingResult
    {
        public MessageRecord Received { get; set; }
        public List<OutgoingMessage> Responses { get; private set; }
        public int ResponseCode { get; set; }
        public string ResponseMessage { get; set; }

        public DeviceProcessingResult()
        {
            Responses = new List<OutgoingMessage>();
            ResponseMessage = string.Empty;
        }
    }

    public sealed class CommandExecution
    {
        public bool Handled { get; set; }
        public bool Ok { get; set; }
        public int Code { get; set; }
        public string Message { get; set; }
        public Dictionary<string, object> Data { get; set; }
        public bool RebootRequested { get; set; }

        public CommandExecution()
        {
            Message = string.Empty;
            Data = new Dictionary<string, object>();
        }
    }
}
