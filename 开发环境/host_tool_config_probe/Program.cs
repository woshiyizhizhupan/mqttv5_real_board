using System;
using System.Linq;
using System.Net;
using System.Threading;
using mqttv5_tool;

namespace host_tool_config_probe
{
    internal static class Program
    {
        private const string DefaultBoardIp = "192.168.0.30";
        private const string DefaultCityId = "tjw";
        private const string DefaultPoleId = "pole001";
        private const string DefaultRemoteHost = "39.103.154.108";
        private const ushort DefaultRemotePort = 1883;
        private const string DefaultRemoteUsername = "GM400-452089";
        private const string DefaultRemotePassword = "public";
        private const string DefaultDeviceTypeName = "GM400";
        private const string DefaultDeviceNumericId = "452089";
        private const string DefaultRequestTopicPrefix = "v1/devices/request/";
        private const string DefaultResponseTopicPrefix = "v1/devices/response/";

        private static int Main(string[] args)
        {
            string command = args.Length > 0 ? args[0].Trim().ToLowerInvariant() : "read";
            string ip = args.Length > 1 ? args[1].Trim() : DefaultBoardIp;

            try
            {
                if (command == "read")
                {
                    using (ConnectedManagement connected = ConnectAndUpload(ip))
                    {
                        PrintConfig("READ", connected.Device);
                    }
                    return 0;
                }

                if (command == "write-doc-topics")
                {
                    string cityId = args.Length > 2 ? args[2].Trim() : DefaultCityId;
                    string poleId = args.Length > 3 ? args[3].Trim() : DefaultPoleId;
                    using (ConnectedManagement connected = ConnectAndUpload(ip))
                    {
                        PrintConfig("BEFORE", connected.Device);
                        string deviceName = connected.Device.DeviceId;
                        ApplyDocumentedTopics(connected.Device, cityId, poleId, deviceName, connected.Device.DeviceId);
                        string error = connected.Device.ValidateForWrite();
                        if (error != null)
                            throw new InvalidOperationException(error);
                        connected.Device.Load();
                        connected.Device.Save();
                        Thread.Sleep(500);
                        connected.Device.Upload();
                        PrintConfig("AFTER", connected.Device);
                        VerifyDocumentedTopics(connected.Device, cityId, poleId, deviceName, connected.Device.DeviceId);
                    }
                    return 0;
                }

                if (command == "write-remote-emqx")
                {
                    string host = args.Length > 2 ? args[2].Trim() : DefaultRemoteHost;
                    ushort port = args.Length > 3 ? ushort.Parse(args[3].Trim()) : DefaultRemotePort;
                    string username = args.Length > 4 ? args[4].Trim() : DefaultRemoteUsername;
                    string password = args.Length > 5 ? args[5] : DefaultRemotePassword;
                    byte tlsMode = args.Length > 6 ? byte.Parse(args[6].Trim()) : (byte)0;
                    byte tlsVerifyPeer = args.Length > 7 ? byte.Parse(args[7].Trim()) : (byte)0;
                    string cityId = args.Length > 8 ? args[8].Trim() : DefaultCityId;
                    string poleId = args.Length > 9 ? args[9].Trim() : DefaultPoleId;

                    using (ConnectedManagement connected = ConnectAndUpload(ip))
                    {
                        PrintConfig("BEFORE", connected.Device);
                        string deviceName = connected.Device.DeviceId;
                        ApplyDocumentedTopics(connected.Device, cityId, poleId, deviceName, connected.Device.DeviceId);
                        connected.Device.Host = host;
                        connected.Device.Port = port;
                        connected.Device.Username = username;
                        connected.Device.Password = password;
                        connected.Device.TlsMode = tlsMode;
                        connected.Device.TlsVerifyPeer = tlsVerifyPeer;
                        string error = connected.Device.ValidateForWrite();
                        if (error != null)
                            throw new InvalidOperationException(error);
                        connected.Device.Load();
                        connected.Device.Save();
                        Thread.Sleep(500);
                        connected.Device.Upload();
                        PrintConfig("AFTER", connected.Device);
                        VerifyRemoteEmqx(connected.Device, host, port, username, password, tlsMode, tlsVerifyPeer, cityId, poleId, deviceName, connected.Device.DeviceId);
                    }
                    return 0;
                }

                if (command == "write-static")
                {
                    string staticIp = args.Length > 2 ? args[2].Trim() : ip;
                    string subnetMask = args.Length > 3 ? args[3].Trim() : "255.255.255.0";
                    string gateway = args.Length > 4 ? args[4].Trim() : BuildDefaultGateway(staticIp);
                    string dns = args.Length > 5 ? args[5].Trim() : gateway;
                    int reconnectTimeoutMs = args.Length > 6 ? int.Parse(args[6].Trim()) : 45000;

                    using (ConnectedManagement connected = ConnectAndUpload(ip))
                    {
                        PrintConfig("BEFORE", connected.Device);
                        ApplyStaticNetwork(connected.Device, staticIp, subnetMask, gateway, dns);
                        string error = connected.Device.ValidateForWrite();
                        if (error != null)
                            throw new InvalidOperationException(error);
                        connected.Device.Load();
                        connected.Device.SaveAndRestart();
                        Console.WriteLine("SAVE_AND_RESTART_SENT=1");
                    }

                    using (ConnectedManagement reconnected = WaitForConfig(staticIp, reconnectTimeoutMs))
                    {
                        PrintConfig("AFTER", reconnected.Device);
                        VerifyStaticNetwork(reconnected.Device, staticIp, subnetMask, gateway, dns);
                    }
                    return 0;
                }

                if (command == "write-dhcp")
                {
                    string expectedIp = args.Length > 2 ? args[2].Trim() : ip;
                    int reconnectTimeoutMs = args.Length > 3 ? int.Parse(args[3].Trim()) : 45000;

                    using (ConnectedManagement connected = ConnectAndUpload(ip))
                    {
                        PrintConfig("BEFORE", connected.Device);
                        ApplyDhcpNetwork(connected.Device);
                        string error = connected.Device.ValidateForWrite();
                        if (error != null)
                            throw new InvalidOperationException(error);
                        connected.Device.Load();
                        connected.Device.SaveAndRestart();
                        Console.WriteLine("SAVE_AND_RESTART_SENT=1");
                    }

                    using (ConnectedManagement reconnected = WaitForConfig(expectedIp, reconnectTimeoutMs))
                    {
                        PrintConfig("AFTER", reconnected.Device);
                        VerifyDhcpNetwork(reconnected.Device);
                    }
                    return 0;
                }

                if (command == "write-unified-request-topic" || command == "write-single-request-topic" || command == "write-server-topics")
                {
                    using (ConnectedManagement connected = ConnectAndUpload(ip))
                    {
                        string requestTopic;
                        string responseTopic;
                        SelectServerTopicArguments(args, connected.Device, out requestTopic, out responseTopic);
                        PrintConfig("BEFORE", connected.Device);
                        ApplyServerTopics(connected.Device, requestTopic, responseTopic);
                        string error = connected.Device.ValidateForWrite();
                        if (error != null)
                            throw new InvalidOperationException(error);
                        connected.Device.Load();
                        connected.Device.Save();
                        Thread.Sleep(500);
                        connected.Device.Upload();
                        PrintConfig("AFTER", connected.Device);
                        VerifyServerTopics(connected.Device, requestTopic, responseTopic);
                        connected.Device.Reset();
                        Console.WriteLine("RESET_SENT=1");
                    }
                    return 0;
                }

                if (command == "reset")
                {
                    using (ConnectedManagement connected = ConnectAndUpload(ip))
                    {
                        connected.Device.Reset();
                        Console.WriteLine("RESET_SENT=1");
                    }
                    return 0;
                }

                Console.Error.WriteLine("Usage: host_tool_config_probe read|write-doc-topics|write-remote-emqx|write-static|write-dhcp|write-unified-request-topic|write-single-request-topic|write-server-topics|reset [ip] [args...]");
                return 2;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("ERROR=" + ex.GetType().Name + ": " + ex.Message);
                return 1;
            }
        }

