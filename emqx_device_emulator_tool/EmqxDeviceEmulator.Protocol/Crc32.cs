using System;

namespace EmqxDeviceEmulator.Protocol
{
    public static class Crc32
    {
        private static readonly uint[] Table = BuildTable();

        public static uint Compute(byte[] data)
        {
            if (data == null)
                return 0;
            return Compute(data, 0, data.Length);
        }

        public static uint Compute(byte[] data, int offset, int count)
        {
            if (data == null)
                return 0;
            if (offset < 0 || count < 0 || offset + count > data.Length)
                throw new ArgumentOutOfRangeException("offset");

            uint crc = 0xFFFFFFFFU;
            for (int i = 0; i < count; i++)
                crc = Table[(crc ^ data[offset + i]) & 0xFF] ^ (crc >> 8);
            return crc ^ 0xFFFFFFFFU;
        }

        private static uint[] BuildTable()
        {
            uint[] table = new uint[256];
            for (uint i = 0; i < table.Length; i++)
            {
                uint crc = i;
                for (int bit = 0; bit < 8; bit++)
                    crc = (crc & 1U) != 0U ? 0xEDB88320U ^ (crc >> 1) : crc >> 1;
                table[i] = crc;
            }
            return table;
        }
    }
}
