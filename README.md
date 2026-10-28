# Pollen

A VRChat OSC app for Linux, Windows and macOS, built with [Fizmo](https://github.com/ThatNewCoderJSCpp/Fizmo), [Grove](https://github.com/ThatNewCoderJSCpp/Grove) and [Walnut](https://github.com/ThatNewCoderJSCpp/Walnut).

Most VRChat OSC tools only run on Windows. Pollen's backend is plain C++17 over UDP, and VRChat under Proton still uses the computer's own network, so a native Linux build talks to it like any Windows tool would.

## What it does

- **Dashboard**: connection state, which avatar you're in, traffic counters, CPU and GPU load, RAM and video memory, and what's playing.
- **Parameters**: every parameter of the current avatar (read from VRChat's OSC file and from what VRChat sends), with switches for bools, number boxes for ints and sliders for floats. Values update live.
- **Chatbox**: send messages, with the typing bubble and the notification sound if you want them, and **status messages** that rotate on a timer, built from placeholders like `{time}`, `{cpu_temp}`, `{gpu_used} / {gpu_total}` and `{song}`. A line with only `---` rotates between whole messages; to rotate just part of one, put lines between `[rotate]` and `[/rotate]` (each send shows the next line, or the next group of lines separated by `--`, while everything around it stays put; `[rotate every 2]` changes it every other send), or write `[[CPU {cpu} || GPU {gpu}]]` inside a line. A rotating choice that would come out empty, like a song line with nothing playing, is skipped.
- **Modules**: every kind of placeholder (Units, CPU, GPU, RAM, VRAM, Uptime, Time, Date, Now playing, Weather, World, Friends, Avatar) has its own card. Opening one shows its options, its placeholders listed one per line with what each one means, and a box to try it out with a live preview of just that module; turning an option on (seconds, say) unlocks its placeholder (`{second}`). Each placeholder is a single value, so `{gpu_used} / {gpu_total}` reads `1.0 GB / 12.0 GB`, and each one has its own settings: sizes can be in B, KB, MB, GB or TB, and any amount (`{gpu_used}`, `{ram_free}`, ...) can be switched to a percentage of the total on its own without changing the rest. Temperatures can be in °C, °F or K with a second unit alongside (`12°C (54°F)` or `54°F / 12°C`), wind in km/h, mph, m/s, knots or ft/s, also with an optional second unit, and the Time module shows how far the time here is from UTC, GMT and TAI (`UTC-5`, `UTC+5:30`, `TAI-5:00:37`) in hours, minutes or seconds. The Units card sets the defaults every placeholder starts from. To show a placeholder differently in just one spot, add to it after a colon: `{ram_app} ({ram_app:%})` shows Pollen's memory as an amount and a percentage, `{vram_vrchat:%,.1}` is a percentage with one decimal, `{ram_total:MB}` picks a unit, `{cpu_temp:F+C}` shows two units, and module options work the same way, like `{time:24h}`, `{time:seconds}`, `{date:iso}`, `{weekday:short}` and `{uptime:long}`. Pollen warns under the preview when it doesn't understand one.
- **VRChat**: the world you're in, its instance type (Public, Friends+, Friends, Invite+, Invite, Group...), region and how long you've been there, and everyone in your instance, all read from VRChat's log with no login needed. Log in to VRChat as well and you get your status, your friends list (who's in your instance, in VRChat, on the website or offline, and where they are), buttons to request an invite, invite them, favorite or unfriend them, and a feed of what's happening: people joining and leaving, friends coming online, going offline, moving worlds or changing status, friend requests, invites, and friends who unfriended you. VRChat shows a block the same way as an unfriend, so Pollen can't tell the two apart. It also doesn't reveal where friends are in private instances, so Pollen doesn't either; it shows how long they've been there and the last public world it saw them in.
- **Favorites**: your favorite avatars, worlds and friends (move them between groups or remove them) and your stickers, emojis and items (archive, unarchive or delete them, including your own uploads).
- **Monitor**: every OSC message in and out, plus a box for sending any message by hand (`/avatar/parameters/Ears true`).
- **Settings**: VRChat's address and ports, and an extra OSC folder if Pollen can't find VRChat's by itself.

- **OSCQuery**: Pollen and VRChat find each other on their own. Pollen advertises itself over mDNS, VRChat sends to whatever port Pollen listens on (so if another app already has 9001, Pollen takes a free port and nothing breaks), and Pollen reads the current avatar's parameters, types and values straight from VRChat.
- **What VRChat shows**: a preview of the chatbox bubble as you type, with the typing dots, trimmed to VRChat's 144 characters and 9 lines.

Logging in to VRChat happens in Settings, either with your username, password and two-factor code, or by approving a code on vrchat.com so Pollen never sees your password. Pollen keeps only the login session, in `vrchat-session.json` next to `settings.json` and readable only by you, and the names of your friends in `vrchat-friends.json` so it can tell you who disappeared from your list while it was closed. It talks to VRChat's unofficial web API and its live "pipeline", identifies itself as Pollen, and waits between requests; VRChat can change that API at any time.

Hardware readings come from `/proc`, `/sys` (hwmon, amdgpu, cpufreq) and NVIDIA's NVML on Linux, and from performance counters, DXGI and NVML on Windows. Weather comes from [Open-Meteo](https://open-meteo.com), which needs no account or key.

Now playing uses `playerctl` (MPRIS) on Linux, which covers Spotify, browsers, mpv and most players, and Windows' media session API on Windows 10 and 11, which covers anything that shows up in the volume flyout. On Linux, Pollen also finds VRChat's OSC folder inside Proton's prefix in every Steam library (including Flatpak and Snap Steam), for when OSCQuery isn't available.

## The `osc` library

`Include/osc` is a standalone OSC 1.0/1.1 library that Pollen is built on. Encoding, decoding and pattern matching need only the C++17 standard library; `osc::Sender` and `osc::Receiver` use Fizmo's networking.

```cpp
#include <osc/osc.hpp>

osc::Message m("/avatar/parameters/Ears", true);
m << 0.5f << "hello" << osc::Value::int64(1ll << 40);

std::vector<std::uint8_t> bytes = m.encode();
std::optional<osc::Packet> packet = osc::decode(bytes);

bool ears = m.arg<bool>(0);
float lenient = m.to<float>(0);
auto [on, x, text] = m.unpack<bool, float, std::string>();

osc::Dispatcher d;
d.on<float>("/avatar/parameters/VelocityX", [](float x) { std::cout << x << "\n"; });
d.on("/avatar/parameters/*", [](const osc::Message& m) { std::cout << m.to_string() << "\n"; });

osc::vrchat::Connection vrchat;
vrchat.start({});
vrchat.set_parameter("Ears", true);
vrchat.chatbox("hi from Linux");
vrchat.poll();
```

- `osc::Value` holds every OSC 1.0 and 1.1 type (int32, float32, string, blob, int64, double, time tag, symbol, char, RGBA color, MIDI, true, false, nil, impulse and arrays). `as<T>()` is strict and throws `osc::TypeError`, `get<T>()` gives an empty optional instead, and `to<T>()` converts between numbers, bools and strings.
- `osc::Bundle` and `osc::Packet` cover bundles, nested to any depth.
- `osc::matches(pattern, address)` supports `?`, `*`, `[a-z]`, `[!0-9]`, `{foo,bar}` and OSC 1.1's `//`.
- `osc::query` is OSCQuery: a `Tree` of OSC methods served by a small HTTP `Server`, `http_get` for asking other apps, and `Mdns`, a tiny mDNS responder and browser (no Avahi or Bonjour needed).
- `osc::vrchat` has the VRChat addresses, chatbox limits (144 characters, 9 lines), avatar OSC files, a `Connection` that keeps track of the avatar and its parameters, and a `QueryLink` that advertises your app to VRChat and finds VRChat's own OSCQuery service.

## Building

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/bin/pollen
```

CMake fetches Fizmo, Grove and Walnut. On Fedora you'll need `libX11-devel libXft-devel libXrender-devel fontconfig-devel freetype-devel openssl-devel pango-devel cairo-devel zlib-devel libjpeg-turbo-devel vulkan-headers mesa-libGL-devel libstdc++-static`. `-DPOLLEN_BUILD_APP=OFF` builds just the `osc` library and the tests.

| Variable | |
|---|---|
| `POLLEN_CONFIG_DIR` | where `settings.json` is kept (default `~/.config/pollen`, `%APPDATA%\Pollen` or `~/Library/Application Support/Pollen`) |
| `POLLEN_UI_DIR` | load the UI from this folder instead of the built-in copy |
| `VRCHAT_OSC_DIR` | VRChat's OSC folder, if Pollen can't find it |

## Releases

Pushing a `v*` tag builds packages for Linux (x86_64, arm64), Windows (x86_64, i686, arm64) and macOS (arm64, x86_64). The tag has to match `project(pollen VERSION ...)` in `CMakeLists.txt`.

VRChat doesn't run on macOS, so on a Mac set VRChat's address to the PC running it, set the listen address to `0.0.0.0`, and start VRChat with `--osc=9000:<mac's IP>:9001`.