        private static ConnectedManagement ConnectAndUpload(string ip)
        {
            Management device = new Management(IPAddress.Parse(ip));
            try
            {
                device.Connect();
                device.Upload();
                return new ConnectedManagement(device);
            }
            catch
            {
                device.Disconnect();
                throw;
            }
        }

        private static void ApplyDocumentedTopics(Management device, string cityId, string poleId, string deviceName, string deviceId)
        {
            string prefixByName = "city/" + cityId + "/pole/" + poleId + "/device/" + deviceName;
            string prefixById = "city/" + cityId + "/pole/" + poleId + "/device/" + deviceId;
            device.Topics[0] = prefixByName + "/";
            device.Topics[1] = prefixByName + "/get";
            device.Topics[2] = prefixByName + "/event";
            device.Topics[3] = prefixById + "/ota";
            device.Topics[4] = prefixByName + "/debug";
            for (int i = 0; i < Management.TopicCount; i++)
                device.Qos[i] = 2;
            if (string.IsNullOrWhiteSpace(device.NtpServer))
                device.NtpServer = "pool.ntp.org";
            device.TlsMode = 2;
            device.TlsVerifyPeer = 1;
        }

        private static void SelectServerTopicArguments(string[] args, Management device, out string requestTopic, out string responseTopic)
        {
            requestTopic = args.Length > 2 ? args[2].Trim() : BuildRequestTopic(device);
            responseTopic = args.Length > 3 ? args[3].Trim() : BuildResponseTopic(device);
            if (string.IsNullOrWhiteSpace(requestTopic))
                requestTopic = BuildRequestTopic(device);
            if (string.IsNullOrWhiteSpace(responseTopic))
                responseTopic = BuildResponseTopic(device);
        }

