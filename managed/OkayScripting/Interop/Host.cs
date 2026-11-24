// OkayScripting: the entry points the engine calls (through hostfxr's function-pointer lookup).
// All are [UnmanagedCallersOnly] with blittable parameters only (ints, doubles, pointers). Each script
// instance lives in a collectible AssemblyLoadContext, so recompiling a script can unload the old one.
// Nothing here ever throws into the engine: every script call is wrapped and becomes an error line.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;

namespace OkayScripting.Interop
{
    public static unsafe class Host
    {
        sealed class Ctx : AssemblyLoadContext
        {
            public Ctx() : base(isCollectible: true) { }
            // A script references OkayScripting: give it THIS copy (the one the engine started), so its Behaviour is
            // our Behaviour. Without this the runtime would look for a second copy and the script would not be found.
            protected override Assembly Load(AssemblyName name) =>
                name.Name == typeof(Host).Assembly.GetName().Name ? typeof(Host).Assembly : null;
        }
        sealed class Loaded { public Ctx ctx; public Assembly asm; public int refs; }
        sealed class Instance
        {
            public Behaviour obj; public Type type; public string path;
            public Dictionary<string, MethodInfo> methods = new Dictionary<string, MethodInfo>();
        }

        static readonly Dictionary<string, Loaded> loaded = new Dictionary<string, Loaded>();
        static readonly Dictionary<int, Instance> instances = new Dictionary<int, Instance>();
        static readonly Dictionary<ulong, List<Behaviour>> byOwner = new Dictionary<ulong, List<Behaviour>>();
        static int nextHandle = 1;

        internal static T FindScript<T>(ulong owner) where T : class
        {
            if (!byOwner.TryGetValue(owner, out var list)) return null;
            foreach (var b in list) if (b is T t) return t;
            return null;
        }

