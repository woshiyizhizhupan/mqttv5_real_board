using System;
using System.Linq;
using System.Windows.Forms;

namespace EmqxDeviceEmulator.App
{
    internal static class Program
    {
        [STAThread]
        private static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            bool autoConnect = args != null && args.Any(arg => string.Equals(arg, "--autoconnect", StringComparison.OrdinalIgnoreCase));
            Application.Run(new DeviceEmulatorForm(autoConnect));
        }
    }
}
