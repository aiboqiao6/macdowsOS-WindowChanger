#include "adapter-api.h"
#include "pe-identity.h"
#include "module-capabilities.h"
#include <commctrl.h>
#include <array>
#include <cstring>
#include <fstream>
#include <string>
#include <iterator>

namespace {
struct Context {};
struct String {};
struct Script {};
struct Value {};
template <class T> struct Local {
    T *slot;
    Local() : slot(nullptr) {}
};
struct Isolate {};
template <class T> T Symbol(HMODULE m, const char *name) {
    T r{};
    auto p = GetProcAddress(m, name);
    static_assert(sizeof(T) == sizeof(p));
    memcpy(&r, &p, sizeof(r));
    return r;
}
HMODULE Runtime() {
    return styler::FindElectronRuntime();
}
bool KnownRuntime(HMODULE module) {
    if (!module)
        return false;
    auto version = Symbol<const char *(*)()>(module, "?GetVersion@V8@v8@@SAPEBDXZ");
    if (!version)
        return false;
    const int major = atoi(version());
    return major == 13 || major == 15 || strcmp(version(), "14.8.178.28-electron.0") == 0 ||
           strcmp(version(), "14.2.231.21-electron.0") == 0 || strcmp(version(), "14.2.231.26-electron.0") == 0;
}
std::wstring File() {
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&File), &self);
    wchar_t file[32768]{};
    GetModuleFileNameW(self, file, 32768);
    return file;
}
std::wstring Directory() {
    auto p = File();
    return p.substr(0, p.find_last_of(L'\\') + 1);
}
std::string Quote(std::wstring const &s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), utf8.data(), n, nullptr, nullptr);
    std::string r = "\"";
    for (char ch : utf8) {
        if (ch == '\\' || ch == '\"')
            r += '\\';
        r += ch;
    }
    return r + '\"';
}

