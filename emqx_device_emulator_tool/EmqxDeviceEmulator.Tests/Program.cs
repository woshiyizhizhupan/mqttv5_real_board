using System;
using System.Linq;
using System.Text;
using EmqxDeviceEmulator.Protocol;

namespace EmqxDeviceEmulator.Tests
{
    internal static class Program
    {
        private static int Main()
        {
            Run("decode_preserves_raw_text_and_hex", DecodePreservesRawTextAndHex);
            Run("ping_builds_board_like_response", PingBuildsBoardLikeResponse);
            Run("emqx_envelope_plain_payload_is_acknowledged", EmqxEnvelopePlainPayloadIsAcknowledged);
            Run("emqx_envelope_json_payload_is_unwrapped_and_executed", EmqxEnvelopeJsonPayloadIsUnwrappedAndExecuted);
            Run("server_payload_hex_legacy_frame_is_parsed_before_base64", ServerPayloadHexLegacyFrameIsParsedBeforeBase64);
            Run("status_topic_is_classified", StatusTopicIsClassified);
            Run("legacy_b1_accepts_both_file_info_orders", LegacyB1AcceptsBothFileInfoOrders);
            Run("legacy_ota_tracks_512_byte_chunks_and_completion", LegacyOtaTracks512ByteChunksAndCompletion);
            Console.WriteLine("All protocol tests passed.");
            return 0;
        }

        private static void DecodePreservesRawTextAndHex()
        {
            MessageRecord record = PayloadDecoder.Decode("city/tjw/pole/pole001/device/GM400/get", Encoding.UTF8.GetBytes("{\"cmd\":\"ping\"}"));
            AssertEqual("{\"cmd\":\"ping\"}", record.RawText, "raw text");
            AssertEqual("7B 22 63 6D 64 22 3A 22 70 69 6E 67 22 7D", record.RawHex, "raw hex");
            AssertEqual("ping", record.Command, "command");
        }

        private static void PingBuildsBoardLikeResponse()
        {
            TopicSet topics = TestTopics();
            DeviceProtocolEmulator emulator = new DeviceProtocolEmulator(topics, "GM400-TEST", "emulator-test-fw");
            DeviceProcessingResult result = emulator.ProcessIncoming(topics.CommandDown, Encoding.UTF8.GetBytes("{\"id\":\"t1\",\"cmd\":\"ping\"}"));

            AssertEqual("cmd_down", result.Received.TopicKey, "topic key");
            AssertEqual("ping", result.Received.Command, "command");
            AssertEqual(1, result.Responses.Count, "response count");
            AssertEqual(topics.EventUp, result.Responses[0].Topic, "response topic");
            AssertContains(result.Responses[0].PayloadText, "\"schema\":\"emqx-gateway.response.v1\"", "response schema");
            AssertContains(result.Responses[0].PayloadText, "\"cmd\":\"ping\"", "response cmd");
            AssertContains(result.Responses[0].PayloadText, "\"ok\":true", "response ok");
        }

        private static void EmqxEnvelopePlainPayloadIsAcknowledged()
        {
            TopicSet topics = TestTopics();
            DeviceProtocolEmulator emulator = new DeviceProtocolEmulator(topics, "GM400-TEST", "emulator-test-fw");
            string envelope = "{\"mutable\":true,\"payload\":\"SGVsbG8gV29ybGQ=\",\"qos\":1,\"retained\":false,\"dup\":false,\"messageId\":12345,\"properties\":{\"property1\":\"value1\",\"property2\":42}}";

            DeviceProcessingResult result = emulator.ProcessIncoming(topics.CommandDown, Encoding.UTF8.GetBytes(envelope));

            AssertEqual(true, result.Received.IsEnvelope, "is envelope");
            AssertEqual("Hello World", result.Received.EnvelopePayloadText, "decoded payload text");
            AssertEqual(0, result.ResponseCode, "response code");
            AssertContains(result.Responses[0].PayloadText, "\"cmd\":\"mqtt_envelope\"", "response cmd");
            AssertContains(result.Responses[0].PayloadText, "\"ok\":true", "response ok");
            AssertContains(result.Responses[0].PayloadText, "\"payload_text\":\"Hello World\"", "decoded payload in response");
        }

        private static void EmqxEnvelopeJsonPayloadIsUnwrappedAndExecuted()
        {
            TopicSet topics = TestTopics();
            DeviceProtocolEmulator emulator = new DeviceProtocolEmulator(topics, "GM400-TEST", "emulator-test-fw");
            string inner = Convert.ToBase64String(Encoding.UTF8.GetBytes("{\"id\":\"inner-ping\",\"cmd\":\"ping\"}"));
            string envelope = "{\"mutable\":true,\"payload\":\"" + inner + "\",\"qos\":1,\"retained\":false,\"dup\":false,\"messageId\":12345}";

            DeviceProcessingResult result = emulator.ProcessIncoming(topics.CommandDown, Encoding.UTF8.GetBytes(envelope));

            AssertEqual(true, result.Received.IsEnvelope, "is envelope");
            AssertEqual("ping", result.Received.InnerCommand, "inner command");
            AssertEqual(0, result.ResponseCode, "response code");
            AssertContains(result.Responses[0].PayloadText, "\"id\":\"inner-ping\"", "inner id used");
            AssertContains(result.Responses[0].PayloadText, "\"cmd\":\"ping\"", "inner cmd used");
            AssertContains(result.Responses[0].PayloadText, "\"ok\":true", "response ok");
        }

