using NModbus;
using NModbus.Device;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace mqttv5_tool
{
    public sealed class ScanProgressInfo
    {
        public ScanProgressInfo(string currentIp, string stage, int completed, int total, int foundCount, int concurrency, TimeSpan elapsed, TimeSpan estimatedRemaining, bool isCompleted)
        {
            CurrentIp = currentIp ?? string.Empty;
            Stage = stage ?? string.Empty;
            Completed = Math.Max(0, completed);
            Total = Math.Max(0, total);
            FoundCount = Math.Max(0, foundCount);
            Concurrency = Math.Max(1, concurrency);
            Elapsed = elapsed < TimeSpan.Zero ? TimeSpan.Zero : elapsed;
            EstimatedRemaining = estimatedRemaining < TimeSpan.Zero ? TimeSpan.Zero : estimatedRemaining;
            IsCompleted = isCompleted;
        }

        public string CurrentIp { get; private set; }
        public string Stage { get; private set; }
        public int Completed { get; private set; }
        public int Total { get; private set; }
        public int FoundCount { get; private set; }
        public int Concurrency { get; private set; }
        public TimeSpan Elapsed { get; private set; }
        public TimeSpan EstimatedRemaining { get; private set; }
        public bool IsCompleted { get; private set; }

        public int Percent
        {
            get
            {
                if (Total <= 0)
                    return 0;
                int percent = (int)Math.Round(Completed * 100.0 / Total);
                if (percent < 0)
                    return 0;
                if (percent > 100)
                    return 100;
                return percent;
            }
        }
    }

    public sealed class Management
    {
        public const int ConfigSizeBytes = 768;
        public const int TopicCount = 5;
        public const int TopicLength = 96;
        public const int NtpServerLength = 64;

        private const byte SlaveId = 1;
        private const int ModbusPort = 502;
        private const int MaxRegistersPerRequest = 120;
        private const int ScanConnectTimeoutMs = 1000;
        private const int PriorityScanConnectTimeoutMs = 15000;
        private const int PriorityScanRetryDelayMs = 250;
        private const int ScanConcurrencyLimit = 5;
        private const int ScanUploadRetryCount = 2;
        private const int ManagementConnectRetryCount = 5;
        private const int ManagementConnectRetryDelayMs = 300;
        public const int SaveRestartReconnectTimeoutMs = 60000;
        private static readonly string[] DefaultBoardScanPrefixes = new string[] { "192.168.0.", "192.168.10." };
        private static readonly int[] PriorityScanHosts = new int[] { 108, 30, 111, 218 };
        private const int DeviceIdOffset = 26;
        public const int DeviceIdLength = 32;
        private const int HostOffset = 58;
        public const int HostLength = 64;
        private const int PortOffset = 122;
        private const int UsernameOffset = 124;
        public const int UsernameLength = 32;
        private const int PasswordOffset = 156;
        public const int PasswordLength = 32;
        private const int TopicsOffset = 188;
        private const int QosOffset = 668;
        private const int NtpOffset = 673;
        private const int TlsModeOffset = 737;
        private const int TlsVerifyPeerOffset = 738;

        private static readonly ModbusFactory Factory = new ModbusFactory();
        private static readonly object ScanDiagnosticsLock = new object();

        private IModbusMaster master;
        private TcpClient tcpClient;

        public static string LastScanDiagnostics { get; private set; }

        private sealed class ScanProgressState
        {
            public int Total;
            public int Completed;
            public DateTime StartedAtUtc;
            public Action<ScanProgressInfo> OnProgress;
            public Func<int> FoundCountProvider;
        }

        public Management(IPAddress ethIp)
        {
            EthIp = ethIp;
            DeviceModel = "GM400";
            DeviceId = "GM400";
            Status = "正常运行";
            Mode = 1;
            Ip = IPAddress.None;
            Sn = IPAddress.Parse("255.255.255.0");
            Gw = IPAddress.None;
            Dns = IPAddress.None;
            Mac = PhysicalAddress.None;
            Host = "192.168.0.110";
            Port = 1883;
            Username = string.Empty;
            Password = string.Empty;
            Topics = BuildDefaultTopics();
            Qos = new byte[TopicCount];
            NtpServer = "pool.ntp.org";
            TlsMode = 0;
            TlsVerifyPeer = 1;
        }

        public IPAddress EthIp { get; private set; }
        public string DeviceModel { get; set; }
        public string DeviceId { get; set; }
        public string Status { get; set; }
        public byte Mode { get; set; }
        public IPAddress Ip { get; set; }
        public IPAddress Sn { get; set; }
        public IPAddress Gw { get; set; }
        public IPAddress Dns { get; set; }
        public PhysicalAddress Mac { get; set; }
        public string Host { get; set; }
        public ushort Port { get; set; }
        public string Username { get; set; }
        public string Password { get; set; }
        public string[] Topics { get; private set; }
        public byte[] Qos { get; private set; }
        public string NtpServer { get; set; }
        public byte TlsMode { get; set; }
        public byte TlsVerifyPeer { get; set; }

        public string IpAssignText
        {
            get { return Mode == 0 ? "静态" : "动态"; }
        }

        public static async Task Scan(List<Management> managements, Action<Management> onDeviceFound = null, Action<ScanProgressInfo> onProgress = null, CancellationToken cancellationToken = default(CancellationToken))
        {
            cancellationToken.ThrowIfCancellationRequested();
            managements.Clear();
            var prefixes = GetLocalIpv4Prefixes();
            LastScanDiagnostics = BuildScanDiagnostics(prefixes);
            ScanBegin(prefixes);
            AppLogger.Info("Scan diagnostics: " + LastScanDiagnostics.Replace(Environment.NewLine, " | "));
            var tasks = new List<Task>();
            object lockObj = new object();
            var scannedIps = new HashSet<string>();
            var candidateIps = new List<string>();

            foreach (string ip in BuildScanCandidateIps(prefixes))
            {
                cancellationToken.ThrowIfCancellationRequested();
                if (scannedIps.Add(ip))
                    candidateIps.Add(ip);
            }

            var progressState = new ScanProgressState();
            progressState.Total = candidateIps.Count;
            progressState.StartedAtUtc = DateTime.UtcNow;
            progressState.OnProgress = onProgress;
            progressState.FoundCountProvider = () =>
            {
                lock (lockObj)
                {
                    return managements.Count;
                }
            };

            ReportScanProgress(progressState, string.Empty, "准备扫描", false);

            using (var scanLimiter = new SemaphoreSlim(ScanConcurrencyLimit))
            {
                foreach (string ip in candidateIps)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    tasks.Add(ScanIpLimitedAsync(ip, managements, lockObj, scanLimiter, onDeviceFound, progressState, cancellationToken));
                }
                await Task.WhenAll(tasks.ToArray());
            }
            cancellationToken.ThrowIfCancellationRequested();
            managements.Sort((left, right) => CompareIp(left.EthIp, right.EthIp));
            ReportScanProgress(progressState, string.Empty, "扫描完成", true);
            ScanEnd(managements);
        }

        public static Management WaitForDevice(IPAddress ethIp, int timeoutMs)
        {
            if (ethIp == null)
                throw new ArgumentNullException("ethIp");
            if (timeoutMs <= 0)
                throw new ArgumentOutOfRangeException("timeoutMs");

            DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            Exception lastError = null;
            while (DateTime.UtcNow < deadline)
            {
                Management device = new Management(ethIp);
                try
                {
                    device.Connect();
                    device.Upload();
                    device.Disconnect();
                    return device;
                }
                catch (Exception ex)
                {
                    lastError = ex;
                    device.Disconnect();
                    Thread.Sleep(500);
                }
            }

            string message = "Device did not come online at " + ethIp + " within " + timeoutMs + " ms";
            if (lastError != null)
                message += ": " + lastError.Message;
            throw new TimeoutException(message);
        }

        public void CopyConnectionStateFrom(Management source)
        {
            if (source == null)
                throw new ArgumentNullException("source");

            EthIp = source.EthIp;
            DeviceModel = source.DeviceModel;
            DeviceId = source.DeviceId;
            Mode = source.Mode;
            Ip = source.Ip;
            Sn = source.Sn;
            Gw = source.Gw;
            Dns = source.Dns;
            Mac = source.Mac;
            Host = source.Host;
            Port = source.Port;
            Username = source.Username;
            Password = source.Password;
            for (int i = 0; i < TopicCount; i++)
            {
                Topics[i] = source.Topics[i];
                Qos[i] = source.Qos[i];
            }
            NtpServer = source.NtpServer;
            TlsMode = source.TlsMode;
            TlsVerifyPeer = source.TlsVerifyPeer;
        }

        public bool Connect()
        {
            for (int attempt = 0; attempt < ManagementConnectRetryCount; attempt++)
            {
                TcpClient client = new TcpClient();
                try
                {
                    client.Connect(EthIp, ModbusPort);
                    AttachConnectedClient(client);
                    return true;
                }
                catch (Exception)
                {
                    client.Close();
                    if (attempt + 1 < ManagementConnectRetryCount)
                        Thread.Sleep(ManagementConnectRetryDelayMs);
                }
            }

            throw new SocketException((int)SocketError.TimedOut);
        }

        private bool Connect(TcpClient connectedClient)
        {
            AttachConnectedClient(connectedClient);
            return true;
        }

        private void AttachConnectedClient(TcpClient connectedClient)
        {
            connectedClient.NoDelay = true;
            connectedClient.LingerState = new LingerOption(true, 0);
            tcpClient = connectedClient;
            master = Factory.CreateMaster(tcpClient);
            master.Transport.Retries = 2;
            master.Transport.WaitToRetryMilliseconds = 200;
            master.Transport.ReadTimeout = 1000;
            master.Transport.WriteTimeout = 1000;
        }

        public bool Load()
        {
            string error = ValidateForWrite();
            if (error != null)
                throw new InvalidOperationException(error);
            byte[] bytes = ToConfigBytes();
            ushort[] registers = new ushort[ConfigSizeBytes / 2];
            Buffer.BlockCopy(bytes, 0, registers, 0, ConfigSizeBytes);
            WriteRegistersBlock(registers);
            return true;
        }

        public bool Upload()
        {
            ushort[] registers = ReadRegistersBlock(ConfigSizeBytes / 2);
            byte[] bytes = new byte[ConfigSizeBytes];
            Buffer.BlockCopy(registers, 0, bytes, 0, ConfigSizeBytes);
            FromConfigBytes(bytes);
            return true;
        }

        public bool Reset()
        {
            master.WriteSingleCoil(SlaveId, 1, true);
            Status = "设置成功";
            return true;
        }

        public bool Save()
        {
            master.WriteSingleCoil(SlaveId, 0, true);
            Status = "设置成功";
            return true;
        }

        public bool SaveAndRestart()
        {
            Save();
            Reset();
            Status = "设置成功，设备重启中";
            return true;
        }

        public string ValidateForWrite()
        {
            string error;
            if (!ValidateTextLength("设备ID", DeviceId, DeviceIdLength, out error))
                return error;
            if (!ValidateTextLength("服务器IP或域名", Host, HostLength, out error))
                return error;
            if (string.IsNullOrWhiteSpace(Host))
                return "服务器IP或域名不能为空。";
            if (Port == 0)
                return "端口必须是 1-65535。";
            if (!ValidateTextLength("用户名", Username, UsernameLength, out error))
                return error;
            if (!ValidateTextLength("登录密码", Password, PasswordLength, out error))
                return error;
            for (int i = 0; i < TopicCount; i++)
            {
                if (!ValidateTextLength("主题" + (i + 1), Topics[i], TopicLength, out error))
                    return error;
                if (string.IsNullOrWhiteSpace(Topics[i]))
                    return "主题" + (i + 1) + "不能为空。";
                if (Qos[i] > 2)
                    return "Qos 只能是 0、1 或 2。";
            }
            if (!ValidateTextLength("NTP地址", NtpServer, NtpServerLength, out error))
                return error;
            if (Mode > 1)
                return "IP分配模式无效，只能是 DHCP 或静态 IP。";
            error = ValidateStaticNetwork();
            if (error != null)
                return error;
            if (Mode == 0)
            {
                if (Ip == null || Sn == null || Gw == null || Dns == null)
                    return "静态 IP 模式下 IP、子网掩码、网关和 DNS 都不能为空。";
            }
            return null;
        }

        private string ValidateStaticNetwork()
        {
            if (!IsValidUnicastMac(Mac))
                return "MAC地址不能为空、全 FF 或组播地址。";
            if (Mode != 0)
                return null;
            return ValidateStaticNetworkFields(Ip, Sn, Gw, Dns, Mac);
        }

        public static string ValidateStaticNetworkFields(IPAddress ip, IPAddress sn, IPAddress gw, IPAddress dns, PhysicalAddress mac)
        {
            if (ip == null || sn == null || gw == null || dns == null)
                return "静态 IP 模式下 IP、子网掩码、网关和 DNS 都不能为空。";
            if (!IsUsableHostAddress(ip))
                return "静态 IP 地址不能是 0.0.0.0、广播、回环或组播地址。";
            if (!IsValidSubnetMask(sn))
                return "子网掩码必须是连续掩码，且不能是 0.0.0.0 或 255.255.255.255。";
            if (IsNetworkOrBroadcast(ip, sn))
                return "静态 IP 地址不能是当前子网的网络地址或广播地址。";
            if (!IsUsableHostAddress(gw))
                return "默认网关不能是 0.0.0.0、广播、回环或组播地址。";
            if (!IsSameSubnet(ip, gw, sn))
                return "默认网关必须和静态 IP 在同一子网。";
            if (ip.Equals(gw))
                return "静态 IP 地址不能和默认网关相同。";
            if (!IsUsableHostAddress(dns))
                return "DNS服务器不能是 0.0.0.0、广播、回环或组播地址。";
            if (!IsValidUnicastMac(mac))
                return "MAC地址不能为空、全 FF 或组播地址。";
            return null;
        }

        public void Disconnect()
        {
            if (master != null)
            {
                master.Dispose();
                master = null;
            }
            if (tcpClient != null)
            {
                tcpClient.Close();
                tcpClient = null;
            }
        }

        public byte[] ToConfigBytes()
        {
            byte[] bytes = new byte[ConfigSizeBytes];
            bytes[0] = Mode;
            CopyIp(bytes, 4, Ip);
            CopyIp(bytes, 8, Sn);
            CopyIp(bytes, 12, Gw);
            CopyIp(bytes, 16, Dns);
            CopyMac(bytes, 20, Mac);
            CopyString(bytes, DeviceIdOffset, DeviceIdLength, DeviceId);
            CopyString(bytes, HostOffset, HostLength, Host);
            BitConverter.GetBytes(Port).CopyTo(bytes, PortOffset);
            CopyString(bytes, UsernameOffset, UsernameLength, Username);
            CopyString(bytes, PasswordOffset, PasswordLength, Password);
            for (int i = 0; i < TopicCount; i++)
            {
                CopyString(bytes, TopicsOffset + i * TopicLength, TopicLength, Topics[i]);
                bytes[QosOffset + i] = Qos[i];
            }
            CopyString(bytes, NtpOffset, NtpServerLength, NtpServer);
            bytes[TlsModeOffset] = TlsMode;
            bytes[TlsVerifyPeerOffset] = TlsVerifyPeer;
            return bytes;
        }

        public void FromConfigBytes(byte[] bytes)
        {
            Mode = bytes[0];
            Ip = new IPAddress(bytes.Skip(4).Take(4).ToArray());
            Sn = new IPAddress(bytes.Skip(8).Take(4).ToArray());
            Gw = new IPAddress(bytes.Skip(12).Take(4).ToArray());
            Dns = new IPAddress(bytes.Skip(16).Take(4).ToArray());
            Mac = new PhysicalAddress(bytes.Skip(20).Take(6).ToArray());
            DeviceId = ReadString(bytes, DeviceIdOffset, DeviceIdLength, "GM400");
            Host = ReadString(bytes, HostOffset, HostLength, "192.168.0.110");
            Port = BitConverter.ToUInt16(bytes, PortOffset);
            if (Port == 0)
                Port = 1883;
            Username = ReadString(bytes, UsernameOffset, UsernameLength, string.Empty);
            Password = ReadString(bytes, PasswordOffset, PasswordLength, string.Empty);
            for (int i = 0; i < TopicCount; i++)
            {
                Topics[i] = ReadString(bytes, TopicsOffset + i * TopicLength, TopicLength, BuildDefaultTopics()[i]);
                Qos[i] = bytes[QosOffset + i] > 2 ? (byte)0 : bytes[QosOffset + i];
            }
            NtpServer = ReadString(bytes, NtpOffset, NtpServerLength, "pool.ntp.org");
            TlsMode = bytes[TlsModeOffset] > 2 ? (byte)0 : bytes[TlsModeOffset];
            TlsVerifyPeer = bytes[TlsVerifyPeerOffset] == 0 ? (byte)0 : (byte)1;
        }

        public static string[] BuildDefaultTopics()
        {
            string requestTopic = "v1/devices/request/{device_name}";
            string responseTopic = "v1/devices/response/{device_name}";
            string[] topics = new string[TopicCount];
            topics[0] = responseTopic;
            topics[1] = requestTopic;
            topics[2] = responseTopic;
            topics[3] = requestTopic;
            topics[4] = responseTopic;
            return topics;
        }

        private static Task ScanIpAsync(string ip, List<Management> managements, object lockObj, int timeoutMs, bool retryUntilReady, Action<Management> onDeviceFound, CancellationToken cancellationToken)
        {
            return Task.Run(() => ScanIp(ip, managements, lockObj, timeoutMs, retryUntilReady, onDeviceFound, cancellationToken), cancellationToken);
        }

        private static void ScanIp(string ip, List<Management> managements, object lockObj, int timeoutMs, bool retryUntilReady, Action<Management> onDeviceFound, CancellationToken cancellationToken)
        {
            DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            string lastError = null;
            string stage = retryUntilReady ? "priority-retry" : "subnet";

            do
            {
                cancellationToken.ThrowIfCancellationRequested();
                int attemptTimeoutMs = retryUntilReady ? Math.Min(ScanConnectTimeoutMs, RemainingMilliseconds(deadline)) : timeoutMs;
                if (attemptTimeoutMs <= 0)
                    break;

                ScanCandidate(ip, stage, attemptTimeoutMs);
                using (TcpClient client = new TcpClient())
                {
                    try
                    {
                        if (!ConnectClientWithTimeout(client, ip, attemptTimeoutMs))
                        {
                            lastError = "connect timeout after " + attemptTimeoutMs + "ms";
                            ScanFailure(ip, stage, lastError);
                        }
                        else
                        {
                            Management management = new Management(IPAddress.Parse(ip));
                            try
                            {
                                management.Connect(client);
                                UploadWithRetry(management);
                            }
                            finally
                            {
                                management.Disconnect();
                            }
                            lock (lockObj)
                            {
                                managements.Add(management);
                            }
                            ScanSuccess(ip, stage, management);
                            NotifyDeviceFound(onDeviceFound, management);
                            return;
                        }
                    }
                    catch (Exception ex)
                    {
                        lastError = ex.GetType().Name + ": " + ex.Message;
                        Debug.WriteLine("扫描 " + ip + " 失败: " + ex.Message);
                        ScanFailure(ip, stage, lastError);
                    }
                }

                if (!retryUntilReady)
                    break;
                Thread.Sleep(PriorityScanRetryDelayMs);
            } while (DateTime.UtcNow < deadline);

            AppendPriorityScanDiagnostic(ip, lastError ?? ("connect timeout after " + timeoutMs + "ms"));
        }

        private static int RemainingMilliseconds(DateTime deadline)
        {
            double remaining = (deadline - DateTime.UtcNow).TotalMilliseconds;
            if (remaining <= 0)
                return 0;
            if (remaining > int.MaxValue)
                return int.MaxValue;
            return (int)remaining;
        }

        private static bool ConnectClientWithTimeout(TcpClient client, string ip, int timeoutMs)
        {
            IAsyncResult connectResult = null;
            try
            {
                connectResult = client.BeginConnect(ip, ModbusPort, null, null);
                if (!connectResult.AsyncWaitHandle.WaitOne(timeoutMs))
                    return false;
                client.EndConnect(connectResult);
                return true;
            }
            finally
            {
                if (connectResult != null)
                    connectResult.AsyncWaitHandle.Close();
            }
        }

        private static async Task ScanPriorityCandidatesAsync(IList<string> prefixes, List<Management> managements, object lockObj, HashSet<string> scannedIps)
        {
            DateTime priorityDeadline = DateTime.UtcNow.AddMilliseconds(PriorityScanConnectTimeoutMs);

            foreach (string prefix in prefixes)
            {
                foreach (int host in PriorityScanHosts)
                {
                    string ip = prefix + host;
                    if (!scannedIps.Add(ip))
                        continue;
                    await ScanPriorityIpBlockingAsync(ip, managements, lockObj, priorityDeadline);
                    if (managements.Count > 0)
                        return;
                }
            }
        }

        private static Task ScanPriorityIpBlockingAsync(string ip, List<Management> managements, object lockObj, DateTime deadline)
        {
            return Task.Run(() => ScanPriorityIpBlocking(ip, managements, lockObj, deadline));
        }

        private static void ScanPriorityIpBlocking(string ip, List<Management> managements, object lockObj, DateTime deadline)
        {
            string lastError = null;
            int attemptTimeoutMs = Math.Min(ScanConnectTimeoutMs, RemainingMilliseconds(deadline));
            if (attemptTimeoutMs <= 0)
            {
                AppendPriorityScanDiagnostic(ip, "priority scan timeout");
                ScanFailure(ip, "priority", "priority scan timeout");
                return;
            }

            ScanCandidate(ip, "priority", attemptTimeoutMs);
            using (TcpClient client = new TcpClient())
            {
                try
                {
                    if (!ConnectClientWithTimeout(client, ip, attemptTimeoutMs))
                    {
                        lastError = "connect timeout after " + attemptTimeoutMs + "ms";
                        ScanFailure(ip, "priority", lastError);
                    }
                    else
                    {
                        Management management = new Management(IPAddress.Parse(ip));
                        try
                        {
                            management.Connect(client);
                            UploadWithRetry(management);
                            lock (lockObj)
                            {
                                managements.Add(management);
                            }
                        }
                        finally
                        {
                            management.Disconnect();
                        }
                        ScanSuccess(ip, "priority", management);
                        return;
                    }
                }
                catch (Exception ex)
                {
                    lastError = ex.GetType().Name + ": " + ex.Message;
                    Debug.WriteLine("优先扫描 " + ip + " 失败: " + ex.Message);
                    ScanFailure(ip, "priority", lastError);
                }
            }

            AppendPriorityScanDiagnostic(ip, lastError ?? "priority scan timeout");
        }

        private static async Task ScanIpLimitedAsync(string ip, List<Management> managements, object lockObj, SemaphoreSlim scanLimiter, Action<Management> onDeviceFound, ScanProgressState progressState, CancellationToken cancellationToken)
        {
            await scanLimiter.WaitAsync(cancellationToken);
            try
            {
                cancellationToken.ThrowIfCancellationRequested();
                ReportScanProgress(progressState, ip, "扫描中", false);
                await ScanIpAsync(ip, managements, lockObj, ScanConnectTimeoutMs, false, onDeviceFound, cancellationToken);
            }
            finally
            {
                Interlocked.Increment(ref progressState.Completed);
                ReportScanProgress(progressState, ip, "已完成", false);
                scanLimiter.Release();
            }
        }

        private static void ReportScanProgress(ScanProgressState state, string currentIp, string stage, bool isCompleted)
        {
            if (state == null || state.OnProgress == null)
                return;

            int completed = isCompleted ? state.Total : Math.Min(state.Completed, state.Total);
            TimeSpan elapsed = DateTime.UtcNow - state.StartedAtUtc;
            TimeSpan remaining = TimeSpan.Zero;
            if (completed > 0 && state.Total > completed)
            {
                double remainingSeconds = elapsed.TotalSeconds * (state.Total - completed) / completed;
                if (!double.IsNaN(remainingSeconds) && !double.IsInfinity(remainingSeconds) && remainingSeconds > 0)
                    remaining = TimeSpan.FromSeconds(remainingSeconds);
            }

            int foundCount = 0;
            if (state.FoundCountProvider != null)
                foundCount = state.FoundCountProvider();

            try
            {
                state.OnProgress(new ScanProgressInfo(
                    currentIp,
                    stage,
                    completed,
                    state.Total,
                    foundCount,
                    ScanConcurrencyLimit,
                    elapsed,
                    remaining,
                    isCompleted));
            }
            catch (Exception ex)
            {
                AppLogger.Error("Scan progress callback failed", ex);
            }
        }

        private static IEnumerable<string> BuildScanCandidateIps(IList<string> prefixes)
        {
            foreach (string prefix in prefixes)
            {
                foreach (int host in PriorityScanHosts)
                    yield return prefix + host;
            }

            foreach (string prefix in prefixes)
            {
                for (int host = 1; host <= 254; host++)
                    yield return prefix + host;
            }
        }

        private static void NotifyDeviceFound(Action<Management> onDeviceFound, Management management)
        {
            if (onDeviceFound == null)
                return;

            try
            {
                onDeviceFound(management);
            }
            catch (Exception ex)
            {
                AppLogger.Error("Scan device-found callback failed ip=" + management.EthIp + " deviceId=" + management.DeviceId, ex);
            }
        }

        private static void UploadWithRetry(Management management)
        {
            for (int attempt = 1; ; attempt++)
            {
                try
                {
                    AppLogger.Info("ScanUpload ip=" + management.EthIp + " attempt=" + attempt);
                    management.Upload();
                    return;
                }
                catch (Exception ex)
                {
                    AppLogger.Warn("ScanUploadFailure ip=" + management.EthIp + " attempt=" + attempt + " error=" + ex.GetType().Name + ": " + ex.Message);
                    if (attempt >= ScanUploadRetryCount)
                        throw;
                    Thread.Sleep(200);
                }
            }
        }

        private static void ScanBegin(IList<string> prefixes)
        {
            AppLogger.Info("ScanBegin prefixes=" + string.Join(",", prefixes.ToArray()) +
                " modbusPort=" + ModbusPort +
                " connectTimeoutMs=" + ScanConnectTimeoutMs +
                " concurrency=" + ScanConcurrencyLimit +
                " hostRange=1-254" +
                " scanOrder=local-prefixes-first");
        }

        private static void ScanCandidate(string ip, string stage, int timeoutMs)
        {
            AppLogger.Info("ScanCandidate stage=" + stage + " ip=" + ip + " timeoutMs=" + timeoutMs);
        }

        private static void ScanSuccess(string ip, string stage, Management management)
        {
            AppLogger.Info("ScanSuccess stage=" + stage +
                " ip=" + ip +
                " deviceId=" + management.DeviceId +
                " model=" + management.DeviceModel +
                " mode=" + management.IpAssignText +
                " mac=" + management.Mac);
        }

        private static void ScanFailure(string ip, string stage, string message)
        {
            AppLogger.Warn("ScanFailure stage=" + stage + " ip=" + ip + " error=" + message);
        }

        private static void ScanEnd(IList<Management> managements)
        {
            string devices = string.Join(",", managements.Select(item => item.EthIp + "/" + item.DeviceId).ToArray());
            AppLogger.Info("ScanEnd found=" + managements.Count + " devices=" + devices);
        }

        private static void AppendPriorityScanDiagnostic(string ip, string message)
        {
            if (!IsPriorityScanIp(ip))
                return;

            lock (ScanDiagnosticsLock)
            {
                LastScanDiagnostics = (LastScanDiagnostics ?? string.Empty) + Environment.NewLine + ip + "：" + message;
            }
        }

        private static bool IsPriorityScanIp(string ip)
        {
            foreach (int host in PriorityScanHosts)
            {
                if (ip.EndsWith("." + host, StringComparison.Ordinal))
                    return true;
            }
            return false;
        }

        private static List<string> GetLocalIpv4Prefixes()
        {
            var prefixes = new List<string>();

            foreach (NetworkInterface item in NetworkInterface.GetAllNetworkInterfaces())
            {
                if (item.OperationalStatus != OperationalStatus.Up)
                    continue;
                foreach (UnicastIPAddressInformation address in item.GetIPProperties().UnicastAddresses)
                {
                    if (address.Address.AddressFamily != AddressFamily.InterNetwork)
                        continue;
                    byte[] bytes = address.Address.GetAddressBytes();
                    if (bytes[0] == 127)
                        continue;
                    if (bytes[0] == 169 && bytes[1] == 254)
                        continue;
                    AddPrefixIfMissing(prefixes, bytes[0] + "." + bytes[1] + "." + bytes[2] + ".");
                }
            }

            foreach (string prefix in DefaultBoardScanPrefixes)
                AddPrefixIfMissing(prefixes, prefix);
            return prefixes;
        }

        private static void AddPrefixIfMissing(List<string> prefixes, string prefix)
        {
            if (!prefixes.Contains(prefix))
                prefixes.Add(prefix);
        }

        private static string BuildScanDiagnostics(IList<string> prefixes)
        {
            var lines = new List<string>();
            bool hasDirectBoardSubnet = false;
            bool hasApipaEthernet = false;

            foreach (NetworkInterface item in NetworkInterface.GetAllNetworkInterfaces())
            {
                if (item.OperationalStatus != OperationalStatus.Up)
                    continue;

                foreach (UnicastIPAddressInformation address in item.GetIPProperties().UnicastAddresses)
                {
                    if (address.Address.AddressFamily != AddressFamily.InterNetwork)
                        continue;

                    string ip = address.Address.ToString();
                    lines.Add(item.Name + "=" + ip);
                    AppLogger.Info("NetworkAdapter name=" + item.Name +
                        " type=" + item.NetworkInterfaceType +
                        " status=" + item.OperationalStatus +
                        " ipv4=" + ip);
                    if (ip.StartsWith("192.168.0.", StringComparison.Ordinal))
                        hasDirectBoardSubnet = true;
                    if (ip.StartsWith("169.254.", StringComparison.Ordinal) &&
                        item.Name.IndexOf("以太网", StringComparison.OrdinalIgnoreCase) >= 0)
                        hasApipaEthernet = true;
                }
            }

            string scanned = string.Join(",", prefixes.ToArray());
            if (hasDirectBoardSubnet)
                return "扫描网段：" + scanned + "；本机已存在 192.168.0.x 地址。";
            if (hasApipaEthernet)
                return "扫描网段：" + scanned + "；以太网当前是 169.254 自动地址，直连板卡请给以太网配置 192.168.0.x/24。";
            return "扫描网段：" + scanned + "；直连板卡默认回退地址通常是 192.168.0.30，请确认电脑以太网也在 192.168.0.x/24。";
        }

        private static int CompareIp(IPAddress left, IPAddress right)
        {
            byte[] leftBytes = left.GetAddressBytes();
            byte[] rightBytes = right.GetAddressBytes();
            for (int i = 0; i < 4; i++)
            {
                int result = leftBytes[i].CompareTo(rightBytes[i]);
                if (result != 0)
                    return result;
            }
            return 0;
        }

        private ushort[] ReadRegistersBlock(int totalRegisters)
        {
            ushort[] result = new ushort[totalRegisters];
            int offset = 0;
            while (offset < totalRegisters)
            {
                int count = Math.Min(MaxRegistersPerRequest, totalRegisters - offset);
                ushort[] part = master.ReadHoldingRegisters(SlaveId, (ushort)offset, (ushort)count);
                Array.Copy(part, 0, result, offset, part.Length);
                offset += count;
            }
            return result;
        }

        private void WriteRegistersBlock(ushort[] registers)
        {
            int offset = 0;
            while (offset < registers.Length)
            {
                int count = Math.Min(MaxRegistersPerRequest, registers.Length - offset);
                ushort[] part = new ushort[count];
                Array.Copy(registers, offset, part, 0, count);
                master.WriteMultipleRegisters(SlaveId, (ushort)offset, part);
                offset += count;
            }
        }

        private static void CopyIp(byte[] bytes, int offset, IPAddress address)
        {
            address.GetAddressBytes().CopyTo(bytes, offset);
        }

        private static void CopyMac(byte[] bytes, int offset, PhysicalAddress address)
        {
            byte[] mac = address.GetAddressBytes();
            Array.Copy(mac, 0, bytes, offset, Math.Min(6, mac.Length));
        }

        private static void CopyString(byte[] bytes, int offset, int length, string value)
        {
            byte[] encoded = Encoding.UTF8.GetBytes(value ?? string.Empty);
            Array.Copy(encoded, 0, bytes, offset, Math.Min(length - 1, encoded.Length));
        }

        private static bool IsUsableHostAddress(IPAddress address)
        {
            if (!IsIpv4Address(address))
                return false;
            byte[] bytes = address.GetAddressBytes();
            if (bytes.All(item => item == 0) || bytes.All(item => item == 255))
                return false;
            if (bytes[0] == 0 || bytes[0] == 127 || bytes[0] >= 224)
                return false;
            if (bytes[0] == 169 && bytes[1] == 254)
                return false;
            return true;
        }

        private static bool IsValidSubnetMask(IPAddress mask)
        {
            if (!IsIpv4Address(mask))
                return false;
            uint value = ToUInt32(mask);
            if (value == 0U || value == 0xFFFFFFFFU)
                return false;
            uint inverted = ~value;
            return (inverted & (inverted + 1U)) == 0U;
        }

        private static bool IsSameSubnet(IPAddress left, IPAddress right, IPAddress mask)
        {
            if (!IsIpv4Address(left) || !IsIpv4Address(right) || !IsValidSubnetMask(mask))
                return false;
            uint maskValue = ToUInt32(mask);
            return (ToUInt32(left) & maskValue) == (ToUInt32(right) & maskValue);
        }

        private static bool IsNetworkOrBroadcast(IPAddress address, IPAddress mask)
        {
            if (!IsIpv4Address(address) || !IsValidSubnetMask(mask))
                return true;
            uint maskValue = ToUInt32(mask);
            uint hostMask = ~maskValue;
            uint host = ToUInt32(address) & hostMask;
            return host == 0U || host == hostMask;
        }

        private static bool IsValidUnicastMac(PhysicalAddress address)
        {
            if (address == null)
                return false;
            byte[] bytes = address.GetAddressBytes();
            if (bytes.Length != 6)
                return false;
            if (bytes.All(item => item == 0) || bytes.All(item => item == 255))
                return false;
            return (bytes[0] & 0x01) == 0;
        }

        private static bool IsIpv4Address(IPAddress address)
        {
            return address != null && address.AddressFamily == AddressFamily.InterNetwork;
        }

        private static uint ToUInt32(IPAddress address)
        {
            byte[] bytes = address.GetAddressBytes();
            return ((uint)bytes[0] << 24) |
                   ((uint)bytes[1] << 16) |
                   ((uint)bytes[2] << 8) |
                   bytes[3];
        }

        private static bool ValidateTextLength(string name, string value, int length, out string error)
        {
            int byteCount = Encoding.UTF8.GetByteCount(value ?? string.Empty);
            if (byteCount >= length)
            {
                error = name + "过长，UTF-8 编码后必须小于 " + length + " 字节。";
                return false;
            }
            error = null;
            return true;
        }

        private static string ReadString(byte[] bytes, int offset, int length, string fallback)
        {
            int actualLength = 0;
            while (actualLength < length && bytes[offset + actualLength] != 0)
                actualLength++;
            if (actualLength == 0)
                return fallback;
            return Encoding.UTF8.GetString(bytes, offset, actualLength).Trim();
        }
    }
}
