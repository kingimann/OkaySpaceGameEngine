// OkayScripting: GameObject, Transform, Component, Time, Input, Debug, Physics, Find/Destroy/Instantiate.
// Every call goes through the engine's function table (Interop/Native.cs). Written for this engine.
using System;
using System.Collections.Generic;
using OkayScripting.Interop;

namespace OkayScripting
{
    /// <summary>A private field that should still show in the Inspector (public fields always do).</summary>
    [AttributeUsage(AttributeTargets.Field)] public sealed class SerializeField : Attribute { }
    /// <summary>A public field that should not show in the Inspector.</summary>
    [AttributeUsage(AttributeTargets.Field)] public sealed class HideInInspector : Attribute { }

    public static class Debug
    {
        public static void Log(object o) => Native.Log(0, o == null ? "null" : o.ToString());
        public static void LogWarning(object o) => Native.Log(1, o == null ? "null" : o.ToString());
        public static void LogError(object o) => Native.Log(2, o == null ? "null" : o.ToString());
    }

    public static unsafe class Time
    {
        public static float deltaTime => Native.Ready ? (float)Native.Api.GetTime(0) : 0f;
        public static float time => Native.Ready ? (float)Native.Api.GetTime(1) : 0f;
        public static float timeScale
        {
            get => Native.Ready ? (float)Native.Api.GetTime(2) : 1f;
            set { if (Native.Ready) Native.Api.SetTimeScale(value); }
        }
        public static float unscaledDeltaTime => Native.Ready ? (float)Native.Api.GetTime(3) : 0f;
        public static float fixedDeltaTime => Native.Ready ? (float)Native.Api.GetTime(4) : 0f;
        public static int frameCount => Native.Ready ? (int)Native.Api.GetTime(5) : 0;
    }

    public static unsafe class Input
    {
        static int Key(string name, int mode)
        {
            fixed (byte* p = Native.Utf8(name))
            {
                int r = Native.Api.InputKey(p, mode);
                if (r < 0) throw new ArgumentException("Input: unknown key name '" + name + "'");
                return r;
            }
        }
        public static bool GetKey(string name) => Key(name, 0) != 0;
        public static bool GetKeyDown(string name) => Key(name, 1) != 0;
        public static bool GetKeyUp(string name) => Key(name, 2) != 0;
        public static bool GetMouseButton(int b) => Native.Api.InputMouse(b, 0) != 0;
        public static bool GetMouseButtonDown(int b) => Native.Api.InputMouse(b, 1) != 0;
        public static bool GetMouseButtonUp(int b) => Native.Api.InputMouse(b, 2) != 0;
        public static Vector2 mousePosition { get { float* f = stackalloc float[2]; Native.Api.MousePosition(f); return new Vector2(f[0], f[1]); } }
        /// <summary>"Horizontal" or "Vertical" (WASD / arrows).</summary>
        public static float GetAxis(string name) { fixed (byte* p = Native.Utf8(name)) return Native.Api.InputAxis(p); }
    }

    public struct RaycastHit
    {
        public Vector3 point, normal;
        public float distance;
        public GameObject gameObject;
    }