        static void Err(string s, byte* buf, int cap) => Native.WriteUtf8(s, buf, cap);

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static int Init(void* table)
        {
            var api = (NativeApi*)table;
            if (api == null || api->size < (uint)sizeof(NativeApi) || api->version != Native.ExpectedVersion) return 1;
            Native.Api = *api; Native.Ready = true;
            return 0;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static int Create(byte* asmPath, byte* className, byte* err, int errCap)
        {
            string path = Native.ReadUtf8(asmPath), cls = Native.ReadUtf8(className);
            try
            {
                if (!loaded.TryGetValue(path, out var l))
                {
                    var ctx = new Ctx();
                    l = new Loaded { ctx = ctx, asm = ctx.LoadFromAssemblyPath(path) };
                    loaded[path] = l;
                }
                Type t = l.asm.GetType(cls, throwOnError: false) ?? FindByName(l.asm, cls);
                if (t == null || !typeof(Behaviour).IsAssignableFrom(t)) { Release(path); Err("class '" + cls + "' was not found or does not inherit from Behaviour", err, errCap); return 0; }
                var obj = (Behaviour)Activator.CreateInstance(t, nonPublic: true);
                l.refs++;
                int h = nextHandle++;
                instances[h] = new Instance { obj = obj, type = t, path = path };
                return h;
            }
            catch (Exception ex)
            {
                Release(path);
                Err(Describe(ex), err, errCap);
                return 0;
            }
        }

        static Type FindByName(Assembly a, string cls)
        {
            foreach (var t in a.GetTypes()) if (t.Name == cls) return t;
            return null;
        }

        static void Release(string path)
        {
            if (!loaded.TryGetValue(path, out var l)) return;
            if (l.refs > 0) return;
            loaded.Remove(path);
            try { l.ctx.Unload(); } catch { }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static void Destroy(int h)
        {
            if (!instances.TryGetValue(h, out var inst)) return;
            instances.Remove(h);
            if (byOwner.TryGetValue(inst.obj.ownerId, out var list)) { list.Remove(inst.obj); if (list.Count == 0) byOwner.Remove(inst.obj.ownerId); }
            if (loaded.TryGetValue(inst.path, out var l)) { l.refs--; if (l.refs <= 0) { l.refs = 0; Release(inst.path); } }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static void SetOwner(int h, ulong id)
        {
            if (!instances.TryGetValue(h, out var inst)) return;
            if (byOwner.TryGetValue(inst.obj.ownerId, out var old)) { old.Remove(inst.obj); if (old.Count == 0) byOwner.Remove(inst.obj.ownerId); }
            inst.obj.ownerId = id;
            if (id != 0)
            {
                if (!byOwner.TryGetValue(id, out var list)) byOwner[id] = list = new List<Behaviour>();
                list.Add(inst.obj);
            }
        }

        const BindingFlags Any = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;

        static MethodInfo FindMethod(Instance inst, string name)
        {
            if (inst.methods.TryGetValue(name, out var m)) return m;
            for (Type t = inst.type; t != null && t != typeof(Behaviour) && t != typeof(object); t = t.BaseType)
            {
                foreach (var cand in t.GetMethods(Any | BindingFlags.DeclaredOnly))
                {
                    if (cand.Name != name) continue;
                    var ps = cand.GetParameters();
                    if (ps.Length == 0 || (ps.Length == 1 && (ps[0].ParameterType == typeof(float) || ps[0].ParameterType == typeof(GameObject) || ps[0].ParameterType == typeof(string))))
                    { m = cand; break; }
                }
                if (m != null) break;
            }
            inst.methods[name] = m;
            return m;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static int Has(int h, byte* name)
        {
            return instances.TryGetValue(h, out var inst) && FindMethod(inst, Native.ReadUtf8(name)) != null ? 1 : 0;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static int Invoke(int h, byte* name, double num, byte* str, ulong obj, byte* err, int errCap)
        {
            try
            {
                if (!instances.TryGetValue(h, out var inst)) { Err("the script no longer exists", err, errCap); return 1; }
                var m = FindMethod(inst, Native.ReadUtf8(name));
                if (m == null) return 0;
                var ps = m.GetParameters();
                object[] args;
                if (ps.Length == 0) args = null;
                else if (ps[0].ParameterType == typeof(float)) args = new object[] { (float)num };
                else if (ps[0].ParameterType == typeof(GameObject)) args = new object[] { obj != 0 ? GameObject.FromId(obj) : GameObject.Find(Native.ReadUtf8(str)) };
                else args = new object[] { Native.ReadUtf8(str) };
                m.Invoke(inst.obj, args);
                return 0;
            }
            catch (Exception ex)
            {
                Err(Describe(ex), err, errCap);
                return 1;
            }
        }

        /// <summary>"line N: ExceptionType: message" (line from the script's own frame when it has debug info).</summary>
        static string Describe(Exception ex)
        {
            if (ex is TargetInvocationException tie && tie.InnerException != null) ex = tie.InnerException;
            int line = 0;
            try
            {
                var st = new StackTrace(ex, true);
                foreach (var f in st.GetFrames())
                {
                    var asm = f.GetMethod()?.DeclaringType?.Assembly;
                    if (asm == null || asm == typeof(Host).Assembly || f.GetFileLineNumber() <= 0) continue;
                    line = f.GetFileLineNumber(); break;
                }
            }
            catch { }
            return "line " + line + ": " + ex.GetType().Name + ": " + ex.Message;
        }

        // ---- public fields
        static IEnumerable<FieldInfo> PublicFields(Type t)
        {
            foreach (var f in t.GetFields(Any))
            {
                if (f.IsStatic || f.IsInitOnly || f.IsLiteral) continue;
                if (f.DeclaringType == typeof(Behaviour)) continue;
                if (f.GetCustomAttribute<HideInInspector>() != null) continue;
                if (!f.IsPublic && f.GetCustomAttribute<SerializeField>() == null) continue;
                if (TypeName(f.FieldType) == null) continue;
                yield return f;
            }
        }
        static string TypeName(Type t) =>
            t == typeof(float) ? "float" : t == typeof(double) ? "double" : t == typeof(int) ? "int" :
            t == typeof(bool) ? "bool" : t == typeof(string) ? "string" : t == typeof(Vector3) ? "Vector3" : null;
        static string Inv(float f) => f.ToString("R", CultureInfo.InvariantCulture);

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static int Fields(int h, byte* buf, int cap)
        {
            try
            {
                if (!instances.TryGetValue(h, out var inst)) return 0;
                var sb = new StringBuilder();
                foreach (var f in PublicFields(inst.type))
                {
                    object v = f.GetValue(inst.obj);
                    string text = v is float fl ? Inv(fl) : v is double d ? d.ToString("R", CultureInfo.InvariantCulture) :
                        v is bool b ? (b ? "true" : "false") : v is Vector3 vec ? Inv(vec.x) + "," + Inv(vec.y) + "," + Inv(vec.z) :
                        Convert.ToString(v, CultureInfo.InvariantCulture) ?? "";
                    sb.Append(f.Name).Append('\t').Append(TypeName(f.FieldType)).Append('\t').Append(text.Replace('\n', ' ').Replace('\t', ' ')).Append('\n');
                }
                byte[] bytes = Encoding.UTF8.GetBytes(sb.ToString());
                if (buf != null && cap > 0) { int n = Math.Min(bytes.Length, cap - 1); for (int i = 0; i < n; i++) buf[i] = bytes[i]; buf[n] = 0; }
                return bytes.Length;
            }
            catch { return 0; }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static int SetField(int h, byte* name, byte* value, byte* err, int errCap)
        {
            try
            {
                if (!instances.TryGetValue(h, out var inst)) { Err("the script no longer exists", err, errCap); return 1; }
                string n = Native.ReadUtf8(name), v = Native.ReadUtf8(value);
                foreach (var f in PublicFields(inst.type))
                {
                    if (f.Name != n) continue;
                    var ic = CultureInfo.InvariantCulture;
                    Type t = f.FieldType;
                    if (t == typeof(float)) f.SetValue(inst.obj, float.Parse(v, NumberStyles.Float, ic));
                    else if (t == typeof(double)) f.SetValue(inst.obj, double.Parse(v, NumberStyles.Float, ic));
                    else if (t == typeof(int)) f.SetValue(inst.obj, (int)Math.Round(double.Parse(v, NumberStyles.Float, ic)));
                    else if (t == typeof(bool)) f.SetValue(inst.obj, v == "true" || v == "True" || v == "1");
                    else if (t == typeof(string)) f.SetValue(inst.obj, v);
                    else if (t == typeof(Vector3))
                    {
                        var p = v.Split(',');
                        f.SetValue(inst.obj, new Vector3(float.Parse(p[0], NumberStyles.Float, ic), float.Parse(p[1], NumberStyles.Float, ic), p.Length > 2 ? float.Parse(p[2], NumberStyles.Float, ic) : 0f));
                    }
                    return 0;
                }
                Err("no field '" + n + "'", err, errCap);
                return 1;
            }
            catch (Exception ex) { Err(ex.GetType().Name + ": " + ex.Message, err, errCap); return 1; }
        }
    }
}
