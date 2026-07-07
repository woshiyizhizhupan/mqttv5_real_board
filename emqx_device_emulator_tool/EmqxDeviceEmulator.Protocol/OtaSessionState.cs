using System;
using System.Collections.Generic;
using System.Globalization;

namespace EmqxDeviceEmulator.Protocol
{
    public sealed class OtaSessionState
    {
        public const int MaxChunkSize = 1024;
        public const int MaxChunks = 1024;

        private bool[] receivedFlags = new bool[0];
        private byte[] imageBuffer = new byte[0];

        public bool Active { get; private set; }
        public bool Complete { get; private set; }
        public bool RebootRequested { get; private set; }
        public string SessionId { get; private set; }
        public uint FileLength { get; private set; }
        public uint FileCrc32 { get; private set; }
        public int ChunkSize { get; private set; }
        public int ChunkCount { get; private set; }
        public uint ReceivedBytes { get; private set; }
        public int ReceivedChunkCount { get; private set; }
        public string LastError { get; private set; }
        public bool BreakUpgrade { get; private set; }
        public bool UpgradeStateSuccess { get; private set; }

        public OtaSessionState()
        {
            Reset();
        }

        public void Reset()
        {
            Active = false;
            Complete = false;
            RebootRequested = false;
            SessionId = string.Empty;
            FileLength = 0;
            FileCrc32 = 0;
            ChunkSize = 0;
            ChunkCount = 0;
            ReceivedBytes = 0;
            ReceivedChunkCount = 0;
            LastError = string.Empty;
            BreakUpgrade = false;
            UpgradeStateSuccess = false;
            receivedFlags = new bool[0];
            imageBuffer = new byte[0];
        }

        public CommandExecution HandleJsonCommand(string cmd, Dictionary<string, object> root)
        {
            if (string.Equals(cmd, "ota_begin", StringComparison.Ordinal))
                return HandleJsonBegin(root);
            if (string.Equals(cmd, "ota_chunk", StringComparison.Ordinal))
                return HandleJsonChunk(root);
            if (string.Equals(cmd, "ota_end", StringComparison.Ordinal))
                return HandleJsonEnd(root);
            if (string.Equals(cmd, "ota_abort", StringComparison.Ordinal))
            {
                BreakUpgrade = true;
                Active = false;
                LastError = "OTA aborted by command";
                return Success("OTA aborted", StatusData());
            }
            if (string.Equals(cmd, "ota_status", StringComparison.Ordinal))
                return Success("ok", StatusData());
            return Error(404, "unknown OTA command", null);
        }

        public CommandExecution HandleLegacy(byte cmdType, byte[] payload)
        {
            switch (cmdType)
            {
                case 0xB0: return HandleLegacyB0();
                case 0xB1: return HandleLegacyB1(payload);
                case 0xB2: return HandleLegacyB2(payload);
                case 0xB3: return HandleLegacyB3();
                case 0xB4: return HandleLegacyB4();
                case 0xB5: return HandleLegacyB5();
                case 0xB6: return HandleLegacyB6();
                default: return BuildLegacyExecution(cmdType, new byte[] { 0 }, 404, "unknown legacy OTA command", "unknown");
            }
        }

        public Dictionary<string, object> StatusData()
        {
            return new Dictionary<string, object>
            {
                { "active", Active },
                { "complete", Complete },
                { "session_id", SessionId },
                { "file_len", FileLength },
                { "file_crc32", FileCrc32 },
                { "chunk_size", ChunkSize },
                { "chunk_count", ChunkCount },
                { "received_bytes", ReceivedBytes },
                { "received_chunk_count", ReceivedChunkCount },
                { "upgrade_state", UpgradeStateSuccess ? 1 : 0 },
                { "break_upgrade", BreakUpgrade },
                { "reboot_requested", RebootRequested },
                { "last_error", LastError }
            };
        }

        private CommandExecution HandleJsonBegin(Dictionary<string, object> root)
        {
            Dictionary<string, object> parameters = JsonUtil.GetObject(root, "params");
            if (parameters == null)
                return Error(422, "ota_begin params are invalid", null);

            string sessionId = JsonUtil.GetString(parameters, "session_id");
            uint fileLen;
            uint fileCrc;
            int chunkSize;
            int chunkCount;
            if (string.IsNullOrWhiteSpace(sessionId) ||
                !JsonUtil.TryGetUInt(parameters, "file_len", out fileLen) ||
                !JsonUtil.TryGetUInt(parameters, "file_crc32", out fileCrc) ||
                !JsonUtil.TryGetInt(parameters, "chunk_size", out chunkSize) ||
                !JsonUtil.TryGetInt(parameters, "chunk_count", out chunkCount))
                return Error(422, "ota_begin params are invalid", null);

            if (!Start(sessionId, fileLen, chunkSize, chunkCount, fileCrc))
                return Error(422, LastError, null);

            return Success("OTA session started", StatusData());
        }