    public static unsafe class Physics
    {
        public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hit, float maxDistance = 1e9f)
        {
            float* o = stackalloc float[3]; float* d = stackalloc float[3];
            float* pt = stackalloc float[3]; float* nm = stackalloc float[3];
            o[0] = origin.x; o[1] = origin.y; o[2] = origin.z; d[0] = direction.x; d[1] = direction.y; d[2] = direction.z;
            float dist = 0; ulong go = 0;
            hit = default;
            if (Native.Api.Raycast(o, d, maxDistance, pt, nm, &dist, &go) == 0) return false;
            hit.point = new Vector3(pt[0], pt[1], pt[2]); hit.normal = new Vector3(nm[0], nm[1], nm[2]);
            hit.distance = dist; hit.gameObject = GameObject.FromId(go);
            return true;
        }
    }

    /// <summary>
    /// An engine component. A script holds it as a handle (its object, its class and which one of that class), never a
    /// pointer, so a destroyed object or a removed component reads as null and using it throws a clear error.
    /// Every class the engine binds (Light, Rigidbody3D, AudioSource ...) is a generated subclass with typed properties
    /// and methods (Bindings.g.cs, made from the engine's binding table); Get / Set / Call reach any member by name.
    /// </summary>
    public unsafe class Component
    {
        internal ulong ownerId;
        internal uint classKey;
        internal uint index;
        string _type;
        public GameObject gameObject => GameObject.FromId(ownerId);
        public Transform transform => gameObject?.transform;
        /// <summary>The engine class name ("Light").</summary>
        public string typeName => _type ??= Native.ClassName(classKey);
        /// <summary>True while the object and the component still exist.</summary>
        public bool alive
        {
            get
            {
                var h = Handle(); var o = new OkayValue();
                byte* e = stackalloc byte[256];
                fixed (byte* n = Native.Utf8("enabled")) return Native.Api.GetMember(&h, n, &o, e, 256) == 1;
            }
        }
        public bool enabled { get => Get<bool>("enabled"); set => Set("enabled", value); }

        internal OkayValue Handle() => new OkayValue { kind = 7, id = ownerId, classKey = classKey, index = index };

        internal static Component FromHandle(ulong id, uint key, uint index)
        {
            if (id == 0 || key == 0) return null;
            Component c = ComponentTypes.Make(Native.ClassName(key));
            c.ownerId = id; c.classKey = key; c.index = index;
            return c;
        }

        static void Check(int rc, string what, byte* err)
        {
            if (rc == 1) return;
            if (rc == 0) throw new MissingMemberException(what + " was not found");
            throw new InvalidOperationException(Native.ReadUtf8(err));
        }

        /// <summary>A property (the Inspector's name, "intensity") as type T.</summary>
        public T Get<T>(string member)
        {
            var h = Handle(); var o = new OkayValue();
            byte* err = stackalloc byte[512]; err[0] = 0;
            int rc;
            fixed (byte* n = Native.Utf8(member)) rc = Native.Api.GetMember(&h, n, &o, err, 512);
            Check(rc, typeName + "." + member, err);
            return (T)Native.FromValue(o, typeof(T));
        }

        /// <summary>Set a property through the engine's checks (range, clamping, the change hook).</summary>
        public void Set<T>(string member, T value)
        {
            var pins = new List<System.Runtime.InteropServices.GCHandle>();
            try
            {
                var h = Handle(); var v = Native.ToValue(value, pins);
                byte* err = stackalloc byte[512]; err[0] = 0;
                int rc;
                fixed (byte* n = Native.Utf8(member)) rc = Native.Api.SetMember(&h, n, &v, err, 512);
                Check(rc, typeName + "." + member, err);
            }
            finally { foreach (var p in pins) p.Free(); }
        }

        object Invoke(string method, Type want, object[] args)
        {
            var pins = new List<System.Runtime.InteropServices.GCHandle>();
            try
            {
                int n = args?.Length ?? 0;
                OkayValue* a = stackalloc OkayValue[n > 0 ? n : 1];
                for (int i = 0; i < n; i++) a[i] = Native.ToValue(args[i], pins);
                var h = Handle(); var o = new OkayValue();
                byte* err = stackalloc byte[512]; err[0] = 0;
                int rc;
                fixed (byte* m = Native.Utf8(method)) rc = Native.Api.CallMethod(&h, m, a, n, &o, err, 512);
                Check(rc, typeName + "." + method + "()", err);
                return Native.FromValue(o, want);
            }
            finally { foreach (var p in pins) p.Free(); }
        }
        /// <summary>Call an engine method by name; the engine picks the overload that fits the arguments.</summary>
        public T Call<T>(string method, params object[] args) => (T)Invoke(method, typeof(T), args);
        public void CallVoid(string method, params object[] args) => Invoke(method, typeof(void), args);

        // Text access by Inspector path, kept from the first C# API ("Light.intensity" style).
        public string GetString(string property) => Native.GetString(ownerId, typeName + "." + property);
        public float GetFloat(string property) { float.TryParse(GetString(property), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out float f); return f; }
        public bool GetBool(string property) { string s = GetString(property); return s == "true" || s == "True" || s == "1"; }
        public bool SetString(string property, string value)
        {
            fixed (byte* p = Native.Utf8(typeName + "." + property)) fixed (byte* v = Native.Utf8(value)) return Native.Api.SetProperty(ownerId, p, v) != 0;
        }
        public bool SetFloat(string property, float value) => SetString(property, value.ToString("R", System.Globalization.CultureInfo.InvariantCulture));
        public bool SetBool(string property, bool value) => SetString(property, value ? "true" : "false");

        public override string ToString() => typeName + "(" + (gameObject?.name ?? "destroyed") + ")";
        public override bool Equals(object o) => o is Component c && c.ownerId == ownerId && c.classKey == classKey && c.index == index;
        public override int GetHashCode() => HashCode.Combine(ownerId, classKey, index);
    }

    /// <summary>Class name -> constructor for every generated component class (filled by Bindings.g.cs).</summary>
    static partial class ComponentTypes
    {
        static readonly Dictionary<string, Func<Component>> map = Build();
        static Dictionary<string, Func<Component>> Build() { var m = new Dictionary<string, Func<Component>>(); AddGenerated(m); return m; }
        static partial void AddGenerated(Dictionary<string, Func<Component>> m);
        internal static Component Make(string name) => map.TryGetValue(name, out var f) ? f() : new Component();
        /// <summary>The engine class name for a C# component type: its own name.</summary>
        internal static string NameOf(Type t) => t == typeof(Component) ? null : t.Name;
    }

    public sealed unsafe class Transform
    {
        public GameObject gameObject { get; }
        internal Transform(GameObject go) { gameObject = go; }
        Vector3 V(int what) { float* f = stackalloc float[4]; Native.Api.GetVec(gameObject.id, what, f); return new Vector3(f[0], f[1], f[2]); }
        void V(int what, Vector3 v) { float* f = stackalloc float[4]; f[0] = v.x; f[1] = v.y; f[2] = v.z; Native.Api.SetVec(gameObject.id, what, f); }
        public Vector3 position { get => V(0); set => V(0, value); }
        public Vector3 localPosition { get => V(1); set => V(1, value); }
        public Vector3 eulerAngles { get => V(2); set => V(2, value); }
        public Vector3 localEulerAngles { get => V(5); set => V(5, value); }
        public Vector3 lossyScale { get => V(3); set => V(3, value); }
        public Vector3 localScale { get => V(3); set => V(3, value); }
        public Quaternion rotation
        {
            get { float* f = stackalloc float[4]; Native.Api.GetVec(gameObject.id, 4, f); return new Quaternion(f[0], f[1], f[2], f[3]); }
            set { float* f = stackalloc float[4]; f[0] = value.x; f[1] = value.y; f[2] = value.z; f[3] = value.w; Native.Api.SetVec(gameObject.id, 4, f); }
        }
        public Vector3 forward => rotation * Vector3.forward;
        public Vector3 up => rotation * Vector3.up;
        public Vector3 right => rotation * Vector3.right;
        public void Translate(Vector3 delta) => localPosition = localPosition + delta;
        public void Rotate(Vector3 eulerDegrees) => rotation = (rotation * Quaternion.Euler(eulerDegrees)).normalized;
    }

    public sealed unsafe class GameObject
    {
        public readonly ulong id;
        Transform _t;
        GameObject(ulong id) { this.id = id; }
        internal static GameObject FromId(ulong id) => id == 0 ? null : new GameObject(id);
        public static GameObject Find(string name) { fixed (byte* p = Native.Utf8(name)) return FromId(Native.Api.Find(p)); }
        public static GameObject CreatePrimitiveEmpty(string name) { fixed (byte* p = Native.Utf8(name)) return FromId(Native.Api.Create(p)); }
        public bool alive => Native.Api.IsAlive(id) != 0;
        public string name { get { byte* b = stackalloc byte[256]; return Native.Api.GetName(id, b, 256) < 0 ? "" : Native.ReadUtf8(b); } }
        public bool activeSelf { get => Native.Api.GetActive(id) != 0; }
        public void SetActive(bool a) => Native.Api.SetActive(id, a ? 1 : 0);
        public Transform transform => _t ??= new Transform(this);
        public override bool Equals(object o) => o is GameObject g && g.id == id;
        public override int GetHashCode() => id.GetHashCode();
        public static bool operator ==(GameObject a, GameObject b) => ReferenceEquals(a, b) || (!(a is null) && !(b is null) && a.id == b.id);
        public static bool operator !=(GameObject a, GameObject b) => !(a == b);
        public override string ToString() => "GameObject(" + name + ")";

        /// <summary>An engine component by its class name ("Light", "Rigidbody"), or null if the object has none.</summary>
        public Component GetComponent(string typeName, int index = 0)
        {
            var o = new OkayValue();
            fixed (byte* p = Native.Utf8(typeName)) if (Native.Api.FindComponent(id, p, index, &o) != 1) return null;
            return Component.FromHandle(o.id, o.classKey, o.index);
        }
        /// <summary>An engine component (Light, AudioSource ...) or a C# script (a Behaviour class) of type T, or null.</summary>
        public T GetComponent<T>() where T : class
        {
            if (typeof(Component).IsAssignableFrom(typeof(T))) return GetComponent(ComponentTypes.NameOf(typeof(T)) ?? "") as T;
            return Interop.Host.FindScript<T>(id);
        }
        /// <summary>Add an engine component by class name; throws when the class cannot be added.</summary>
        public Component AddComponent(string typeName)
        {
            var o = new OkayValue();
            byte* err = stackalloc byte[512]; err[0] = 0;
            int rc;
            fixed (byte* p = Native.Utf8(typeName)) rc = Native.Api.AddComponent(id, p, &o, err, 512);
            if (rc == 0) throw new ArgumentException("there is no engine component called '" + typeName + "'");
            if (rc < 0) throw new InvalidOperationException(Native.ReadUtf8(err));
            return Component.FromHandle(o.id, o.classKey, o.index);
        }
        public T AddComponent<T>() where T : Component => AddComponent(ComponentTypes.NameOf(typeof(T)) ?? "") as T;
    }

    /// <summary>Destroy / Instantiate (also reachable as Destroy(...) and Instantiate(...) inside a script).</summary>
    public static unsafe class Objects
    {
        public static void Destroy(GameObject go, float delaySeconds = 0f) { if (go != null) Native.Api.Destroy(go.id, delaySeconds); }
        public static GameObject Instantiate(GameObject prefab) => GameObject.FromId(Native.Api.Instantiate(prefab.id, null, null));
        public static GameObject Instantiate(GameObject prefab, Vector3 position)
        {
            float* p = stackalloc float[3]; p[0] = position.x; p[1] = position.y; p[2] = position.z;
            return GameObject.FromId(Native.Api.Instantiate(prefab.id, p, null));
        }
        public static GameObject Instantiate(GameObject prefab, Vector3 position, Quaternion rotation)
        {
            float* p = stackalloc float[3]; float* r = stackalloc float[4];
            p[0] = position.x; p[1] = position.y; p[2] = position.z; r[0] = rotation.x; r[1] = rotation.y; r[2] = rotation.z; r[3] = rotation.w;
            return GameObject.FromId(Native.Api.Instantiate(prefab.id, p, r));
        }
    }

    /// <summary>
    /// Base class of every C# script. Just write the methods you need, with these exact names (any visibility, no override):
    /// Awake, OnEnable, Start, Update, FixedUpdate, LateUpdate, OnCollisionEnter/Stay/Exit(GameObject other),
    /// OnTriggerEnter/Stay/Exit(GameObject other), OnDisable, OnDestroy. Update may take no argument.
    /// Public fields (and [SerializeField] private ones) of type float, int, bool, string and Vector3 show in the Inspector.
    /// </summary>
    public abstract class Behaviour
    {
        internal ulong ownerId;
        public GameObject gameObject => GameObject.FromId(ownerId);
        public Transform transform => gameObject?.transform;
        public T GetComponent<T>() where T : class => gameObject?.GetComponent<T>();
        public Component GetComponent(string typeName) => gameObject?.GetComponent(typeName);
        public T AddComponent<T>() where T : Component => gameObject?.AddComponent<T>();

        public static void Destroy(GameObject go, float delaySeconds = 0f) => Objects.Destroy(go, delaySeconds);
        public static GameObject Instantiate(GameObject prefab) => Objects.Instantiate(prefab);
        public static GameObject Instantiate(GameObject prefab, Vector3 position) => Objects.Instantiate(prefab, position);
        public static GameObject Instantiate(GameObject prefab, Vector3 position, Quaternion rotation) => Objects.Instantiate(prefab, position, rotation);
        public static GameObject Find(string name) => GameObject.Find(name);
    }
}
