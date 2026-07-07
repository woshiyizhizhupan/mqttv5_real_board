using System;
using System.Globalization;
using System.Text;

namespace EmqxDeviceEmulator.Protocol
{
    public sealed class LegacyFrameInfo
    {
        public bool IsValid { get; set; }
        public bool HasOuterWrapper { get; set; }
        public byte FrameType { get; set; }
        public byte OuterSubType { get; set; }
        public byte CommandType { get; set; }
        public byte[] Payload { get; set; }
        public string RawHex { get; set; }
        public string PayloadHex { get; set; }
        public bool OuterCrcOk { get; set; }
        public bool InnerCrcOk { get; set; }
        public string Error { get; set; }
        public string Summary { get; set; }

        public string CommandHex
        {
            get { return CommandType.ToString("X2", CultureInfo.InvariantCulture); }
        }

        public LegacyFrameInfo()
        {
            Payload = new byte[0];
            RawHex = string.Empty;
            PayloadHex = string.Empty;
            Error = string.Empty;
            Summary = string.Empty;
        }
    }

    public static class LegacyFrame
    {
        private const byte Preamble = 0xFE;
        private const byte FrameTypeUpgrade = 0xA5;

        public static byte[] BuildV2OtaRequest(byte cmdType, byte[] payload)
        {
            byte[] bodyPayload = payload ?? new byte[0];
            byte[] innerBody = Concat(new[] { cmdType }, U16(bodyPayload.Length), bodyPayload);
            byte[] inner = Concat(new[] { Preamble }, innerBody, U32(Crc32.Compute(innerBody)));
            byte[] outerBody = Concat(new[] { FrameTypeUpgrade, (byte)0x01 }, U16(inner.Length), inner);
            return Concat(new[] { Preamble }, outerBody, U32(Crc32.Compute(outerBody)));
        }

        public static byte[] BuildUpgradeResponse(byte cmdType, byte[] payload)
        {
            byte[] bodyPayload = payload ?? new byte[0];
            byte[] body = Concat(new[] { FrameTypeUpgrade, cmdType }, U16(bodyPayload.Length), bodyPayload);
            return Concat(new[] { Preamble }, body, U32(Crc32.Compute(body)));
        }

        public static bool TryParseAny(byte[] raw, out LegacyFrameInfo info)
        {
            info = new LegacyFrameInfo();
            if (raw == null || raw.Length < 7)
            {
                info.Error = "legacy frame too short";
                return false;
            }

            info.RawHex = Hex(raw);
            if (raw[0] != Preamble)
            {
                info.Error = "legacy frame missing FE preamble";
                return false;
            }

            if (raw.Length >= 10 && raw[1] == FrameTypeUpgrade && raw[2] == 0x01)
                return TryParseV2Outer(raw, info);

            if (raw[1] == FrameTypeUpgrade)
                return TryParseUpgradeResponse(raw, info);

            if (raw[1] >= 0xB0 && raw[1] <= 0xB6)
                return TryParseInnerRequest(raw, out info);

            info.Error = "unsupported legacy frame type";
            return false;
        }

        public static bool TryDecodeTextFrame(string text, out byte[] frame)
        {
            frame = null;
            if (string.IsNullOrWhiteSpace(text))
                return false;

            try
            {
                frame = Convert.FromBase64String(text.Trim());
                return frame.Length > 0;
            }
            catch
            {
            }

            return TryDecodeHex(text, out frame);
        }