        private CommandExecution HandleJsonChunk(Dictionary<string, object> root)
        {
            Dictionary<string, object> parameters = JsonUtil.GetObject(root, "params");
            if (parameters == null)
                return Error(422, "ota_chunk params are invalid", null);

            string sessionId = JsonUtil.GetString(parameters, "session_id");
            int index;
            int offset;
            uint chunkCrc;
            if (!SessionMatches(sessionId) ||
                !JsonUtil.TryGetInt(parameters, "index", out index) ||
                !JsonUtil.TryGetInt(parameters, "offset", out offset) ||
                !JsonUtil.TryGetUInt(parameters, "chunk_crc32", out chunkCrc))
                return Error(409, "OTA session mismatch or params invalid", null);

            string encoded = JsonUtil.GetString(parameters, "data");
            byte[] data;
            try
            {
                data = Convert.FromBase64String(encoded);
            }
            catch
            {
                return Error(422, "OTA chunk data is not valid Base64", null);
            }

            if (Crc32.Compute(data) != chunkCrc)
                return Error(422, "OTA chunk CRC mismatch", null);

            return StoreChunk(index, offset, data) ? Success("OTA chunk written", AckData("chunk", index)) : Error(422, LastError, null);
        }

        private CommandExecution HandleJsonEnd(Dictionary<string, object> root)
        {
            Dictionary<string, object> parameters = JsonUtil.GetObject(root, "params");
            string sessionId = parameters == null ? string.Empty : JsonUtil.GetString(parameters, "session_id");
            if (!SessionMatches(sessionId))
                return Error(409, "OTA session mismatch", null);
            if (!AllChunksReceived())
                return Error(409, "OTA chunks are incomplete", null);
            if (Crc32.Compute(imageBuffer, 0, (int)FileLength) != FileCrc32)
                return Error(422, "OTA image CRC mismatch", null);

            Active = false;
            Complete = true;
            UpgradeStateSuccess = true;
            RebootRequested = true;
            return Success("OTA image verified; reboot scheduled", AckData("end", -1));
        }

        private CommandExecution HandleLegacyB0()
        {
            byte[] info = new byte[] { 1, 10, 2, 55, (byte)'x', (byte)'m', 1 };
            return BuildLegacyExecution(0xB0, info, 0, "legacy OTA info", "start");
        }

        private CommandExecution HandleLegacyB1(byte[] payload)
        {
            byte ack = 0;
            if (payload != null && payload.Length >= 12)
            {
                uint fileLen = ReadU32(payload, 0);
                uint fileCrc = ReadU32(payload, 8);
                int firmwareOrderChunkCount = ReadU16(payload, 4);
                int firmwareOrderChunkSize = ReadU16(payload, 6);
                int toolOrderChunkSize = ReadU16(payload, 4);
                int toolOrderChunkCount = ReadU16(payload, 6);

                LegacyFileInfoChoice choice = ChooseLegacyFileInfo(fileLen, firmwareOrderChunkSize, firmwareOrderChunkCount, toolOrderChunkSize, toolOrderChunkCount);
                if (choice.Valid && Start("legacy", fileLen, choice.ChunkSize, choice.ChunkCount, fileCrc))
                {
                    ack = 1;
                }
            }
            return BuildLegacyExecution(0xB1, new[] { ack }, ack == 1 ? 0 : 422, ack == 1 ? "legacy OTA file info accepted" : "legacy OTA file info invalid", "file_info");
        }

        private CommandExecution HandleLegacyB2(byte[] payload)
        {
            byte[] ack = new byte[] { 0, 1 };
            if (BreakUpgrade)
                ack[1] = 0;
            if (payload != null && payload.Length > 2 && Active && !BreakUpgrade)
            {
                int index = ReadU16(payload, 0);
                byte[] data = new byte[payload.Length - 2];
                Buffer.BlockCopy(payload, 2, data, 0, data.Length);
                if (StoreChunk(index, index * ChunkSize, data))
                    ack[0] = 1;
            }
            return BuildLegacyExecution(0xB2, ack, ack[0] == 1 ? 0 : 422, ack[0] == 1 ? "legacy OTA chunk written" : "legacy OTA chunk rejected", "chunk");
        }

