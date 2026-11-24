// okay::lua table library.
//
// Written from the Lua 5.4 Reference Manual, section 6.6 (table
// manipulation): insert, remove, concat, unpack, pack, sort and move. Element
// access goes through __index / __newindex / __len as the manual describes.
// table.sort is our own quicksort (median of three, insertion sort for short
// runs) over a copy of the elements; a comparison function that is not a
// strict order is detected and reported instead of reading out of bounds.
#include "LuaCode.hpp"
#include <utility>
#include <vector>

namespace okay::lua {

namespace {

Value GetI(State& L, const Value& t, std::int64_t i) {
    if (t.IsTable() && !t.Tab()->meta) return t.Tab()->GetInt(i);
    return L.Index(t, Value::Int(i));
}
void SetI(State& L, const Value& t, std::int64_t i, const Value& v) {
    if (t.IsTable() && !t.Tab()->meta) { t.Tab()->SetInt(L, i, v); return; }
    L.SetIndex(t, Value::Int(i), v);
}
std::int64_t LenOf(State& L, const Value& t) {
    if (t.IsTable() && !t.Tab()->meta) return t.Tab()->Length();
    Value n = L.Length(t);
    std::int64_t r;
    if (!State::ToInteger(n, r)) L.Error("object length is not an integer");
    return r;
}
// a table, or anything with the metamethods the operation needs
Value CheckTab(State& L, int arg, const char* fname) {
    Value v = L.Arg(arg);
    if (v.IsTable()) return v;
    Table* mt = L.GetMetatable(v);
    if (mt && (!mt->GetStr(L.tmName[TM_INDEX]).IsNil() || !mt->GetStr(L.tmName[TM_LEN]).IsNil())) return v;
    L.TypeError(arg, fname, "table");
}

int T_insert(State& L, int n) {
    Value t = CheckTab(L, 0, "insert");
    std::int64_t e = LenOf(L, t) + 1;
    std::int64_t pos;
    if (n == 2) {
        pos = e;
    } else if (n == 3) {
        pos = L.CheckInt(1, "insert");
        if ((std::uint64_t)pos - 1u >= (std::uint64_t)e) L.ArgError(1, "insert", "position out of bounds");
        L.Charge((std::uint64_t)(e - pos) / 8);
        for (std::int64_t i = e; i > pos; --i) SetI(L, t, i, GetI(L, t, i - 1));
    } else {
        L.Error("wrong number of arguments to 'insert'");
    }
    SetI(L, t, pos, L.Arg(n - 1));
    return 0;
}

int T_remove(State& L, int n) {
    Value t = CheckTab(L, 0, "remove");
    std::int64_t size = LenOf(L, t);
    std::int64_t pos = size;
    if (n >= 2) {
        pos = L.CheckInt(1, "remove");
        if (pos != size && (std::uint64_t)pos - 1u > (std::uint64_t)size) L.ArgError(1, "remove", "position out of bounds");
    }
    L.Push(GetI(L, t, pos));
    L.Charge((std::uint64_t)(size > pos ? size - pos : 0) / 8);
    for (; pos < size; ++pos) SetI(L, t, pos, GetI(L, t, pos + 1));
    SetI(L, t, pos, Value());
    return 1;
}

int T_concat(State& L, int n) {
    Value t = CheckTab(L, 0, "concat");
    std::string sep = n >= 2 && !L.Arg(1).IsNil() ? L.CheckStr(1, "concat")->s : std::string();
    std::int64_t i = n >= 3 ? L.OptInt(2, "concat", 1) : 1;
    std::int64_t j = n >= 4 && !L.Arg(3).IsNil() ? L.CheckInt(3, "concat") : LenOf(L, t);
    std::string out;
    for (std::int64_t k = i; k <= j; ++k) {
        Value v = GetI(L, t, k);
        if (v.IsString()) out += v.Str()->s;
        else if (v.IsNumber()) out += State::NumberToString(v);
        else L.Error(std::string("invalid value (at index ") + std::to_string(k) + ") in table for 'concat'");
        if (k < j) out += sep;
        if (out.size() > (std::size_t(1) << 30)) L.Error("resulting string too large");
        if (k == INT64_MAX) break;
    }
    L.Charge(out.size() / 64);
    L.PushString(out);
    return 1;
}

int T_unpack(State& L, int n) {
    Value t = L.Arg(0);
    std::int64_t i = n >= 2 ? L.OptInt(1, "unpack", 1) : 1;
    std::int64_t j = n >= 3 && !L.Arg(2).IsNil() ? L.CheckInt(2, "unpack") : LenOf(L, t);
    if (i > j) return 0;
    std::uint64_t cnt = (std::uint64_t)j - (std::uint64_t)i;
    if (cnt >= 1000000) L.Error("too many results to unpack");
    L.CheckStack((int)cnt + 1);
    for (std::int64_t k = i;; ++k) {
        L.Push(GetI(L, t, k));
        if (k == j) break;
    }
    return (int)cnt + 1;
}

int T_pack(State& L, int n) {
    Table* t = L.NewTable((std::size_t)n, 1);
    L.Push(Value::Obj(t));
    for (int i = 0; i < n; ++i) t->SetInt(L, i + 1, L.Arg(i));
    t->Set(L, Value::Obj(L.NewString("n")), Value::Int(n));
    return 1;
}

int T_move(State& L, int n) {
    Value a1 = CheckTab(L, 0, "move");
    std::int64_t f = L.CheckInt(1, "move");
    std::int64_t e = L.CheckInt(2, "move");
    std::int64_t t = L.CheckInt(3, "move");
    Value a2 = n >= 5 && !L.Arg(4).IsNil() ? CheckTab(L, 4, "move") : a1;
    if (e >= f) {
        if (!(f > 0 || e < INT64_MAX + f)) L.ArgError(2, "move", "too many elements to move");
        if (t > INT64_MAX - (e - f)) L.ArgError(3, "move", "destination wrap around");
        L.Charge((std::uint64_t)(e - f) / 8);
        bool overlap = t > f && t <= e && (a2.t == a1.t && a2.o == a1.o);
        if (!overlap) {
            for (std::int64_t i = 0; i <= e - f; ++i) SetI(L, a2, t + i, GetI(L, a1, f + i));
        } else {
            for (std::int64_t i = e - f; i >= 0; --i) SetI(L, a2, t + i, GetI(L, a1, f + i));
        }
    }
    L.Push(a2);
    return 1;
}

// ---------------------------------------------------------------- sort
struct Sorter {
    State& L;
    std::vector<Value>& v;
    Value cmp;
    bool Less(const Value& a, const Value& b) {
        L.Charge(1);
        if (cmp.IsNil()) return L.LessThan(a, b);
        int func = L.Current()->top;
        L.CheckStack(3);
        L.Push(cmp);
        L.Push(a);
        L.Push(b);
        L.CallAt(func, 1);
        return L.Pop().Truthy();
    }
    [[noreturn]] void Bad() { L.Error("invalid order function for sorting"); }
    void Insertion(std::size_t lo, std::size_t hi) {   // [lo, hi]
        for (std::size_t i = lo + 1; i <= hi; ++i) {
            std::size_t j = i;
            while (j > lo && Less(v[j], v[j - 1])) { std::swap(v[j], v[j - 1]); --j; }
        }
    }
    void Sort(std::size_t lo, std::size_t hi, int depth) {
        while (hi > lo) {
            if (hi - lo < 12) { Insertion(lo, hi); return; }
            std::size_t mid = lo + (hi - lo) / 2;
            if (Less(v[mid], v[lo])) std::swap(v[mid], v[lo]);
            if (Less(v[hi], v[mid])) {
                std::swap(v[hi], v[mid]);
                if (Less(v[mid], v[lo])) std::swap(v[mid], v[lo]);
            }
            // the pivot goes to hi - 1; partition (lo, hi - 1)
            std::swap(v[mid], v[hi - 1]);
            std::size_t pv = hi - 1;
            std::size_t i = lo, j = hi - 1;
            for (;;) {
                while (Less(v[++i], v[pv])) if (i >= hi) Bad();
                while (Less(v[pv], v[--j])) if (j <= lo) Bad();
                if (j < i) break;
                std::swap(v[i], v[j]);
            }
            std::swap(v[i], v[hi - 1]);
            // recurse into the smaller side, loop on the bigger one
            if (i - lo < hi - i) {
                if (i > lo) Sort(lo, i - 1, depth + 1);
                lo = i + 1;
            } else {
                Sort(i + 1, hi, depth + 1);
                if (i == 0) return;
                hi = i - 1;
            }
        }
    }
};

int T_sort(State& L, int n) {
    Value t = CheckTab(L, 0, "sort");
    std::int64_t len = LenOf(L, t);
    if (len > 1) {
        if (len >= INT32_MAX) L.ArgError(0, "sort", "array too big");
        Value cmp = n >= 2 ? L.Arg(1) : Value();
        if (!cmp.IsNil() && !cmp.IsFunction()) L.TypeError(1, "sort", "function");
        std::vector<Value> items((std::size_t)len);
        for (std::int64_t i = 0; i < len; ++i) items[(std::size_t)i] = GetI(L, t, i + 1);
        L.tempRoots.push_back(&items);
        struct Pop { State& L; ~Pop() { L.tempRoots.pop_back(); } } pop{L};
        Sorter s{L, items, cmp};
        s.Sort(0, items.size() - 1, 0);
        for (std::int64_t i = 0; i < len; ++i) SetI(L, t, i + 1, items[(std::size_t)i]);
    }
    return 0;
}

} // namespace

void OpenTable(State& L) {
    Table* t = L.NewTable();
    L.SetGlobal("table", Value::Obj(t));
    L.Register(t, "insert", T_insert);
    L.Register(t, "remove", T_remove);
    L.Register(t, "concat", T_concat);
    L.Register(t, "unpack", T_unpack);
    L.Register(t, "pack", T_pack);
    L.Register(t, "move", T_move);
    L.Register(t, "sort", T_sort);
}

} // namespace okay::lua