        private static string BuildDeviceName(Management device)
        {
            string current = device.DeviceId == null ? string.Empty : device.DeviceId.Trim();
            if (!string.IsNullOrWhiteSpace(current) && !string.Equals(current, DefaultDeviceTypeName, StringComparison.Ordinal))
                return current;
            return DefaultDeviceTypeName + "-" + DefaultDeviceNumericId;
        }

        private static string BuildRequestTopic(Management device)
        {
            return DefaultRequestTopicPrefix + BuildDeviceName(device);
        }

        private static string BuildResponseTopic(Management device)
        {
            return DefaultResponseTopicPrefix + BuildDeviceName(device);
        }

        private static void ApplyServerTopics(Management device, string requestTopic, string responseTopic)
        {
            device.Topics[0] = responseTopic;
            device.Topics[1] = requestTopic;
            device.Topics[2] = responseTopic;
            device.Topics[3] = requestTopic;
            device.Topics[4] = responseTopic;
            for (int i = 0; i < Management.TopicCount; i++)
                device.Qos[i] = 1;
            if (string.IsNullOrWhiteSpace(device.NtpServer))
                device.NtpServer = "pool.ntp.org";
        }

        private static void ApplyStaticNetwork(Management device, string staticIp, string subnetMask, string gateway, string dns)
        {
            device.Mode = 0;
            device.Ip = IPAddress.Parse(staticIp);
            device.Sn = IPAddress.Parse(subnetMask);
            device.Gw = IPAddress.Parse(gateway);
            device.Dns = IPAddress.Parse(dns);
        }

        private static void ApplyDhcpNetwork(Management device)
        {
            device.Mode = 1;
        }

        private static ConnectedManagement WaitForConfig(string ip, int timeoutMs)
        {
            return new ConnectedManagement(Management.WaitForDevice(IPAddress.Parse(ip), timeoutMs));
        }

        private static string BuildDefaultGateway(string ip)
        {
            byte[] bytes = IPAddress.Parse(ip).GetAddressBytes();
            bytes[3] = 1;
            return new IPAddress(bytes).ToString();
        }

        private static void VerifyServerTopics(Management device, string requestTopic, string responseTopic)
        {
            if (!string.Equals(device.Topics[0], responseTopic, StringComparison.Ordinal))
                throw new InvalidOperationException("topic1 readback mismatch");
            if (!string.Equals(device.Topics[1], requestTopic, StringComparison.Ordinal))
                throw new InvalidOperationException("topic2 readback mismatch");
            if (!string.Equals(device.Topics[2], responseTopic, StringComparison.Ordinal))
                throw new InvalidOperationException("topic3 readback mismatch");
            if (!string.Equals(device.Topics[3], requestTopic, StringComparison.Ordinal))
                throw new InvalidOperationException("topic4 readback mismatch");
            if (!string.Equals(device.Topics[4], responseTopic, StringComparison.Ordinal))
                throw new InvalidOperationException("topic5 readback mismatch");
            for (int i = 0; i < Management.TopicCount; i++)
                if (device.Qos[i] != 1)
                    throw new InvalidOperationException("qos" + (i + 1) + " readback mismatch");

            Console.WriteLine("SERVER_TOPICS_ROUNDTRIP_OK=1");
            Console.WriteLine("REQUEST_TOPIC=" + requestTopic);
            Console.WriteLine("RESPONSE_TOPIC=" + responseTopic);
            Console.WriteLine("PUBLISH_TOPIC=" + responseTopic);
            Console.WriteLine("SUBSCRIBE_TOPIC=" + requestTopic);
        }