        private CommandExecution HandleLegacyB3()
        {
            int lost = FirstLostChunk();
            byte[] payload = new byte[3];
            WriteU16(payload, 0, lost < 0 ? 0xFFFF : lost);
            if (lost < 0 && FileLength > 0)
            {
                if (Crc32.Compute(imageBuffer, 0, (int)FileLength) == FileCrc32)
                {
                    payload[2] = 1;
                    Active = false;
                    Complete = true;
                    UpgradeStateSuccess = true;
                }
                else
                {
                    LastError = "legacy OTA image CRC mismatch";
                }
            }

            return BuildLegacyExecution(0xB3, payload, payload[2] == 1 ? 0 : 409, payload[2] == 1 ? "legacy OTA image verified" : "legacy OTA has missing chunks or CRC mismatch", "check_lost");
        }

        private CommandExecution HandleLegacyB4()
        {
            byte ack = 0;
            if (UpgradeStateSuccess)
            {
                ack = 1;
                RebootRequested = true;
            }
            return BuildLegacyExecution(0xB4, new[] { ack }, ack == 1 ? 0 : 409, ack == 1 ? "legacy OTA end accepted; reboot scheduled" : "legacy OTA end rejected", "end");
        }

        private CommandExecution HandleLegacyB5()
        {
            byte status = UpgradeStateSuccess ? (byte)1 : (byte)0;
            return BuildLegacyExecution(0xB5, new[] { status }, UpgradeStateSuccess ? 0 : 409, UpgradeStateSuccess ? "legacy OTA result success" : "legacy OTA result pending", "result");
        }

        private CommandExecution HandleLegacyB6()
        {
            BreakUpgrade = true;
            Active = false;
            LastError = "legacy OTA break requested";
            return BuildLegacyExecution(0xB6, new byte[] { 1 }, 0, "legacy OTA break accepted", "break");
        }

        private bool Start(string sessionId, uint fileLength, int chunkSize, int chunkCount, uint fileCrc)
        {
            if (!ValidateFileInfo(fileLength, chunkSize, chunkCount))
                return false;

            Active = true;
            Complete = false;
            RebootRequested = false;
            SessionId = sessionId ?? string.Empty;
            FileLength = fileLength;
            FileCrc32 = fileCrc;
            ChunkSize = chunkSize;
            ChunkCount = chunkCount;
            ReceivedBytes = 0;
            ReceivedChunkCount = 0;
            LastError = string.Empty;
            BreakUpgrade = false;
            UpgradeStateSuccess = false;
            receivedFlags = new bool[chunkCount];
            imageBuffer = new byte[fileLength];
            return true;
        }

        private bool ValidateFileInfo(uint fileLength, int chunkSize, int chunkCount)
        {
            if (fileLength == 0)
            {
                LastError = "OTA image length is zero";
                return false;
            }
            if (chunkSize <= 0 || chunkSize > MaxChunkSize || chunkCount <= 0 || chunkCount > MaxChunks)
            {
                LastError = "OTA chunk settings are invalid";
                return false;
            }
            if ((uint)((chunkCount - 1) * chunkSize) >= fileLength)
            {
                LastError = "OTA chunk count does not match image length";
                return false;
            }
            if ((uint)(chunkCount * chunkSize) < fileLength)
            {
                LastError = "OTA chunk count is too small";
                return false;
            }
            return true;
        }

        private LegacyFileInfoChoice ChooseLegacyFileInfo(uint fileLength, int firmwareOrderChunkSize, int firmwareOrderChunkCount, int toolOrderChunkSize, int toolOrderChunkCount)
        {
            LegacyFileInfoChoice firmwareOrder = BuildChoice(fileLength, firmwareOrderChunkSize, firmwareOrderChunkCount);
            LegacyFileInfoChoice toolOrder = BuildChoice(fileLength, toolOrderChunkSize, toolOrderChunkCount);
            if (toolOrder.Valid && !firmwareOrder.Valid)
                return toolOrder;
            if (firmwareOrder.Valid && !toolOrder.Valid)
                return firmwareOrder;
            if (toolOrder.Valid && firmwareOrder.Valid)
            {
                if (ScoreLegacyChoice(toolOrder) >= ScoreLegacyChoice(firmwareOrder))
                    return toolOrder;
                return firmwareOrder;
            }
            return new LegacyFileInfoChoice();
        }