        private static void ServerPayloadHexLegacyFrameIsParsedBeforeBase64()
        {
            TopicSet topics = TestTopics();
            DeviceProtocolEmulator emulator = new DeviceProtocolEmulator(topics, "GM400-TEST", "emulator-test-fw");
            byte[] frame = LegacyFrame.BuildV2OtaRequest(0xB0, new byte[0]);
            string json = "{\"connectType\":\"1\",\"msgType\":\"1\",\"payload\":\"" + LegacyFrame.Hex(frame).Replace(" ", string.Empty) + "\",\"timestamp\":\"1782916895695\"}";

            DeviceProcessingResult result = emulator.ProcessIncoming(topics.CommandDown, Encoding.UTF8.GetBytes(json));

            AssertTrue(result.Received.LegacyFrame != null, "payload hex legacy frame decoded");
            AssertEqual("B0", result.Received.LegacyFrame.CommandHex, "legacy command");
            AssertEqual(0, result.ResponseCode, "response code");
            AssertEqual(topics.EventUp, result.Responses[0].Topic, "response topic");
            LegacyFrameInfo response = ResponseFrame(result);
            AssertEqual("B0", response.CommandHex, "legacy response command");
        }

        private static void StatusTopicIsClassified()
        {
            TopicSet topics = TestTopics();
            MessageRecord record = PayloadDecoder.Decode(topics.Status, Encoding.UTF8.GetBytes("{\"schema\":\"emqx-gateway.status.v1\",\"device_id\":\"GM400-TEST\",\"status\":\"online\",\"reason\":\"mqtt_connected\"}"), topics);

            AssertEqual("status", record.TopicKey, "status topic key");
            AssertContains(record.Meaning, "设备上下线状态主题", "status meaning");
        }

        private static void LegacyB1AcceptsBothFileInfoOrders()
        {
            byte[] image = Enumerable.Repeat((byte)0x5A, 1024).ToArray();
            uint crc = Crc32.Compute(image);

            TopicSet topics = TestTopics();
            DeviceProtocolEmulator firmwareOrder = new DeviceProtocolEmulator(topics, "GM400-TEST", "emulator-test-fw");
            byte[] orderA = Bytes.Concat(U32((uint)image.Length), U16(2), U16(512), U32(crc));
            DeviceProcessingResult resultA = firmwareOrder.ProcessIncoming(topics.OtaDown, LegacyJson("b1-a", 0xB1, orderA));
            AssertEqual(0, resultA.ResponseCode, "firmware order code");
            AssertEqual(512, firmwareOrder.Ota.ChunkSize, "firmware order chunk size");
            AssertEqual(2, firmwareOrder.Ota.ChunkCount, "firmware order chunk count");

            DeviceProtocolEmulator toolOrder = new DeviceProtocolEmulator(topics, "GM400-TEST", "emulator-test-fw");
            byte[] orderB = Bytes.Concat(U32((uint)image.Length), U16(512), U16(2), U32(crc));
            DeviceProcessingResult resultB = toolOrder.ProcessIncoming(topics.OtaDown, LegacyJson("b1-b", 0xB1, orderB));
            AssertEqual(0, resultB.ResponseCode, "tool order code");
            AssertEqual(512, toolOrder.Ota.ChunkSize, "tool order chunk size");
            AssertEqual(2, toolOrder.Ota.ChunkCount, "tool order chunk count");
        }

