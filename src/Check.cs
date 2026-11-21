using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using Microsoft.Win32;
using Valve.VR;

// Standalone diagnostics. This program never opens or modifies a game process.
internal static class Check
{
    private const uint CandidateTimestamp = 0x6a8332d9;
    private const string CandidateHash = "F6F7104AF2BD0C2B931EA1E739969E85845E499AFFBF686144C6B490B344AB1E";
    private static volatile bool cancelled;

    private static string Runtime(RegistryView view)
    {
        using (RegistryKey root = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view))
        using (RegistryKey key = root.OpenSubKey(@"SOFTWARE\Khronos\OpenXR\1"))
            return key == null ? null : key.GetValue("ActiveRuntime") as string;
    }

    private static Dictionary<string, object> InspectGame(string path)
    {
        using (FileStream stream = File.OpenRead(path))
        using (BinaryReader reader = new BinaryReader(stream))
        {
            if (stream.Length < 64 || reader.ReadUInt16() != 0x5a4d)
                throw new InvalidDataException("Not a Windows executable.");
            stream.Position = 60;
            int pe = reader.ReadInt32();
            if (pe < 64 || pe > stream.Length - 24)
                throw new InvalidDataException("Invalid PE offset.");
            stream.Position = pe;
            if (reader.ReadUInt32() != 0x4550)
                throw new InvalidDataException("Invalid PE signature.");
            ushort machine = reader.ReadUInt16();
            reader.ReadUInt16();
            uint timestamp = reader.ReadUInt32();
            stream.Position = 0;
            string hash;
            using (SHA256 sha = SHA256.Create())
                hash = BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "");
            bool candidate = machine == 0x14c && timestamp == CandidateTimestamp && hash == CandidateHash;
            return new Dictionary<string, object> {
                { "path", Path.GetFullPath(path) }, { "bytes", stream.Length },
                { "machine", "0x" + machine.ToString("X4") },
                { "timestamp", "0x" + timestamp.ToString("X8") }, { "sha256", hash },
                { "matchesResearchCandidate", candidate },
                { "gameHooksValidated", false }
            };
        }
    }

    private static bool ValidPose(TrackedDevicePose_t[] poses, uint device)
    {
        return device < poses.Length && poses[device].bDeviceIsConnected && poses[device].bPoseIsValid
            && poses[device].eTrackingResult == ETrackingResult.Running_OK;
    }

    private static object Device(CVRSystem system, TrackedDevicePose_t[] poses, uint device)
    {
        if (device >= poses.Length) return null;
        ETrackedPropertyError error = ETrackedPropertyError.TrackedProp_Success;
        StringBuilder name = new StringBuilder(256);
        system.GetStringTrackedDeviceProperty(device, ETrackedDeviceProperty.Prop_ModelNumber_String,
            name, (uint)name.Capacity, ref error);
        TrackedDevicePose_t p = poses[device];
        HmdMatrix34_t m = p.mDeviceToAbsoluteTracking;
        return new Dictionary<string, object> {
            { "model", error == ETrackedPropertyError.TrackedProp_Success ? name.ToString() : null },
            { "connected", p.bDeviceIsConnected }, { "valid", ValidPose(poses, device) },
            { "trackingResult", p.eTrackingResult.ToString() },
            { "deviceToStanding", new float[] { m.m0,m.m1,m.m2,m.m3,m.m4,m.m5,m.m6,m.m7,m.m8,m.m9,m.m10,m.m11 } }
        };
    }

    private static bool ReadController(CVRSystem system, TrackedDevicePose_t[] poses, uint device)
    {
        if (device >= poses.Length) return false;
        VRControllerState_t state = new VRControllerState_t();
        TrackedDevicePose_t pose = new TrackedDevicePose_t();
        bool available = system.GetControllerStateWithPose(ETrackingUniverseOrigin.TrackingUniverseStanding,
            device, ref state, (uint)Marshal.SizeOf(typeof(VRControllerState_t)), ref pose);
        if (available) poses[device] = pose;
        return available;
    }

    private static bool Probe(Dictionary<string, object> report, int seconds)
    {
        // Background mode plus this check avoids launching SteamVR just to inspect files.
        Process[] servers = Process.GetProcessesByName("vrserver");
        bool running = servers.Length > 0;
        foreach (Process process in servers) process.Dispose();
        if (!running)
        {
            report["trackingStatus"] = "SteamVR is not running. Connect Quest to the PC and start SteamVR, then run again.";
            return false;
        }
        EVRInitError error = EVRInitError.None;
        CVRSystem system = OpenVR.Init(ref error, EVRApplicationType.VRApplication_Background);
        if (error != EVRInitError.None || system == null)
        {
            report["openVRInitError"] = error.ToString();
            report["trackingStatus"] = error == EVRInitError.Init_HmdNotFound
                ? "SteamVR is running but cannot see a VR headset. Inside Quest, launch Quest Link/Air Link to this PC, or connect using Steam Link/Virtual Desktop. A USB cable alone does not confirm an active PC VR session."
                : "OpenVR initialization failed: " + error;
            return false;
        }
        try
        {
            uint width = 0, height = 0;
            system.GetRecommendedRenderTargetSize(ref width, ref height);
            report["recommendedEyeTexture"] = new uint[] { width, height };
            TrackedDevicePose_t[] poses = new TrackedDevicePose_t[OpenVR.k_unMaxTrackedDeviceCount];
            int samples = 0, together = 0, hmd = 0, left = 0, right = 0;
            int nextPrint = 0;
            Stopwatch timer = Stopwatch.StartNew();
            Console.WriteLine("Sampling tracking for {0}s. Move your headset and both controllers. Ctrl+C stops.", seconds);
            while (!cancelled && timer.Elapsed.TotalSeconds < seconds)
            {
                VREvent_t vrEvent = new VREvent_t();
                while (system.PollNextEvent(ref vrEvent, (uint)Marshal.SizeOf(typeof(VREvent_t))))
                {
                    if (vrEvent.eventType == (uint)EVREventType.VREvent_Quit) cancelled = true;
                }
                if (cancelled) break;
                system.GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin.TrackingUniverseStanding, 0, poses);
                uint li = system.GetTrackedDeviceIndexForControllerRole(ETrackedControllerRole.LeftHand);
                uint ri = system.GetTrackedDeviceIndexForControllerRole(ETrackedControllerRole.RightHand);
                report["leftControllerStateAvailable"] = ReadController(system, poses, li);
                report["rightControllerStateAvailable"] = ReadController(system, poses, ri);
                report["inputAvailable"] = system.IsInputAvailable();
                report["steamVRDashboardVisible"] = OpenVR.Overlay != null && OpenVR.Overlay.IsDashboardVisible();
                report["applicationShouldPause"] = system.ShouldApplicationPause();
                bool hv = ValidPose(poses, OpenVR.k_unTrackedDeviceIndex_Hmd);
                bool lv = ValidPose(poses, li), rv = ValidPose(poses, ri);
                ++samples;
                if (hv) ++hmd;
                if (lv) ++left;
                if (rv) ++right;
                if (hv && lv && rv) ++together;
                report["lastDevices"] = new Dictionary<string, object> {
                    { "head", Device(system, poses, OpenVR.k_unTrackedDeviceIndex_Hmd) },
                    { "left", Device(system, poses, li) }, { "right", Device(system, poses, ri) }
                };
                if (timer.Elapsed.TotalSeconds >= nextPrint)
                {
                    Console.WriteLine("Head: {0} | Left: {1} | Right: {2} | Input: {3}", hv, lv, rv, report["inputAvailable"]);
                    ++nextPrint;
                }
                Thread.Sleep(20);
            }
            report["samples"] = samples;
            report["validHeadSamples"] = hmd;
            report["validLeftSamples"] = left;
            report["validRightSamples"] = right;
            report["simultaneouslyValidSamples"] = together;
            report["cancelled"] = cancelled;
            bool observed = together >= 25 && !cancelled;
            report["trackingStatus"] = observed
                ? "Head and both controller poses observed. Rendering, buttons, latency and game integration remain untested."
                : (report.ContainsKey("inputAvailable") && !(bool)report["inputAvailable"]
                    ? "Head/controller tracking check incomplete; SteamVR reports input unavailable to this application. Check VR session focus and dismiss system menus."
                    : "Tracking check incomplete. Wake both controllers and check the SteamVR device indicators.");
            return observed;
        }
        finally { OpenVR.Shutdown(); }
    }

    private static void SelfTest()
    {
        string temp = Path.GetTempFileName();
        try
        {
            File.WriteAllBytes(temp, new byte[128]);
            try { InspectGame(temp); throw new Exception("Accepted a non-PE file."); }
            catch (InvalidDataException) { }
            byte[] corrupt = new byte[128];
            corrupt[0] = 0x4d; corrupt[1] = 0x5a;
            Array.Copy(BitConverter.GetBytes(Int32.MaxValue), 0, corrupt, 60, 4);
            File.WriteAllBytes(temp, corrupt);
            try { InspectGame(temp); throw new Exception("Accepted invalid PE offset."); }
            catch (InvalidDataException) { }
            TrackedDevicePose_t[] poses = new TrackedDevicePose_t[1];
            if (ValidPose(poses, UInt32.MaxValue) || ValidPose(poses, 0))
                throw new Exception("Accepted absent tracking.");
            poses[0].bDeviceIsConnected = true;
            poses[0].bPoseIsValid = true;
            poses[0].eTrackingResult = ETrackingResult.Running_OutOfRange;
            if (ValidPose(poses, 0)) throw new Exception("Accepted out-of-range tracking.");
            poses[0].eTrackingResult = ETrackingResult.Running_OK;
            if (!ValidPose(poses, 0)) throw new Exception("Rejected valid tracking.");
            Console.WriteLine("PASS: malformed executables and missing/degraded tracking rejected.");
        }
        finally { File.Delete(temp); }
    }

    private static int Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        Dictionary<string, object> report = new Dictionary<string, object>();
        string output = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "check-report.json");
        try
        {
            string game = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
                @"Steam\steamapps\common\Call of Duty Black Ops II\t6zm.exe");
            int seconds = 15;
            bool inspectOnly = false;
            for (int i = 0; i < args.Length; ++i)
            {
                if (args[i] == "--self-test") { SelfTest(); return 0; }
                else if (args[i] == "--inspect-only") inspectOnly = true;
                else if (args[i] == "--game" && i + 1 < args.Length) game = args[++i];
                else if (args[i] == "--out" && i + 1 < args.Length) output = Path.GetFullPath(args[++i]);
                else if (args[i] == "--seconds" && i + 1 < args.Length) seconds = Int32.Parse(args[++i]);
                else if (args[i] == "--help")
                {
                    Console.WriteLine("BO2VR.Check [--game path/to/t6zm.exe] [--inspect-only] [--seconds 3..300] [--out report.json] [--self-test]");
                    return 0;
                }
                else throw new ArgumentException("Unknown or incomplete option: " + args[i]);
            }
            if (seconds < 3 || seconds > 300) throw new ArgumentException("Seconds must be 3..300.");
            report["utc"] = DateTime.UtcNow.ToString("o");
            report["processBits"] = IntPtr.Size * 8;
            report["playableMod"] = false;
            report["openXR32"] = Runtime(RegistryView.Registry32);
            report["openXR64"] = Runtime(RegistryView.Registry64);
            Dictionary<string, object> executable = InspectGame(game);
            report["game"] = executable;
            Console.WriteLine("BO2 VR development diagnostic -- not a game mod.");
            Console.WriteLine("Game matches research candidate: {0}", executable["matchesResearchCandidate"]);
            Console.CancelKeyPress += delegate(object sender, ConsoleCancelEventArgs e) { e.Cancel = true; cancelled = true; };
            bool tracking = false;
            if (inspectOnly) report["trackingStatus"] = "Not tested (--inspect-only).";
            else tracking = Probe(report, seconds);
            Console.WriteLine(report["trackingStatus"]);
            string parent = Path.GetDirectoryName(Path.GetFullPath(output));
            Directory.CreateDirectory(parent);
            File.WriteAllText(output, new JavaScriptSerializer().Serialize(report), new UTF8Encoding(false));
            Console.WriteLine("Report: " + output);
            return (bool)executable["matchesResearchCandidate"] && (inspectOnly || tracking) ? 0 : 2;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine("Diagnostic failed: " + error.Message);
            return 1;
        }
    }
}

