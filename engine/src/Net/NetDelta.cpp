#include "okay/Net/NetDelta.hpp"
#include <cmath>
#include <cstring>

namespace okay::net {

namespace {

constexpr std::size_t kMaxId = 64;
constexpr std::size_t kMaxExtra = 512;
constexpr std::size_t kMaxObjects = 256;

std::int32_t QuantMm(float m) {
    if (!std::isfinite(m)) return 0;
    const double mm = static_cast<double>(m) * 1000.0;
    if (mm >= 2147483647.0) return 2147483647;
    if (mm <= -2147483647.0) return -2147483647;
    return static_cast<std::int32_t>(std::llround(mm));
}

std::int16_t QuantUnit(float c) {
    if (!std::isfinite(c)) return 0;
    float x = c * 32767.0f;
    if (x > 32767.0f) x = 32767.0f;
    if (x < -32767.0f) x = -32767.0f;
    return static_cast<std::int16_t>(std::lround(x));
}

bool FitI16(std::int32_t v) { return v >= -32768 && v <= 32767; }

struct W {
    std::vector<std::uint8_t> b;
    void u8(std::uint8_t v) { b.push_back(v); }
    void u16(std::uint16_t v) { u8(static_cast<std::uint8_t>(v)); u8(static_cast<std::uint8_t>(v >> 8)); }
    void u32(std::uint32_t v) { u16(static_cast<std::uint16_t>(v)); u16(static_cast<std::uint16_t>(v >> 16)); }
    void i16(std::int16_t v) { u16(static_cast<std::uint16_t>(v)); }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    void f64(double v) {
        std::uint64_t u = 0;
        static_assert(sizeof(double) == 8, "double is 8 bytes");
        std::memcpy(&u, &v, 8);
        u32(static_cast<std::uint32_t>(u));
        u32(static_cast<std::uint32_t>(u >> 32));
    }
    void raw(const void* p, std::size_t n) {
        auto* c = static_cast<const std::uint8_t*>(p);
        b.insert(b.end(), c, c + n);
    }
};

struct R {
    const std::uint8_t* p;
    const std::uint8_t* e;
    bool ok = true;
    void need(std::size_t n) { if (ok && static_cast<std::size_t>(e - p) < n) ok = false; }
    std::uint8_t u8() { need(1); if (!ok) return 0; return *p++; }
    std::uint16_t u16() { const std::uint16_t lo = u8(); return static_cast<std::uint16_t>(lo | (static_cast<std::uint16_t>(u8()) << 8)); }
    std::uint32_t u32() { const std::uint32_t lo = u16(); return lo | (static_cast<std::uint32_t>(u16()) << 16); }
    std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    double f64() {
        const std::uint64_t lo = u32();
        const std::uint64_t u = lo | (static_cast<std::uint64_t>(u32()) << 32);
        double v = 0;
        std::memcpy(&v, &u, 8);
        return v;
    }
    void raw(void* d, std::size_t n) {
        need(n);
        if (!ok) return;
        std::memcpy(d, p, n);
        p += n;
    }
};

} // namespace

QPose QuantizePose(const Vec3& p, const Quat& q) {
    QPose o;
    o.x = QuantMm(p.x); o.y = QuantMm(p.y); o.z = QuantMm(p.z);
    o.qx = QuantUnit(q.x); o.qy = QuantUnit(q.y); o.qz = QuantUnit(q.z); o.qw = QuantUnit(q.w);
    return o;
}

void DequantizePose(const QPose& q, Vec3& p, Quat& rot) {
    p = Vec3(static_cast<float>(q.x) / 1000.0f, static_cast<float>(q.y) / 1000.0f, static_cast<float>(q.z) / 1000.0f);
    rot = Quat(static_cast<float>(q.qx) / 32767.0f, static_cast<float>(q.qy) / 32767.0f,
               static_cast<float>(q.qz) / 32767.0f, static_cast<float>(q.qw) / 32767.0f);
    if (rot.x * rot.x + rot.y * rot.y + rot.z * rot.z + rot.w * rot.w < 1e-8f) rot = Quat(0, 0, 0, 1);
    else rot.Normalize();
}

bool SameBase(const DeltaBase& a, const DeltaBase& b) {
    return a.pose == b.pose && a.extra == b.extra;
}

std::vector<std::uint8_t> EncodeSnapshot(const DeltaBase& base, const DeltaBase& current,
                                         std::uint32_t snapId, double tick) {
    struct Rec { std::string id; QPose pose; std::string extra; bool key = false; bool hasExtra = false; std::int16_t dx = 0, dy = 0, dz = 0; };
    std::vector<Rec> recs;
    recs.reserve(current.pose.size());
    for (const auto& [id, pose] : current.pose) {
        if (id.empty() || id.size() > kMaxId) continue;
        const auto ex = current.extra.find(id);
        const std::string extra = ex == current.extra.end() ? std::string() : ex->second;
        const auto bp = base.pose.find(id);
        const auto be = base.extra.find(id);
        const std::string oldExtra = be == base.extra.end() ? std::string() : be->second;
        if (bp != base.pose.end() && bp->second == pose && oldExtra == extra) continue;
        Rec r;
        r.id = id;
        r.pose = pose;
        r.hasExtra = extra != oldExtra;
        if (r.hasExtra) r.extra = extra.size() > kMaxExtra ? extra.substr(0, kMaxExtra) : extra;
        if (bp != base.pose.end()) {
            const std::int32_t dx = pose.x - bp->second.x, dy = pose.y - bp->second.y, dz = pose.z - bp->second.z;
            if (FitI16(dx) && FitI16(dy) && FitI16(dz)) {
                r.dx = static_cast<std::int16_t>(dx);
                r.dy = static_cast<std::int16_t>(dy);
                r.dz = static_cast<std::int16_t>(dz);
            } else r.key = true;
        } else r.key = true;
        recs.push_back(std::move(r));
        if (recs.size() >= kMaxObjects) break;
    }
    if (recs.empty()) return {};
    W w;
    w.u8(kSnapDelta);
    w.u32(base.id);
    w.u32(snapId);
    w.f64(std::isfinite(tick) ? tick : 0.0);
    w.u16(static_cast<std::uint16_t>(recs.size()));
    for (const Rec& r : recs) {
        w.u8(static_cast<std::uint8_t>(r.id.size()));
        w.raw(r.id.data(), r.id.size());
        w.u8(static_cast<std::uint8_t>((r.key ? 1 : 0) | (r.hasExtra ? 2 : 0)));
        if (r.key) { w.i32(r.pose.x); w.i32(r.pose.y); w.i32(r.pose.z); }
        else { w.i16(r.dx); w.i16(r.dy); w.i16(r.dz); }
        w.i16(r.pose.qx); w.i16(r.pose.qy); w.i16(r.pose.qz); w.i16(r.pose.qw);
        if (r.hasExtra) {
            w.u16(static_cast<std::uint16_t>(r.extra.size()));
            w.raw(r.extra.data(), r.extra.size());
        }
    }
    return std::move(w.b);
}

bool PeekSnapshot(const std::uint8_t* data, std::size_t size, std::uint32_t& baselineId, std::uint32_t& snapId) {
    if (!data || size < 1 + 4 + 4) return false;
    R r{data, data + size, true};
    if (r.u8() != kSnapDelta) return false;
    baselineId = r.u32();
    snapId = r.u32();
    return r.ok && snapId != 0;
}

bool DecodeSnapshot(const std::uint8_t* data, std::size_t size, const DeltaBase& have,
                    std::uint32_t& baselineId, std::uint32_t& snapId, double& tick,
                    std::vector<SnapChange>& changes) {
    changes.clear();
    if (!data || size < 1 + 4 + 4 + 8 + 2) return false;
    R r{data, data + size, true};
    if (r.u8() != kSnapDelta) return false;
    baselineId = r.u32();
    snapId = r.u32();
    tick = r.f64();
    const std::uint16_t n = r.u16();
    if (!r.ok || n > kMaxObjects || snapId == 0) return false;
    if (baselineId != have.id) return false;
    changes.reserve(n);
    for (std::uint16_t i = 0; i < n; ++i) {
        const std::uint8_t idLen = r.u8();
        if (!r.ok || idLen == 0 || idLen > kMaxId) return false;
        std::string id(idLen, '\0');
        r.raw(id.data(), idLen);
        const std::uint8_t flags = r.u8();
        SnapChange c;
        c.id = std::move(id);
        c.hasExtra = (flags & 2) != 0;
        if (flags & 1) {
            c.pose.x = r.i32(); c.pose.y = r.i32(); c.pose.z = r.i32();
        } else {
            const auto bp = have.pose.find(c.id);
            if (bp == have.pose.end()) return false;
            c.pose.x = bp->second.x + r.i16();
            c.pose.y = bp->second.y + r.i16();
            c.pose.z = bp->second.z + r.i16();
        }
        c.pose.qx = r.i16(); c.pose.qy = r.i16(); c.pose.qz = r.i16(); c.pose.qw = r.i16();
        if (c.hasExtra) {
            const std::uint16_t el = r.u16();
            if (!r.ok || el > kMaxExtra) return false;
            c.extra.assign(el, '\0');
            if (el) r.raw(c.extra.data(), el);
        } else {
            const auto be = have.extra.find(c.id);
            if (be != have.extra.end()) c.extra = be->second;
        }
        if (!r.ok) return false;
        changes.push_back(std::move(c));
    }
    return r.ok;
}

std::vector<std::uint8_t> EncodeSnapAck(std::uint32_t snapId) {
    W w;
    w.u8(kSnapAck);
    w.u32(snapId);
    return std::move(w.b);
}

bool DecodeSnapAck(const std::uint8_t* data, std::size_t size, std::uint32_t& snapId) {
    if (!data || size < 5) return false;
    R r{data, data + size, true};
    if (r.u8() != kSnapAck) return false;
    snapId = r.u32();
    return r.ok && snapId != 0;
}

} // namespace okay::net
