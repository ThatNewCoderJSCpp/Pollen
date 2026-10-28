#include "media_windows.hpp"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <inspectable.h>

#include <chrono>
#include <thread>

namespace pollen {

namespace {

typedef struct HSTRING__* WinString;

struct Api {
    HMODULE combase = nullptr;
    HRESULT(WINAPI* ro_initialize)(int) = nullptr;
    HRESULT(WINAPI* ro_get_activation_factory)(WinString, REFIID, void**) = nullptr;
    HRESULT(WINAPI* create_string)(const wchar_t*, UINT32, WinString*) = nullptr;
    HRESULT(WINAPI* delete_string)(WinString) = nullptr;
    const wchar_t*(WINAPI* string_buffer)(WinString, UINT32*) = nullptr;
    bool ok = false;

    Api() {
        combase = LoadLibraryW(L"combase.dll");
        if (!combase) return;
        ro_initialize = reinterpret_cast<decltype(ro_initialize)>(reinterpret_cast<void*>(GetProcAddress(combase, "RoInitialize")));
        ro_get_activation_factory = reinterpret_cast<decltype(ro_get_activation_factory)>(reinterpret_cast<void*>(GetProcAddress(combase, "RoGetActivationFactory")));
        create_string = reinterpret_cast<decltype(create_string)>(reinterpret_cast<void*>(GetProcAddress(combase, "WindowsCreateString")));
        delete_string = reinterpret_cast<decltype(delete_string)>(reinterpret_cast<void*>(GetProcAddress(combase, "WindowsDeleteString")));
        string_buffer = reinterpret_cast<decltype(string_buffer)>(reinterpret_cast<void*>(GetProcAddress(combase, "WindowsGetStringRawBuffer")));
        ok = ro_initialize && ro_get_activation_factory && create_string && delete_string && string_buffer;
    }
};

Api& api() {
    static Api a;
    return a;
}

const IID iid_async_info = { 0x00000036, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
const IID iid_manager_statics = { 0x2050c4ee, 0x11a0, 0x57de, { 0xae, 0xd7, 0xc9, 0x7c, 0x70, 0x33, 0x82, 0x45 } };

struct IAsyncInfoRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Id(UINT32* id) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(int* status) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* code) = 0;
    virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};

struct IAsyncOperationRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(void* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(void** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(void** result) = 0;
};

struct IManagerStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE RequestAsync(IAsyncOperationRaw** operation) = 0;
};

struct ISessionRaw;

struct IManager : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSession(ISessionRaw** session) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSessions(void** sessions) = 0;
};