        private static void LegacyOtaTracks512ByteChunksAndCompletion()
        {
            byte[] image = Enumerable.Range(0, 1024).Select(i => (byte)(i & 0xFF)).ToArray();
            uint crc = Crc32.Compute(image);
            TopicSet topics = TestTopics();
            DeviceProtocolEmulator emulator = new DeviceProtocolEmulator(topics, "GM400-TEST", "emulator-test-fw");

            emulator.ProcessIncoming(topics.OtaDown, LegacyJson("b0", 0xB0, new byte[0]));
            DeviceProcessingResult b1 = emulator.ProcessIncoming(topics.OtaDown, LegacyJson("b1", 0xB1, Bytes.Concat(U32((uint)image.Length), U16(512), U16(2), U32(crc))));
            AssertEqual(0, b1.ResponseCode, "b1 code");
            AssertTrue(emulator.Ota.Active, "ota active after b1");

            DeviceProcessingResult b2First = emulator.ProcessIncoming(topics.OtaDown, LegacyJson("b2-0", 0xB2, Bytes.Concat(U16(0), image.Take(512).ToArray())));
            AssertEqual(0, b2First.ResponseCode, "b2 first code");
            AssertEqual(1, emulator.Ota.ReceivedChunkCount, "received first chunk");
            AssertEqual(512U, emulator.Ota.ReceivedBytes, "received bytes after first chunk");

            DeviceProcessingResult b3Missing = emulator.ProcessIncoming(topics.OtaDown, LegacyJson("b3-missing", 0xB3, new byte[0]));
            LegacyFrameInfo missingFrame = ResponseFrame(b3Missing);
            AssertEqual("B3", missingFrame.CommandHex, "b3 missing command");
            AssertEqual("00 01 00", missingFrame.PayloadHex, "b3 missing payload");
            AssertEqual(409, b3Missing.ResponseCode, "b3 missing response code");

            DeviceProcessingResult b2Second = emulator.ProcessIncoming(topics.OtaDown, LegacyJson("b2-1", 0xB2, Bytes.Concat(U16(1), image.Skip(512).Take(512).ToArray())));
            AssertEqual(0, b2Second.ResponseCode, "b2 second code");
            AssertEqual(2, emulator.Ota.ReceivedChunkCount, "received all chunks");
            AssertEqual(1024U, emulator.Ota.ReceivedBytes, "received bytes after all chunks");

            DeviceProcessingResult b3Complete = emulator.ProcessIncoming(topics.OtaDown, LegacyJson("b3-complete", 0xB3, new byte[0]));
            LegacyFrameInfo completeFrame = ResponseFrame(b3Complete);
            AssertEqual("FF FF 01", completeFrame.PayloadHex, "b3 complete payload");
            AssertEqual(0, b3Complete.ResponseCode, "b3 complete response code");

            DeviceProcessingResult b4 = emulator.ProcessIncoming(topics.OtaDown, LegacyJson("b4", 0xB4, new byte[0]));
            LegacyFrameInfo endFrame = ResponseFrame(b4);
            AssertEqual("01", endFrame.PayloadHex, "b4 payload");
            AssertTrue(emulator.Ota.RebootRequested, "reboot requested");
        }

        private static LegacyFrameInfo ResponseFrame(DeviceProcessingResult result)
        {
            AssertEqual(1, result.Responses.Count, "legacy response count");
            MessageRecord record = PayloadDecoder.Decode(result.Responses[0].Topic, Encoding.UTF8.GetBytes(result.Responses[0].PayloadText));
            AssertTrue(record.LegacyFrame != null, "legacy response frame decoded");
            return record.LegacyFrame;
        }

        private static byte[] LegacyJson(string id, byte cmd, byte[] payload)
        {
            byte[] frame = LegacyFrame.BuildV2OtaRequest(cmd, payload);
            string json = "{\"id\":\"" + id + "\",\"cmd\":\"legacy_frame\",\"frame\":\"" + Convert.ToBase64String(frame) + "\"}";
            return Encoding.UTF8.GetBytes(json);
        }

        private static TopicSet TestTopics()
        {
            return new TopicSet
            {
                TelemetryUp = "city/tjw/pole/pole001/device/GM400/",
                CommandDown = "city/tjw/pole/pole001/device/GM400/get",
                EventUp = "city/tjw/pole/pole001/device/GM400/event",
                OtaDown = "city/tjw/pole/pole001/device/GM400/ota",
                DebugUp = "city/tjw/pole/pole001/device/GM400/debug",
                Status = "v1/devices/status/GM400"
            };
        }

        private static byte[] U16(int value)
        {
            return new[] { (byte)((value >> 8) & 0xFF), (byte)(value & 0xFF) };
        }

        private static byte[] U32(uint value)
        {
            return new[] { (byte)((value >> 24) & 0xFF), (byte)((value >> 16) & 0xFF), (byte)((value >> 8) & 0xFF), (byte)(value & 0xFF) };
        }

        private static void Run(string name, Action test)
        {
            try
            {
                test();
                Console.WriteLine("PASS " + name);
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("FAIL " + name + ": " + ex.Message);
                Environment.Exit(1);
            }
        }

        private static void AssertEqual<T>(T expected, T actual, string label)
        {
            if (!object.Equals(expected, actual))
                throw new InvalidOperationException(label + " expected <" + expected + "> but got <" + actual + ">");
        }

        private static void AssertContains(string haystack, string needle, string label)
        {
            if (haystack == null || haystack.IndexOf(needle, StringComparison.Ordinal) < 0)
                throw new InvalidOperationException(label + " expected to contain <" + needle + "> but got <" + haystack + ">");
        }

        private static void AssertTrue(bool condition, string label)
        {
            if (!condition)
                throw new InvalidOperationException(label + " expected true");
        }
    }

    internal static class Bytes
    {
        public static byte[] Concat(params byte[][] parts)
        {
            int length = parts.Sum(part => part.Length);
            byte[] result = new byte[length];
            int offset = 0;
            foreach (byte[] part in parts)
            {
                Buffer.BlockCopy(part, 0, result, offset, part.Length);
                offset += part.Length;
            }
            return result;
        }
    }
}
