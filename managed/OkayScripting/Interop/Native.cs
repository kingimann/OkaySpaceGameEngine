// OkayScripting: the engine's function table (the host fills it once). Mirrors OkayNativeApi in
// engine/include/okay/Scripting/CSharpBridge.hpp: same field order, only integers, floats and pointers.
using System;
using System.Collections.Generic;
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
        // version 2: the generic binding layer
        public delegate* unmanaged[Cdecl]<ulong, byte*, int, OkayValue*, int> FindComponent;
        public delegate* unmanaged[Cdecl]<ulong, byte*, OkayValue*, byte*, int, int> AddComponent;
        public delegate* unmanaged[Cdecl]<OkayValue*, byte*, OkayValue*, byte*, int, int> GetMember;
        public delegate* unmanaged[Cdecl]<OkayValue*, byte*, OkayValue*, byte*, int, int> SetMember;
        public delegate* unmanaged[Cdecl]<OkayValue*, byte*, OkayValue*, int, OkayValue*, byte*, int, int> CallMethod;
        public delegate* unmanaged[Cdecl]<uint, byte*, int, int> ClassName;
    }

    /// <summary>Mirrors OkayValue in CSharpBridge.hpp. kind: 0 none, 1 bool, 2 number, 3 text, 4 vector,
    /// 5 colour / Vector4, 6 object, 7 component, 8 rotation (to the engine only).</summary>
    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct OkayValue
    {
        public int kind;
        public uint classKey;
        public uint index;
        public uint pad;
        public double x, y, z, w;
        public ulong id;
        public byte* text;
    }

    internal static unsafe class Native
    {
        public const uint ExpectedVersion = 2;
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

        /// <summary>A C# value as an engine value. Text is pinned by the caller (see Pin).</summary>
        public static OkayValue ToValue(object v, List<GCHandle> pins)
        {
            var o = new OkayValue();
            switch (v)
            {
                case null: break;
                case bool b: o.kind = 1; o.x = b ? 1 : 0; break;
                case int i: o.kind = 2; o.x = i; break;
                case float f: o.kind = 2; o.x = f; break;
                case double d: o.kind = 2; o.x = d; break;
                case long l: o.kind = 2; o.x = l; break;
                case string s:
                {
                    var h = GCHandle.Alloc(Utf8(s), GCHandleType.Pinned); pins.Add(h);
                    o.kind = 3; o.text = (byte*)h.AddrOfPinnedObject(); break;
                }
                case Vector2 v2: o.kind = 4; o.x = v2.x; o.y = v2.y; break;
                case Vector3 v3: o.kind = 4; o.x = v3.x; o.y = v3.y; o.z = v3.z; break;
                case Vector4 v4: o.kind = 5; o.x = v4.x; o.y = v4.y; o.z = v4.z; o.w = v4.w; break;
                case Color c: o.kind = 5; o.x = c.r; o.y = c.g; o.z = c.b; o.w = c.a; break;
                case Quaternion q: o.kind = 8; o.x = q.x; o.y = q.y; o.z = q.z; o.w = q.w; break;
                case GameObject g: o.kind = 6; o.id = g.id; break;
                case Component c: o.kind = 7; o.id = c.ownerId; o.classKey = c.classKey; o.index = c.index; break;
                default: throw new ArgumentException("a " + v.GetType().Name + " cannot be passed to the engine");
            }
            return o;
        }

        /// <summary>An engine value as the C# type the generated code asked for.</summary>
        public static object FromValue(in OkayValue v, Type want)
        {
            if (want == typeof(void) || want == typeof(object) && v.kind == 0) return null;
            switch (v.kind)
            {
                case 1: if (want == typeof(bool)) return v.x != 0; break;
                case 2:
                    if (want == typeof(float)) return (float)v.x;
                    if (want == typeof(int)) return (int)Math.Round(v.x);
                    if (want == typeof(double)) return v.x;
                    if (want == typeof(bool)) return v.x != 0;
                    if (want == typeof(string)) return v.x.ToString(System.Globalization.CultureInfo.InvariantCulture);
                    break;
                case 3:
                {
                    string s = ReadUtf8(v.text);
                    if (want == typeof(string) || want == typeof(object)) return s;
                    if (want == typeof(int) && int.TryParse(s, out int i)) return i;
                    break;
                }
                case 4:
                    if (want == typeof(Vector3) || want == typeof(object)) return new Vector3((float)v.x, (float)v.y, (float)v.z);
                    if (want == typeof(Vector2)) return new Vector2((float)v.x, (float)v.y);
                    if (want == typeof(Quaternion)) return Quaternion.Euler((float)v.x, (float)v.y, (float)v.z);   // the engine gives rotations as Euler degrees
                    if (want == typeof(Vector4)) return new Vector4((float)v.x, (float)v.y, (float)v.z, 0f);
                    break;
                case 5:
                    if (want == typeof(Color) || want == typeof(object)) return new Color((float)v.x, (float)v.y, (float)v.z, (float)v.w);
                    if (want == typeof(Vector4)) return new Vector4((float)v.x, (float)v.y, (float)v.z, (float)v.w);
                    break;
                case 6: if (want == typeof(GameObject) || want == typeof(object)) return GameObject.FromId(v.id); break;
                case 7: if (typeof(Component).IsAssignableFrom(want) || want == typeof(object)) return Component.FromHandle(v.id, v.classKey, v.index); break;
                case 0:
                    if (!want.IsValueType) return null;
                    return Activator.CreateInstance(want);
            }
            if (!want.IsValueType) return null;
            throw new InvalidCastException("the engine gave a value of kind " + v.kind + " where a " + want.Name + " was expected");
        }

        public static string ClassName(uint key)
        {
            byte* buf = stackalloc byte[256];
            int n = Api.ClassName(key, buf, 256);
            return n < 0 ? "" : ReadUtf8(buf);
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