struct ITimelineRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_StartTime(INT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_EndTime(INT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MinSeekTime(INT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MaxSeekTime(INT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Position(INT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_LastUpdatedTime(INT64* value) = 0;
};

struct IPlaybackInfoRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Controls(void** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackStatus(int* value) = 0;
};

struct IMediaPropertiesRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Title(WinString* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Subtitle(WinString* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumArtist(WinString* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Artist(WinString* value) = 0;
};

struct ISessionRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_SourceAppUserModelId(WinString* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryGetMediaPropertiesAsync(IAsyncOperationRaw** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTimelineProperties(ITimelineRaw** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPlaybackInfo(IPlaybackInfoRaw** value) = 0;
};

template <typename T>
struct Ref {
    T* p = nullptr;

    Ref() = default;
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    ~Ref() { reset(); }

    void reset() {
        if (p) p->Release();
        p = nullptr;
    }
    T** put() {
        reset();
        return &p;
    }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

std::string utf8(WinString s) {
    if (!s) return std::string();
    UINT32 len = 0;
    const wchar_t* w = api().string_buffer(s, &len);
    std::string out;
    if (w && len) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
        out.resize(static_cast<std::size_t>(n));
        WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), &out[0], n, nullptr, nullptr);
    }
    api().delete_string(s);
    return out;
}

bool wait_for(IAsyncOperationRaw* op, void** result) {
    Ref<IAsyncInfoRaw> info;
    if (FAILED(op->QueryInterface(iid_async_info, reinterpret_cast<void**>(info.put())))) return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    int status = 0;
    while (true) {
        if (FAILED(info->get_Status(&status))) return false;
        if (status != 0) break;
        if (std::chrono::steady_clock::now() > deadline) {
            info->Cancel();
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (status != 1) return false;
    return SUCCEEDED(op->GetResults(result)) && *result;
}

std::string player_name(std::string id) {
    const std::size_t bang = id.find('!');
    if (bang != std::string::npos) id = id.substr(0, bang);
    const std::size_t slash = id.find_last_of("\\/");
    if (slash != std::string::npos) id = id.substr(slash + 1);
    if (id.size() > 4 && (id.compare(id.size() - 4, 4, ".exe") == 0 || id.compare(id.size() - 4, 4, ".EXE") == 0)) id.resize(id.size() - 4);
    const std::size_t underscore = id.find('_');
    if (underscore != std::string::npos && id.find('.') != std::string::npos) {
        id = id.substr(0, underscore);
        const std::size_t dot = id.find_last_of('.');
        if (dot != std::string::npos) id = id.substr(dot + 1);
    }
    return id;
}

struct MediaSession {
    bool started = false;
    bool ok = false;
    Ref<IManager> manager;

    bool start() {
        if (started) return ok;
        started = true;
        if (!api().ok) return false;
        api().ro_initialize(1);
        static const wchar_t name[] = L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager";
        WinString cls = nullptr;
        if (FAILED(api().create_string(name, static_cast<UINT32>(sizeof(name) / sizeof(name[0]) - 1), &cls))) return false;
        Ref<IManagerStatics> statics;
        const HRESULT hr = api().ro_get_activation_factory(cls, iid_manager_statics, reinterpret_cast<void**>(statics.put()));
        api().delete_string(cls);
        if (FAILED(hr) || !statics) return false;
        Ref<IAsyncOperationRaw> op;
        if (FAILED(statics->RequestAsync(op.put())) || !op) return false;
        void* result = nullptr;
        if (!wait_for(op.p, &result)) return false;
        manager.p = static_cast<IManager*>(result);
        ok = true;
        return true;
    }
};

} // namespace

NowPlaying windows_now_playing() {
    static MediaSession session;
    NowPlaying n;
    if (!session.start()) {
        n.reason = "now playing needs Windows 10 (1809) or newer";
        return n;
    }
    n.available = true;
    Ref<ISessionRaw> current;
    if (FAILED(session.manager->GetCurrentSession(current.put())) || !current) {
        n.reason = "nothing is playing";
        return n;
    }
    WinString app = nullptr;
    if (SUCCEEDED(current->get_SourceAppUserModelId(&app))) n.player = player_name(utf8(app));
    Ref<IPlaybackInfoRaw> playback;
    if (SUCCEEDED(current->GetPlaybackInfo(playback.put())) && playback) {
        int status = 0;
        if (SUCCEEDED(playback->get_PlaybackStatus(&status))) {
            n.playing = status == 4;
            n.paused = status == 5;
        }
    }
    Ref<IAsyncOperationRaw> op;
    if (SUCCEEDED(current->TryGetMediaPropertiesAsync(op.put())) && op) {
        void* result = nullptr;
        if (wait_for(op.p, &result)) {
            Ref<IMediaPropertiesRaw> props;
            props.p = static_cast<IMediaPropertiesRaw*>(result);
            WinString title = nullptr, artist = nullptr;
            if (SUCCEEDED(props->get_Title(&title))) n.title = utf8(title);
            if (SUCCEEDED(props->get_Artist(&artist))) n.artist = utf8(artist);
        }
    }
    Ref<ITimelineRaw> timeline;
    if (SUCCEEDED(current->GetTimelineProperties(timeline.put())) && timeline) {
        INT64 start = 0, end = 0, pos = 0;
        timeline->get_StartTime(&start);
        timeline->get_EndTime(&end);
        timeline->get_Position(&pos);
        n.length = static_cast<double>(end - start) / 1e7;
        n.position = static_cast<double>(pos - start) / 1e7;
    }
    if (!n.has_song()) n.reason = "nothing is playing";
    return n;
}

} // namespace pollen

#endif // _WIN32