        public static bool TryDecodeHex(string text, out byte[] data)
        {
            data = null;
            if (string.IsNullOrWhiteSpace(text))
                return false;
            string compact = text.Replace(" ", string.Empty).Replace("-", string.Empty).Replace(":", string.Empty).Replace("\r", string.Empty).Replace("\n", string.Empty).Replace("\t", string.Empty);
            if (compact.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                compact = compact.Substring(2);
            if ((compact.Length % 2) != 0)
                return false;
            byte[] bytes = new byte[compact.Length / 2];
            for (int i = 0; i < bytes.Length; i++)
            {
                byte value;
                if (!byte.TryParse(compact.Substring(i * 2, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out value))
                    return false;
                bytes[i] = value;
            }
            data = bytes;
            return bytes.Length > 0;
        }

        public static string Hex(byte[] data)
        {
            if (data == null || data.Length == 0)
                return string.Empty;
            StringBuilder builder = new StringBuilder(data.Length * 3);
            for (int i = 0; i < data.Length; i++)
            {
                if (i > 0)
                    builder.Append(' ');
                builder.Append(data[i].ToString("X2", CultureInfo.InvariantCulture));
            }
            return builder.ToString();
        }

        private static bool TryParseV2Outer(byte[] raw, LegacyFrameInfo info)
        {
            int innerLength = ReadU16(raw, 3);
            int expected = 1 + 1 + 1 + 2 + innerLength + 4;
            if (raw.Length != expected)
            {
                info.Error = "outer A5/01 length mismatch";
                return false;
            }

            uint expectedCrc = ReadU32(raw, raw.Length - 4);
            uint actualCrc = Crc32.Compute(raw, 1, raw.Length - 5);
            info.OuterCrcOk = expectedCrc == actualCrc;
            if (!info.OuterCrcOk)
            {
                info.Error = "outer A5/01 CRC mismatch";
                return false;
            }

            byte[] inner = new byte[innerLength];
            Buffer.BlockCopy(raw, 5, inner, 0, inner.Length);
            LegacyFrameInfo innerInfo;
            if (!TryParseInnerRequest(inner, out innerInfo))
            {
                info.Error = innerInfo.Error;
                return false;
            }

            info.IsValid = true;
            info.HasOuterWrapper = true;
            info.FrameType = FrameTypeUpgrade;
            info.OuterSubType = 0x01;
            info.CommandType = innerInfo.CommandType;
            info.Payload = innerInfo.Payload;
            info.PayloadHex = innerInfo.PayloadHex;
            info.InnerCrcOk = innerInfo.InnerCrcOk;
            info.Summary = "A5/01旧业务OTA下行帧，命令B" + (info.CommandType & 0x0F).ToString("X1", CultureInfo.InvariantCulture) + "。";
            return true;
        }

        private static bool TryParseInnerRequest(byte[] raw, out LegacyFrameInfo info)
        {
            info = new LegacyFrameInfo();
            info.RawHex = Hex(raw);
            if (raw == null || raw.Length < 8 || raw[0] != Preamble)
            {
                info.Error = "inner legacy frame invalid";
                return false;
            }

            int payloadLength = ReadU16(raw, 2);
            int expected = 1 + 1 + 2 + payloadLength + 4;
            if (raw.Length != expected)
            {
                info.Error = "inner legacy frame length mismatch";
                return false;
            }

            uint expectedCrc = ReadU32(raw, raw.Length - 4);
            uint actualCrc = Crc32.Compute(raw, 1, raw.Length - 5);
            info.InnerCrcOk = expectedCrc == actualCrc;
            if (!info.InnerCrcOk)
            {
                info.Error = "inner legacy frame CRC mismatch";
                return false;
            }

            byte[] payload = new byte[payloadLength];
            if (payloadLength > 0)
                Buffer.BlockCopy(raw, 4, payload, 0, payloadLength);
            info.IsValid = true;
            info.CommandType = raw[1];
            info.Payload = payload;
            info.PayloadHex = Hex(payload);
            info.Summary = "旧业务OTA子帧，命令0x" + info.CommandHex + "。";
            return true;
        }

        private static bool TryParseUpgradeResponse(byte[] raw, LegacyFrameInfo info)
        {
            int payloadLength = ReadU16(raw, 3);
            int expected = 1 + 1 + 1 + 2 + payloadLength + 4;
            if (raw.Length != expected)
            {
                info.Error = "legacy response frame length mismatch";
                return false;
            }

            uint expectedCrc = ReadU32(raw, raw.Length - 4);
            uint actualCrc = Crc32.Compute(raw, 1, raw.Length - 5);
            info.InnerCrcOk = expectedCrc == actualCrc;
            if (!info.InnerCrcOk)
            {
                info.Error = "legacy response frame CRC mismatch";
                return false;
            }

            byte[] payload = new byte[payloadLength];
            if (payloadLength > 0)
                Buffer.BlockCopy(raw, 5, payload, 0, payloadLength);
            info.IsValid = true;
            info.FrameType = raw[1];
            info.CommandType = raw[2];
            info.Payload = payload;
            info.PayloadHex = Hex(payload);
            info.Summary = "旧业务OTA响应帧，命令0x" + info.CommandHex + "。";
            return true;
        }

        private static int ReadU16(byte[] data, int offset)
        {
            return (data[offset] << 8) | data[offset + 1];
        }

        private static uint ReadU32(byte[] data, int offset)
        {
            return ((uint)data[offset] << 24) | ((uint)data[offset + 1] << 16) | ((uint)data[offset + 2] << 8) | data[offset + 3];
        }

        private static byte[] U16(int value)
        {
            return new[] { (byte)((value >> 8) & 0xFF), (byte)(value & 0xFF) };
        }

        private static byte[] U32(uint value)
        {
            return new[] { (byte)((value >> 24) & 0xFF), (byte)((value >> 16) & 0xFF), (byte)((value >> 8) & 0xFF), (byte)(value & 0xFF) };
        }

        private static byte[] Concat(params byte[][] parts)
        {
            int length = 0;
            foreach (byte[] part in parts)
                length += part == null ? 0 : part.Length;
            byte[] result = new byte[length];
            int offset = 0;
            foreach (byte[] part in parts)
            {
                if (part == null)
                    continue;
                Buffer.BlockCopy(part, 0, result, offset, part.Length);
                offset += part.Length;
            }
            return result;
        }
    }
}
