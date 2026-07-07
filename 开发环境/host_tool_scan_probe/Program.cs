using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using mqttv5_tool;

namespace host_tool_scan_probe
{
    internal static class Program
    {
        private const int FirstDeviceTimeoutMs = 45000;

        private static int Main(string[] args)
        {
            string expectedIp = args.Length > 0 ? args[0].Trim() : string.Empty;
            if (string.IsNullOrEmpty(expectedIp))
                return ScanFirstDevice();

            var devices = new List<Management>();

            Management.Scan(devices).GetAwaiter().GetResult();

            PrintDevices(devices);
            return devices.Exists(item => item.EthIp.ToString() == expectedIp) ? 0 : 2;
        }

        private static int ScanFirstDevice()
        {
            var devices = new List<Management>();
            Management first = null;
            object gate = new object();
            using (var found = new ManualResetEventSlim(false))
            {
                Console.WriteLine("SCAN_MODE=first");
                Task scanTask = Management.Scan(devices, device =>
                {
                    lock (gate)
                    {
                        if (first == null)
                            first = device;
                    }
                    found.Set();
                });

                if (!found.Wait(FirstDeviceTimeoutMs))
                {
                    Console.WriteLine("FOUND_COUNT=0");
                    Console.WriteLine("FIRST_IP=");
                    Console.WriteLine("DIAGNOSTICS=" + (Management.LastScanDiagnostics ?? string.Empty).Replace(Environment.NewLine, " | "));
                    return 2;
                }

                lock (gate)
                {
                    if (devices.Count == 0 && first != null)
                        devices.Add(first);
                }
                PrintDevices(devices);
                return 0;
            }
        }

        private static void PrintDevices(List<Management> devices)
        {
            Console.WriteLine("FOUND_COUNT=" + devices.Count);
            if (devices.Count > 0)
                Console.WriteLine("FIRST_IP=" + devices[0].EthIp);
            Console.WriteLine("DIAGNOSTICS=" + (Management.LastScanDiagnostics ?? string.Empty).Replace(Environment.NewLine, " | "));
            foreach (Management item in devices)
            {
                Console.WriteLine(string.Join("|",
                    item.EthIp,
                    item.DeviceModel,
                    item.DeviceId,
                    item.Status,
                    item.IpAssignText,
                    item.Mac));
            }
        }
    }
}