// These exported V8 entry points retain their x64 Local/MaybeLocal ABI in the
// tested Electron runtimes. No engine object offsets or code patches.
std::string Evaluate(std::string const &code) {
    auto m = Runtime();
    if (!KnownRuntime(m))
        return "ERR:unsupported runtime revision";
    auto current = Symbol<Isolate *(*)()>(m, "?TryGetCurrent@Isolate@v8@@SAPEAV12@XZ");
    auto context =
        Symbol<Local<Context> (Isolate::*)()>(m, "?GetCurrentContext@Isolate@v8@@QEAA?AV?$Local@VContext@v8@@@2@XZ");
    auto enter = Symbol<void (*)(void *, Isolate *)>(m, "??0HandleScope@v8@@QEAA@PEAVIsolate@1@@Z");
    auto leave = Symbol<void (*)(void *)>(m, "??1HandleScope@v8@@QEAA@XZ");
    auto string = Symbol<Local<String> (*)(Isolate *, const char *, int, int)>(
        m, "?NewFromUtf8@String@v8@@SA?AV?$MaybeLocal@VString@v8@@@2@PEAVIsolate@2@PEBDW4NewStringType@2@H@Z");
    auto compile = Symbol<Local<Script> (*)(Local<Context>, Local<String>, void *)>(
        m, "?Compile@Script@v8@@SA?AV?$MaybeLocal@VScript@v8@@@2@V?$Local@VContext@v8@@@2@V?$Local@VString@v8@@@2@"
           "PEAVScriptOrigin@2@@Z");
    auto run = Symbol<Local<Value> (Script::*)(Local<Context>)>(
        m, "?Run@Script@v8@@QEAA?AV?$MaybeLocal@VValue@v8@@@2@V?$Local@VContext@v8@@@2@@Z");
    auto utf8 = Symbol<void (*)(void *, Isolate *, Local<Value>, int)>(
        m, "??0Utf8Value@String@v8@@QEAA@PEAVIsolate@2@V?$Local@VValue@v8@@@2@W4WriteOptions@12@@Z");
    if (!utf8)
        utf8 = Symbol<void (*)(void *, Isolate *, Local<Value>, int)>(
            m, "??0Utf8Value@String@v8@@QEAA@PEAVIsolate@2@V?$Local@VValue@v8@@@2@@Z");
    auto chars = Symbol<const char *(*)(void *)>(m, "??DUtf8Value@String@v8@@QEBAPEBDXZ");
    auto freeUtf8 = Symbol<void (*)(void *)>(m, "??1Utf8Value@String@v8@@QEAA@XZ");
    if (!current || !context || !enter || !leave || !string || !compile || !run || !utf8 || !chars || !freeUtf8)
        return "ERR:exports";
    auto isolate = current();
    if (!isolate)
        return "ERR:isolate";
    alignas(16) std::array<BYTE, 64> scope{};
    enter(scope.data(), isolate);
    std::string output = "ERR:context";
    auto main = (isolate->*context)();
    if (main.slot) {
        auto source = string(isolate, code.c_str(), 0, -1);
        auto script = compile(main, source, nullptr);
        if (script.slot) {
            auto result = (script.slot->*run)(main);
            if (result.slot) {
                alignas(16) std::array<BYTE, 64> bytes{};
                utf8(bytes.data(), isolate, result, 0);
                if (auto value = chars(bytes.data()))
                    output = value;
                freeUtf8(bytes.data());
            }
        }
    }
    leave(scope.data());
    return output;
}
std::string Run(const char *action, HWND w) {
    std::ifstream input(Directory() + STYLER_ELECTRON_SCRIPT);
    if (!input)
        return "ERR:script missing";
    std::string source(std::istreambuf_iterator<char>(input), {});
    return Evaluate("(" + source + ")('" + action + "'," + std::to_string(reinterpret_cast<UINT_PTR>(w)) + "," +
                    Quote(File()) + ")");
}
constexpr UINT_PTR SubclassId = 0x4d43454c;
struct State {
    HWND window{}, owner{};
    HANDLE process{};
    UINT_PTR timer{};
    bool ready{}, failed{}, restoring{}, restored{}, overlay{}, transitioning{};
    unsigned commands{};
    std::array<int, 6> metrics{};
};
LRESULT CALLBACK Subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
State *Live(HWND w) {
    DWORD_PTR p{};
    return GetWindowSubclass(w, Subclass, SubclassId, &p) ? reinterpret_cast<State *>(p) : nullptr;
}
void Release(State *s) {
    KillTimer(s->window, s->timer);
    RemoveWindowSubclass(s->window, Subclass, SubclassId);
    RemovePropW(s->window, styler::InlineProperty);
    RemovePropW(s->window, styler::OwnerProperty);
    RemovePropW(s->window, styler::AdapterProperty);
    if (s->process)
        CloseHandle(s->process);
    delete s;
}
void Restore(State *s) {
    if (!s->restoring) {
        s->restoring = true;
        auto result = Run("detach", s->window);
        if (result != "scheduled")
            s->restoring = false;
    }
}
LRESULT CALLBACK Subclass(HWND window, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR ref) {
    auto *s = reinterpret_cast<State *>(ref);
    if (m == styler::QueryMessage())
        return s->ready && !s->failed ? styler::Acknowledged : 0;
    if (m == styler::DetachMessage()) {
        if (reinterpret_cast<HWND>(w) != s->owner)
            return 0;
        if (s->commands)
            return styler::TransitionBusy;
        Restore(s);
        if (!s->restored)
            return styler::TransitionBusy;
        Release(s);
        return styler::Acknowledged;
    }
    if (m == styler::DiagnosticMessage()) {
        int i = int(w) - int(styler::Metric::InlineLeft);
        if (i >= 0 && i < 6)
            return s->metrics[i];
        if (static_cast<styler::Metric>(w) == styler::Metric::InlineKind)
            return 5;
        return 0;
    }
    if (m == WM_TIMER && w == s->timer) {
        if (s->failed || !IsWindow(s->owner) || WaitForSingleObject(s->process, 0) != WAIT_TIMEOUT) {
            Restore(s);
            if (s->restored)
                Release(s);
        }
        return 0;
    }
    if (m == WM_NCDESTROY) {
        Release(s);
        return DefSubclassProc(window, m, w, l);
    }
    if (m == WM_NCHITTEST && s->overlay && (s->ready || s->transitioning) && !s->restoring) {
        auto hit = DefSubclassProc(window, m, w, l);
        return hit == HTMINBUTTON || hit == HTMAXBUTTON || hit == HTCLOSE ? HTCAPTION : hit;
    }
    if (s->overlay && (s->ready || s->transitioning) && !s->restoring) {
        if ((m == WM_NCLBUTTONDOWN || m == WM_NCLBUTTONUP || m == WM_NCLBUTTONDBLCLK) &&
            (w == HTMINBUTTON || w == HTMAXBUTTON || w == HTCLOSE))
            return 0;
        if (m == WM_GETTITLEBARINFOEX && l) {
            auto result = DefSubclassProc(window, m, w, l);
            if (Live(window) == s) {
                auto info = reinterpret_cast<TITLEBARINFOEX *>(l);
                if (info->cbSize == sizeof(*info))
                    for (int i = 2; i <= 5; ++i) {
                        info->rgstate[i] |= STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE | STATE_SYSTEM_OFFSCREEN;
                        SetRectEmpty(&info->rgrect[i]);
                    }
            }
            return result;
        }
    }
    if (m == WM_SYSCOMMAND) {
        ++s->commands;
        auto result = DefSubclassProc(window, m, w, l);
        if (auto live = Live(window))
            --live->commands;
        return result;
    }
    return DefSubclassProc(window, m, w, l);
}
// Node-API is used only inside its own main-thread callbacks. Signal carries
// scalar layout/restore acknowledgements, never pointers from the renderer.
using Env = void *;
using ValueN = void *;
using Callback = ValueN(__cdecl *)(Env, void *);
ValueN __cdecl Signal(Env env, void *info) {
    auto m = Runtime();
    auto args = Symbol<int (*)(Env, void *, size_t *, ValueN *, ValueN *, void **)>(m, "napi_get_cb_info");
    auto number = Symbol<int (*)(Env, ValueN, int64_t *)>(m, "napi_get_value_int64");
    auto undef = Symbol<int (*)(Env, ValueN *)>(m, "napi_get_undefined");
    std::array<ValueN, 8> values{};
    size_t count = values.size();
    ValueN result{};
    if (!args || !number || !undef)
        return nullptr;
    if (args(env, info, &count, values.data(), nullptr, nullptr) == 0 && count >= 2) {
        std::array<int64_t, 8> numbers{};
        bool ok = true;
        for (size_t i = 0; i < count; i++)
            if (number(env, values[i], &numbers[i]) != 0)
                ok = false;
        if (ok)
            if (auto s = Live(reinterpret_cast<HWND>(UINT_PTR(numbers[1])))) {
                if (numbers[0] == 1 && count == 8 && !s->restoring) {
                    for (int i = 0; i < 6; i++)
                        s->metrics[i] = int(numbers[i + 2]);
                    s->ready = true;
                    s->transitioning = false;
                }
                if (numbers[0] == 2)
                    s->restored = true;
                if (numbers[0] == 3) {
                    s->failed = true;
                    s->restoring = false;
                }
                if (numbers[0] == 4 && !s->restoring) {
                    s->ready = false;
                    s->transitioning = true;
                }
            }
    }
    undef(env, &result);
    return result;
}
} // namespace
extern "C" __declspec(dllexport) void *__cdecl napi_register_module_v1(void *env, void *exports) {
    auto m = Runtime();
    auto create = Symbol<int (*)(Env, const char *, size_t, Callback, void *, ValueN *)>(m, "napi_create_function");
    auto set = Symbol<int (*)(Env, ValueN, const char *, ValueN)>(m, "napi_set_named_property");
    ValueN function{};
    if (create && set && create(env, "signal", 6, Signal, nullptr, &function) == 0)
        set(env, exports, "signal", function);
    return exports;
}
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterProbeV1(const styler::AdapterContextV1 *c) {
    if (!styler::ValidContext(c))
        return ERROR_INVALID_PARAMETER;
    return Run("probe", c->window) == "supported" ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
}
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *c) {
    if (!styler::ValidContext(c))
        return ERROR_INVALID_PARAMETER;
    if (Live(c->window))
        return ERROR_ALREADY_EXISTS;
    auto *s = new State;
    s->window = c->window;
    s->owner = c->owner;
    s->overlay = true;
    DWORD pid{};
    GetWindowThreadProcessId(s->owner, &pid);
    s->process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!s->process || !SetWindowSubclass(s->window, Subclass, SubclassId, reinterpret_cast<DWORD_PTR>(s))) {
        Release(s);
        return ERROR_NOT_SUPPORTED;
    }
    s->timer = SetTimer(s->window, reinterpret_cast<UINT_PTR>(s), 750, nullptr);
    if (!s->timer || !SetPropW(s->window, styler::OwnerProperty, s->owner)) {
        Release(s);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    SetPropW(s->window, styler::AdapterProperty, reinterpret_cast<HANDLE>(c->adapterId));
    SetPropW(s->window, styler::InlineProperty, reinterpret_cast<HANDLE>(UINT_PTR(5)));
    auto result = Run("attach", s->window);
    if (result != "scheduled") {
        Release(s);
        return ERROR_NOT_SUPPORTED;
    }
    return ERROR_SUCCESS;
}
