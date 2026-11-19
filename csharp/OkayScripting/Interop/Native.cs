// OkayScripting: the engine's function table (the host fills it once). Mirrors OkayNativeApi in
// engine/include/okay/CSharp/ManagedBridge.hpp: same field order, only integers, floats and pointers.
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace OkayScripting.Interop
{
    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct NativeApi
    {
        public uint size;
        public uint version;
        public delegate* unmanaged[Cdecl]<int, byte*, void> Log;
        public delegate* unmanaged[Cdecl]<ulong, int, float*, int> GetVec;
        public delegate* unmanaged[Cdecl]<ulong, int, float*, int> SetVec;
        public delegate* unmanaged[Cdecl]<int, double> GetTime;
        public delegate* unmanaged[Cdecl]<float, void> SetTimeScale;
        public delegate* unmanaged[Cdecl]<byte*, int, int> InputKey;
        public delegate* unmanaged[Cdecl]<int, int, int> InputMouse;
        public delegate* unmanaged[Cdecl]<float*, void> MousePosition;
        public delegate* unmanaged[Cdecl]<byte*, float> InputAxis;
        public delegate* unmanaged[Cdecl]<byte*, ulong> Find;
        public delegate* unmanaged[Cdecl]<byte*, ulong> Create;
        public delegate* unmanaged[Cdecl]<ulong, float, void> Destroy;
        public delegate* unmanaged[Cdecl]<ulong, float*, float*, ulong> Instantiate;
        public delegate* unmanaged[Cdecl]<ulong, int> IsAlive;
        public delegate* unmanaged[Cdecl]<ulong, byte*, int, int> GetName;
        public delegate* unmanaged[Cdecl]<ulong, int> GetActive;
        public delegate* unmanaged[Cdecl]<ulong, int, void> SetActive;
        public delegate* unmanaged[Cdecl]<float*, float*, float, float*, float*, float*, ulong*, int> Raycast;
        public delegate* unmanaged[Cdecl]<ulong, byte*, int> HasComponent;
        public delegate* unmanaged[Cdecl]<ulong, byte*, byte*, int, int> GetProperty;
        public delegate* unmanaged[Cdecl]<ulong, byte*, byte*, int> SetProperty;
    }

    internal static unsafe class Native
    {
        public const uint ExpectedVersion = 1;
        public static NativeApi Api;
        public static bool Ready;

        public static byte[] Utf8(string s) => Encoding.UTF8.GetBytes((s ?? "") + "\0");

        public static string ReadUtf8(byte* p)
        {
            if (p == null) return "";
            int n = 0; while (p[n] != 0) n++;
            return Encoding.UTF8.GetString(p, n);
        }

        public static void WriteUtf8(string s, byte* buf, int cap)
        {
            if (buf == null || cap <= 0) return;
            byte[] b = Encoding.UTF8.GetBytes(s ?? "");
            int n = Math.Min(b.Length, cap - 1);
            for (int i = 0; i < n; i++) buf[i] = b[i];
            buf[n] = 0;
        }

        public static void Log(int level, string msg)
        {
            if (!Ready) return;
            fixed (byte* p = Utf8(msg)) Api.Log(level, p);
        }

        public static string GetString(ulong go, string path)
        {
            fixed (byte* pp = Utf8(path))
            {
                byte* buf = stackalloc byte[512];
                int n = Api.GetProperty(go, pp, buf, 512);
                if (n < 0) return null;
                if (n < 512) return ReadUtf8(buf);
                byte[] big = new byte[n + 1];
                fixed (byte* bp = big) { Api.GetProperty(go, pp, bp, n + 1); return ReadUtf8(bp); }
            }
        }
    }
}