        private static void VerifyDocumentedTopics(Management device, string cityId, string poleId, string deviceName, string deviceId)
        {
            string[] expected = new string[Management.TopicCount];
            string prefixByName = "city/" + cityId + "/pole/" + poleId + "/device/" + deviceName;
            string prefixById = "city/" + cityId + "/pole/" + poleId + "/device/" + deviceId;
            expected[0] = prefixByName + "/";
            expected[1] = prefixByName + "/get";
            expected[2] = prefixByName + "/event";
            expected[3] = prefixById + "/ota";
            expected[4] = prefixByName + "/debug";

            for (int i = 0; i < Management.TopicCount; i++)
            {
                if (!string.Equals(device.Topics[i], expected[i], StringComparison.Ordinal))
                    throw new InvalidOperationException("topic" + (i + 1) + " readback mismatch");
                if (device.Qos[i] != 2)
                    throw new InvalidOperationException("qos" + (i + 1) + " readback mismatch");
            }
            Console.WriteLine("ROUNDTRIP_OK=1");
            Console.WriteLine("EXPECTED_GET_TOPIC=" + expected[1]);
            Console.WriteLine("EXPECTED_EVENT_TOPIC=" + expected[2]);
        }

        private static void VerifyRemoteEmqx(Management device, string host, ushort port, string username, string password, byte tlsMode, byte tlsVerifyPeer, string cityId, string poleId, string deviceName, string deviceId)
        {
            VerifyDocumentedTopics(device, cityId, poleId, deviceName, deviceId);
            if (!string.Equals(device.Host, host, StringComparison.Ordinal))
                throw new InvalidOperationException("host readback mismatch");
            if (device.Port != port)
                throw new InvalidOperationException("port readback mismatch");
            if (!string.Equals(device.Username, username, StringComparison.Ordinal))
                throw new InvalidOperationException("username readback mismatch");
            if (!string.Equals(device.Password, password, StringComparison.Ordinal))
                throw new InvalidOperationException("password readback mismatch");
            if (device.TlsMode != tlsMode)
                throw new InvalidOperationException("tls mode readback mismatch");
            if (device.TlsVerifyPeer != tlsVerifyPeer)
                throw new InvalidOperationException("tls verify readback mismatch");
            Console.WriteLine("REMOTE_EMQX_ROUNDTRIP_OK=1");
        }

        private static void VerifyStaticNetwork(Management device, string staticIp, string subnetMask, string gateway, string dns)
        {
            if (device.Mode != 0)
                throw new InvalidOperationException("mode readback mismatch");
            if (!IPAddress.Parse(staticIp).Equals(device.Ip))
                throw new InvalidOperationException("static ip readback mismatch");
            if (!IPAddress.Parse(subnetMask).Equals(device.Sn))
                throw new InvalidOperationException("subnet mask readback mismatch");
            if (!IPAddress.Parse(gateway).Equals(device.Gw))
                throw new InvalidOperationException("gateway readback mismatch");
            if (!IPAddress.Parse(dns).Equals(device.Dns))
                throw new InvalidOperationException("dns readback mismatch");

            Console.WriteLine("STATIC_CONFIG_ROUNDTRIP_OK=1");
        }

        private static void VerifyDhcpNetwork(Management device)
        {
            if (device.Mode != 1)
                throw new InvalidOperationException("dhcp mode readback mismatch");

            Console.WriteLine("DHCP_CONFIG_ROUNDTRIP_OK=1");
        }

        private static void PrintConfig(string label, Management device)
        {
            Console.WriteLine("[" + label + "]");
            Console.WriteLine("EthIp=" + device.EthIp);
            Console.WriteLine("Mode=" + device.Mode);
            Console.WriteLine("Ip=" + device.Ip);
            Console.WriteLine("Sn=" + device.Sn);
            Console.WriteLine("Gw=" + device.Gw);
            Console.WriteLine("Dns=" + device.Dns);
            Console.WriteLine("Mac=" + device.Mac);
            Console.WriteLine("DeviceId=" + device.DeviceId);
            Console.WriteLine("Host=" + device.Host);
            Console.WriteLine("Port=" + device.Port);
            Console.WriteLine("Username=" + device.Username);
            Console.WriteLine("PasswordSet=" + (!string.IsNullOrEmpty(device.Password) ? "1" : "0"));
            Console.WriteLine("TlsMode=" + device.TlsMode);
            Console.WriteLine("TlsVerifyPeer=" + device.TlsVerifyPeer);
            Console.WriteLine("NtpServer=" + device.NtpServer);
            Console.WriteLine("Qos=" + string.Join(",", device.Qos.Select(item => item.ToString()).ToArray()));
            for (int i = 0; i < Management.TopicCount; i++)
                Console.WriteLine("Topic" + (i + 1) + "=" + device.Topics[i]);
        }

        private sealed class ConnectedManagement : IDisposable
        {
            public ConnectedManagement(Management device)
            {
                Device = device;
            }

            public Management Device { get; private set; }

            public void Dispose()
            {
                if (Device != null)
                {
                    Device.Disconnect();
                    Device = null;
                }
            }
        }
    }
}