        private LegacyFileInfoChoice BuildChoice(uint fileLength, int chunkSize, int chunkCount)
        {
            string oldError = LastError;
            bool valid = ValidateFileInfo(fileLength, chunkSize, chunkCount);
            LastError = oldError;
            return new LegacyFileInfoChoice { Valid = valid, ChunkSize = chunkSize, ChunkCount = chunkCount };
        }

        private static int ScoreLegacyChoice(LegacyFileInfoChoice choice)
        {
            int score = 0;
            if (choice.ChunkSize >= 64)
                score += 4;
            if (choice.ChunkSize == 512)
                score += 8;
            if (choice.ChunkSize >= choice.ChunkCount)
                score += 2;
            if ((choice.ChunkSize % 128) == 0)
                score += 1;
            return score;
        }

        private bool StoreChunk(int index, int offset, byte[] data)
        {
            if (!Active || data == null)
            {
                LastError = "OTA session is not active";
                return false;
            }
            if (index < 0 || index >= ChunkCount || offset < 0 || offset >= FileLength)
            {
                LastError = "OTA chunk index or offset is invalid";
                return false;
            }
            if (data.Length == 0 || data.Length > ChunkSize || ((uint)offset + (uint)data.Length) > FileLength)
            {
                LastError = "OTA chunk length is invalid";
                return false;
            }
            Buffer.BlockCopy(data, 0, imageBuffer, offset, data.Length);
            if (!receivedFlags[index])
            {
                receivedFlags[index] = true;
                ReceivedChunkCount++;
                ReceivedBytes += (uint)data.Length;
            }
            return true;
        }

        private bool SessionMatches(string sessionId)
        {
            return (Active || Complete) && string.Equals(SessionId, sessionId ?? string.Empty, StringComparison.Ordinal);
        }

        private bool AllChunksReceived()
        {
            return FirstLostChunk() < 0;
        }

        private int FirstLostChunk()
        {
            if (ChunkCount == 0 || receivedFlags == null || receivedFlags.Length != ChunkCount)
                return 0;
            for (int i = 0; i < ChunkCount; i++)
            {
                if (!receivedFlags[i])
                    return i;
            }
            return -1;
        }

        private CommandExecution BuildLegacyExecution(byte cmdType, byte[] payload, int code, string message, string phase)
        {
            byte[] frame = LegacyFrame.BuildUpgradeResponse(cmdType, payload ?? new byte[0]);
            Dictionary<string, object> data = new Dictionary<string, object>
            {
                { "frame", Convert.ToBase64String(frame) },
                { "legacy_protocol", true },
                { "action", "mainboard_upgrade" },
                { "phase", phase },
                { "frame_type", 0xA5 },
                { "cmd_type", cmdType },
                { "payload_len", payload == null ? 0 : payload.Length },
                { "payload_hex", LegacyFrame.Hex(payload) },
                { "ota", StatusData() }
            };
            return new CommandExecution
            {
                Handled = true,
                Ok = code == 0,
                Code = code,
                Message = message,
                Data = data,
                RebootRequested = RebootRequested
            };
        }

        private CommandExecution Success(string message, Dictionary<string, object> data)
        {
            return new CommandExecution { Handled = true, Ok = true, Code = 0, Message = message, Data = data ?? new Dictionary<string, object>(), RebootRequested = RebootRequested };
        }

        private CommandExecution Error(int code, string message, Dictionary<string, object> data)
        {
            LastError = message ?? string.Empty;
            return new CommandExecution { Handled = true, Ok = false, Code = code, Message = message ?? string.Empty, Data = data ?? StatusData(), RebootRequested = RebootRequested };
        }

        private Dictionary<string, object> AckData(string phase, int index)
        {
            Dictionary<string, object> data = StatusData();
            data["phase"] = phase;
            if (index >= 0)
                data["index"] = index;
            return data;
        }

        private static int ReadU16(byte[] data, int offset)
        {
            return (data[offset] << 8) | data[offset + 1];
        }

        private static uint ReadU32(byte[] data, int offset)
        {
            return ((uint)data[offset] << 24) | ((uint)data[offset + 1] << 16) | ((uint)data[offset + 2] << 8) | data[offset + 3];
        }

        private static void WriteU16(byte[] data, int offset, int value)
        {
            data[offset] = (byte)((value >> 8) & 0xFF);
            data[offset + 1] = (byte)(value & 0xFF);
        }

        private struct LegacyFileInfoChoice
        {
            public bool Valid;
            public int ChunkSize;
            public int ChunkCount;
        }
    }
}
