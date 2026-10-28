#include <osc/osc.hpp>
#include <fizmo/../networking.hpp>
#include <pollen/session.hpp>
#include <pollen/status.hpp>
#include <pollen/modules.hpp>
#include <pollen/sensors.hpp>
#include <pollen/weather.hpp>

#include <filesystem>
#include <fstream>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <atomic>
#include <future>
#include <mutex>
#include <set>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::vector<std::uint8_t> bytes(std::initializer_list<int> list) {
    std::vector<std::uint8_t> out;
    for (int b : list) out.push_back(static_cast<std::uint8_t>(b));
    return out;
}

static void test_spec_examples() {
    const osc::Message freq("/oscillator/4/frequency", 440.0f);
    CHECK(freq.encode() == bytes({ 0x2f, 0x6f, 0x73, 0x63, 0x69, 0x6c, 0x6c, 0x61, 0x74, 0x6f, 0x72, 0x2f, 0x34, 0x2f, 0x66, 0x72, 0x65, 0x71, 0x75, 0x65, 0x6e, 0x63, 0x79, 0x00, 0x2c, 0x66, 0x00, 0x00, 0x43, 0xdc, 0x00, 0x00 }));

    const osc::Message foo("/foo", 1000, -1, "hello", 1.234f, 5.678f);
    CHECK(foo.type_tags() == ",iisff");
    CHECK(foo.encode() == bytes({ 0x2f, 0x66, 0x6f, 0x6f, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x69, 0x69, 0x73, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x03, 0xe8, 0xff, 0xff, 0xff, 0xff, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x00, 0x00, 0x00, 0x3f, 0x9d, 0xf3, 0xb6, 0x40, 0xb5, 0xb2, 0x2d }));

    const auto back = osc::decode_message(foo.encode());
    CHECK(back.has_value());
    CHECK(back && *back == foo);
    CHECK(back && back->arg<int>(0) == 1000);
    CHECK(back && back->arg<std::string>(2) == "hello");
}

static void test_round_trip() {
    osc::Message m("/everything");
    m << 7 << osc::Value::int64(1ll << 40) << 0.5f << 2.25 << "text" << osc::Symbol("sym") << true << false << osc::Nil{} << osc::Impulse{}
      << 'x' << osc::Color{ 1, 2, 3, 4 } << osc::Midi{ 0, 0x90, 60, 127 } << osc::TimeTag(5, 6) << osc::Value::blob("abc", 3)
      << osc::Array{ 1, osc::Array{ "nested", 2.0f }, false };
    CHECK(m.type_tags() == ",ihfdsSTFNIcrmtb[i[sf]F]");
    const std::vector<std::uint8_t> enc = m.encode();
    CHECK(enc.size() % 4 == 0);
    std::string error;
    const auto back = osc::decode_message(enc, &error);
    CHECK(back.has_value());
    if (!back) std::printf("  decode error: %s\n", error.c_str());
    CHECK(back && *back == m);
    CHECK(back && back->arg<std::int64_t>(1) == (1ll << 40));
    CHECK(back && back->arg<osc::Array>(15)[1][0].as_string() == "nested");
    CHECK(back && back->arg<osc::Blob>(14).size() == 3);
    CHECK(back && back->arg<osc::Color>(11) == (osc::Color{ 1, 2, 3, 4 }));
}

static void test_bundles() {
    osc::Bundle inner(osc::TimeTag(100, 0));
    inner << osc::Message("/b", 2);
    osc::Bundle outer;
    outer << osc::Message("/a", 1) << std::move(inner) << osc::Message("/c", "three");
    const auto enc = outer.encode();
    CHECK(osc::looks_like_bundle(enc.data(), enc.size()));
    const auto back = osc::decode(enc);
    CHECK(back && back->is_bundle());
    CHECK(back && back->bundle() == outer);
    CHECK(back && back->message_count() == 3);
    std::vector<std::string> seen;
    if (back) back->for_each_message([&](const osc::Message& msg, osc::TimeTag) { seen.push_back(msg.address()); });
    CHECK((seen == std::vector<std::string>{ "/a", "/b", "/c" }));
}

static void test_bad_packets() {
    std::string error;
    CHECK(!osc::decode(nullptr, 0, &error));
    CHECK(!error.empty());
    const auto no_slash = bytes({ 'f', 'o', 'o', 0 });
    CHECK(!osc::decode(no_slash));
    const auto truncated = bytes({ '/', 'a', 0, 0, ',', 'i', 0, 0, 0, 0 });
    CHECK(!osc::decode(truncated, &error));
    const auto bad_tag = bytes({ '/', 'a', 0, 0, ',', 'Q', 0, 0 });
    CHECK(!osc::decode(bad_tag, &error));
    CHECK(error.find("unknown type tag") != std::string::npos);
    const auto no_tags = bytes({ '/', 'a', 0, 0 });
    const auto old_style = osc::decode_message(no_tags);
    CHECK(old_style && old_style->empty());
    bool threw = false;
    try {
        osc::Message("no-slash").encode();
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

static void test_values() {
    const osc::Value f = 0.75f;
    CHECK(f.is<float>());
    CHECK(!f.is<int>());
    CHECK(f.to<int>() == 1);
    CHECK(f.to<bool>());
    CHECK(f.get<int>() == std::nullopt);
    bool threw = false;
    try {
        (void)f.as<std::string>();
    } catch (const osc::TypeError& e) {
        threw = e.wanted() == osc::Type::String && e.got() == osc::Type::Float32;
    }
    CHECK(threw);
    CHECK(osc::Value(true).to<float>() == 1.0f);
    CHECK(osc::Value("0.5").to<double>() == 0.5);
    CHECK(osc::Value(3u).type() == osc::Type::Int64);
    CHECK(osc::Value(std::int16_t(3)).type() == osc::Type::Int32);
    CHECK(osc::Value(0.1f).to_string() == "0.1");
    const osc::Message m("/m", 1, 2.5f, "s");
    CHECK((m.has_types<int, float, std::string>()));
    CHECK((!m.has_types<int, int, std::string>()));
    const auto [a, b, c] = m.unpack<int, float, std::string>();
    CHECK(a == 1 && b == 2.5f && c == "s");
    CHECK(m.to_string() == "/m ,ifs 1 2.5 \"s\"");
    const osc::TimeTag now = osc::TimeTag::now();
    const auto diff = std::chrono::system_clock::now() - now.to_system();
    CHECK(diff < std::chrono::seconds(1) && diff > -std::chrono::seconds(1));
}

static void test_patterns() {
    CHECK(osc::matches("/avatar/parameters/*", "/avatar/parameters/VelocityX"));
    CHECK(!osc::matches("/avatar/parameters/*", "/avatar/change"));
    CHECK(!osc::matches("/avatar/*", "/avatar/parameters/X"));
    CHECK(osc::matches("/a/?/c", "/a/b/c"));
    CHECK(!osc::matches("/a/?/c", "/a/bb/c"));
    CHECK(osc::matches("/ch/[0-9]", "/ch/7"));
    CHECK(!osc::matches("/ch/[!0-9]", "/ch/7"));
    CHECK(osc::matches("/{foo,bar}/x", "/bar/x"));
    CHECK(!osc::matches("/{foo,bar}/x", "/baz/x"));
    CHECK(osc::matches("//volume", "/mixer/3/volume"));
    CHECK(osc::matches("//volume", "/volume"));
    CHECK(osc::matches("/a*c", "/abbbc"));
    CHECK(osc::matches("/*/*/*", "/x/y/z"));
    CHECK(osc::is_pattern("/a/*"));
    CHECK(!osc::is_pattern("/a/b"));
    CHECK(osc::is_valid_address("/avatar/parameters/Foo"));
    CHECK(!osc::is_valid_address("avatar"));
    CHECK(osc::is_valid_pattern("/a/{b,c}/[0-9]"));
    CHECK(!osc::is_valid_pattern("/a/{b"));
}

static void test_dispatcher() {
    osc::Dispatcher d;
    float got = -1.0f;
    int any = 0, unmatched = 0;
    std::string name;
    d.on<float>("/avatar/parameters/VelocityX", [&](float v) { got = v; });
    const auto id = d.on("/avatar/parameters/*", [&](const osc::Message& m) {
        ++any;
        name = osc::vrchat::parameter_name(m.address());
    });
    d.on_unmatched([&](const osc::Message&) { ++unmatched; });
    CHECK(d.dispatch(osc::Message("/avatar/parameters/VelocityX", 0.25f)) == 2);
    CHECK(got == 0.25f);
    CHECK(name == "VelocityX");
    d.dispatch(osc::Message("/avatar/parameters/VelocityX", 1));
    CHECK(got == 1.0f);
    CHECK(d.dispatch(osc::Message("/nothing")) == 0);
    CHECK(unmatched == 1);
    CHECK(d.off(id));
    d.dispatch(osc::Message("/avatar/parameters/Other", true));
    CHECK(any == 2);
}

static void test_vrchat() {
    namespace vrc = osc::vrchat;
    CHECK(vrc::parameter_address("Foo") == "/avatar/parameters/Foo");
    CHECK(vrc::parameter_name("/avatar/parameters/Foo/Bar") == "Foo/Bar");
    CHECK(vrc::parameter_name("/avatar/change").empty());
    const osc::Message chat = vrc::chatbox("hi", true, false);
    CHECK(chat.address() == "/chatbox/input");
    CHECK((chat.has_types<std::string, bool, bool>()));
    std::string longer(300, 'a');
    CHECK(vrc::chatbox_text(longer).size() == vrc::chatbox_max_chars);
    CHECK(vrc::utf8_length(vrc::chatbox_text(std::string(200, 'x') + "\xc3\xa9")) == 144);
    CHECK(vrc::chatbox_text("1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11") == "1\n2\n3\n4\n5\n6\n7\n8\n9");
    CHECK(vrc::input_button("Jump", true).arg<int>(0) == 1);
    CHECK(vrc::make_param_value(vrc::ParamType::Int, "300")->as<int>() == 255);
    CHECK(vrc::make_param_value(vrc::ParamType::Float, "-3")->as<float>() == -1.0f);
    CHECK(vrc::make_param_value(vrc::ParamType::Bool, "on")->as<bool>());
    CHECK(!vrc::make_param_value(vrc::ParamType::Bool, "maybe"));

    const std::string json = "\xEF\xBB\xBF{\"id\":\"avtr_1\",\"name\":\"Fox\",\"parameters\":["
                             "{\"name\":\"Ears\",\"input\":{\"address\":\"/avatar/parameters/Ears\",\"type\":\"Bool\"},\"output\":{\"address\":\"/avatar/parameters/Ears\",\"type\":\"Bool\"}},"
                             "{\"name\":\"VelocityX\",\"output\":{\"address\":\"/avatar/parameters/VelocityX\",\"type\":\"Float\"}}]}";
    std::string error;
    const auto config = vrc::parse_avatar_config(json, &error);
    CHECK(config.has_value());
    if (!config) std::printf("  %s\n", error.c_str());
    CHECK(config && config->name == "Fox" && config->parameters.size() == 2);
    CHECK(config && config->find("Ears") && config->find("Ears")->writable());
    CHECK(config && config->find("VelocityX") && !config->find("VelocityX")->writable() && config->find("VelocityX")->type == vrc::ParamType::Float);

    vrc::AvatarState state;
    std::string changed;
    state.on_avatar_change = [&](const std::string& id) { changed = id; };
    state.handle(osc::Message("/avatar/change", "avtr_2"));
    CHECK(changed == "avtr_2");
    state.handle(osc::Message("/avatar/parameters/Ears", true));
    CHECK(state.value("Ears") && state.value("Ears")->as<bool>());
    const auto rev = state.value_revision();
    state.handle(osc::Message("/avatar/parameters/Ears", true));
    CHECK(state.value_revision() == rev);
    state.handle(osc::Message("/avatar/parameters/Ears", false));
    CHECK(state.value_revision() == rev + 1);
}

static void test_status() {
    pollen::StatusValues v{ { "time", "4:20 PM" }, { "song", "" }, { "cpu", "12%" } };
    CHECK(pollen::render_status("{time} | CPU {cpu}\n\xe2\x99\xaa {song}", v) == "4:20 PM | CPU 12%");
    std::vector<std::string> unknown;
    CHECK(pollen::render_status("{nope} {{literal}}", v, &unknown) == "{nope} {literal}");
    CHECK(unknown.size() == 1 && unknown[0] == "nope");
    const auto pages = pollen::split_status_pages("one\n---\ntwo\nlines\n  ---  \n\n---\nthree");
    CHECK(pages.size() == 3);
    CHECK(pages.size() == 3 && pages[1] == "two\nlines");
    pollen::StatusRotation r;
    r.set_template("a\n---\nb");
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(r.due(t0, 3.0));
    CHECK(r.next(v, t0) == "a");
    CHECK(!r.due(t0 + std::chrono::seconds(1), 3.0));
    CHECK(r.due(t0 + std::chrono::seconds(3), 3.0));
    CHECK(r.next(v, t0) == "b");
    CHECK(r.next(v, t0) == "a");
}

static void test_message_lines() {
    std::string error;
    const auto m = pollen::parse_message_line("/avatar/parameters/Foo 1 0.5 true \"two words\" s:7 f:2 h:9 nil", &error);
    CHECK(m.has_value());
    CHECK(m && m->type_tags() == ",ifTssfhN");
    CHECK(m && m->arg<std::string>(3) == "two words");
    CHECK(!pollen::parse_message_line("nope 1", &error));
    CHECK(!pollen::parse_message_line("/a \"open", &error));
    CHECK(!pollen::parse_message_line("/a i:x", &error));
}

static void test_settings() {
    pollen::Settings s;
    s.host = "192.168.1.20";
    s.send_port = 9100;
    s.status_template = "line \"quoted\"\nnext";
    s.status_interval = 1.0;
    pollen::Settings back;
    CHECK(back.from_json(s.to_json()));
    CHECK(back.host == "192.168.1.20" && back.send_port == 9100);
    CHECK(back.status_template == s.status_template);
    CHECK(back.status_interval == 2.0);
    CHECK(!back.from_json("[1,2]"));
}

static void test_loopback() {
    osc::Receiver rx;
    std::uint16_t port = 0;
    std::string error;
    for (std::uint16_t p = 39123; p < 39163 && port == 0; ++p) {
        if (rx.listen(p, "127.0.0.1", &error)) port = p;
    }
    CHECK(port != 0);
    if (!port) {
        std::printf("  couldn't listen: %s\n", error.c_str());
        return;
    }
    osc::Sender tx(osc::Endpoint::local(port));
    CHECK(tx.send("/avatar/parameters/Ears", true));
    osc::Bundle b;
    b << osc::Message("/avatar/change", "avtr_x") << osc::Message("/avatar/parameters/Tail", 0.5f);
    CHECK(tx.send(b));
    osc::vrchat::AvatarState state;
    osc::Dispatcher d;
    d.on("/avatar/*", [&](const osc::Message& m) { state.handle(m); });
    d.on("/avatar/parameters/*", [&](const osc::Message& m) { state.handle(m); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    std::size_t got = 0;
    while (got < 2 && std::chrono::steady_clock::now() < deadline) {
        rx.wait(std::chrono::milliseconds(100));
        got += rx.poll(d);
    }
    CHECK(got == 2);
    CHECK(state.avatar_id() == "avtr_x");
    CHECK(state.value("Tail") && state.value("Tail")->as<float>() == 0.5f);
    const osc::Stats in = rx.stats();
    CHECK(in.packets == 2 && in.messages == 3);
    CHECK(tx.stats().packets == 2);
    osc::Receiver second;
    CHECK(!second.listen(port, "127.0.0.1", &error));
    CHECK(error.find("another OSC app") != std::string::npos);
    rx.stop();
    CHECK(!rx.listening());
}

static void test_mdns_messages() {
    using namespace osc::query;
    MdnsMessage q;
    q.questions.push_back(MdnsQuestion{ "_oscjson._tcp.local", mdns_type_ptr });
    const auto qb = mdns_encode(q);
    const auto qd = mdns_decode(qb.data(), qb.size());
    CHECK(qd && !qd->response && qd->questions.size() == 1 && qd->questions[0].name == "_oscjson._tcp.local");

    MdnsMessage r;
    r.response = true;
    MdnsRecord ptr;
    ptr.name = "_oscjson._tcp.local";
    ptr.type = mdns_type_ptr;
    ptr.ttl = 120;
    ptr.target = "VRChat-Client-ABC123._oscjson._tcp.local";
    MdnsRecord srv;
    srv.name = ptr.target;
    srv.type = mdns_type_srv;
    srv.ttl = 120;
    srv.port = 51234;
    srv.target = "vrc.local";
    MdnsRecord a;
    a.name = "vrc.local";
    a.type = mdns_type_a;
    a.ttl = 120;
    a.address = "127.0.0.1";
    r.records = { ptr, srv, a };
    const auto rb = mdns_encode(r);
    const auto rd = mdns_decode(rb.data(), rb.size());
    CHECK(rd && rd->response && rd->records.size() == 3);
    const auto found = rd ? mdns_services(*rd, "_oscjson._tcp") : std::vector<osc::query::Service>{};
    CHECK(found.size() == 1);
    CHECK(found.size() == 1 && found[0].instance == "VRChat-Client-ABC123" && found[0].port == 51234 && found[0].address == "127.0.0.1");

    const std::uint8_t compressed[] = { 0, 0, 0x84, 0, 0, 0, 0, 1, 0, 0, 0, 0,
        5, '_', 'o', 's', 'c', 'j', 4, '_', 't', 'c', 'p', 5, 'l', 'o', 'c', 'a', 'l', 0,
        0, 12, 0, 1, 0, 0, 0, 120, 0, 5, 2, 'h', 'i', 0xC0, 12 };
    const auto cd = mdns_decode(compressed, sizeof(compressed));
    CHECK(cd && cd->records.size() == 1 && cd->records[0].target == "hi._oscj._tcp.local");
    const std::uint8_t loop[] = { 0, 0, 0x84, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0xC0, 12, 0, 12, 0, 1 };
    CHECK(!mdns_decode(loop, sizeof(loop)));
}

static void test_query_server() {
    using namespace osc::query;
    Server server;
    HostInfo info;
    info.name = "Test";
    info.osc_port = 9123;
    std::string error;
    CHECK(server.start(info, "127.0.0.1", 0, &error));
    if (!server.running()) {
        std::printf("  query server: %s\n", error.c_str());
        return;
    }
    server.tree().add("/avatar/change", "s", Access::ReadWrite, "", { osc::Value("avtr_q") });
    server.tree().add("/avatar/parameters/Ears", "T", Access::ReadWrite, "", { osc::Value(true) });
    server.tree().add("/avatar/parameters/Velocity", "f", Access::Read, "", { osc::Value(0.5f) });
    server.tree().add("/avatar/parameters/Sub/Nested", "i", Access::ReadWrite, "", { osc::Value(3) });
    const HttpResult hi = http_get("127.0.0.1", server.port(), "/?HOST_INFO");
    CHECK(hi.ok());
    const auto parsed = host_info_from_json(hi.body);
    CHECK(parsed && parsed->name == "Test" && parsed->osc_port == 9123);
    const HttpResult value = http_get("127.0.0.1", server.port(), "/avatar/parameters/Ears?VALUE");
    CHECK(value.ok() && value.body.find("true") != std::string::npos);
    CHECK(http_get("127.0.0.1", server.port(), "/nope").status == 404);
    const HttpResult avatar = http_get("127.0.0.1", server.port(), "/avatar");
    CHECK(avatar.ok());
    const auto node = node_from_json(avatar.body, &error);
    CHECK(node.has_value());
    const auto snap = node ? osc::vrchat::avatar_from_query(*node) : std::nullopt;
    CHECK(snap && snap->avatar_id == "avtr_q");
    CHECK(snap && snap->config.parameters.size() == 3);
    CHECK(snap && snap->config.find("Sub/Nested") && snap->config.find("Sub/Nested")->type == osc::vrchat::ParamType::Int);
    CHECK(snap && snap->config.find("Velocity") && !snap->config.find("Velocity")->writable());
    CHECK(snap && snap->values.size() == 3);
    server.stop();
    CHECK(!server.running());
}

static void test_query_discovery() {
    using namespace osc::query;
    Server fake_vrchat;
    HostInfo info;
    info.name = "VRChat-Client-ABC123";
    info.osc_port = 9000;
    std::string error;
    if (!fake_vrchat.start(info, "127.0.0.1", 0, &error)) {
        std::printf("  skipped discovery: %s\n", error.c_str());
        return;
    }
    fake_vrchat.tree().add("/avatar/change", "s", Access::ReadWrite, "", { osc::Value("avtr_q") });
    fake_vrchat.tree().add("/avatar/parameters/Ears", "T", Access::ReadWrite, "", { osc::Value(true) });
    Mdns fake_mdns;
    if (!fake_mdns.start(&error)) {
        std::printf("  skipped discovery: %s\n", error.c_str());
        return;
    }
    fake_mdns.advertise("VRChat-Client-ABC123", "_oscjson._tcp", fake_vrchat.port());
    fake_mdns.advertise("VRChat-Client-ABC123", "_osc._udp", 9000);

    osc::vrchat::QueryLink link;
    CHECK(link.start("PollenTest", "127.0.0.1", 9124, &error));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    std::optional<osc::vrchat::AvatarSnapshot> snap;
    while (std::chrono::steady_clock::now() < deadline && !snap) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        snap = link.take_avatar();
    }
    const auto v = link.vrchat();
    if (!v) {
        std::printf("  skipped discovery: no multicast here (%s)\n", link.mdns_error().c_str());
        return;
    }
    CHECK(v->osc_port == 9000);
    CHECK(snap && snap->avatar_id == "avtr_q");
    bool saw_us = false;
    fake_mdns.browse("_oscjson._tcp");
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < until && !saw_us) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (const osc::query::Service& s : fake_mdns.services("_oscjson._tcp")) {
            if (s.instance == link.instance()) {
                const HttpResult hi = http_get(s.address, s.port, "/?HOST_INFO");
                const auto parsed = host_info_from_json(hi.body);
                saw_us = parsed && parsed->osc_port == 9124;
            }
        }
    }
    CHECK(saw_us);
}

static void write_file(const std::filesystem::path& p, const std::string& text) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p) << text;
}

static void test_fake_linux_sensors() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "pollen-fake-root";
    fs::remove_all(root);
    write_file(root / "proc/stat", "cpu  100 0 100 800 0 0 0 0 0 0\ncpu0 1 2 3 4\n");
    write_file(root / "proc/cpuinfo", "processor\t: 0\nmodel name\t: AMD Ryzen 7 5800X 8-Core Processor\ncpu MHz\t\t: 3800.000\n");
    write_file(root / "sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "4200000\n");
    write_file(root / "sys/devices/system/cpu/cpu1/cpufreq/scaling_cur_freq", "3800000\n");
    write_file(root / "sys/class/hwmon/hwmon0/name", "nvme\n");
    write_file(root / "sys/class/hwmon/hwmon0/temp1_input", "31000\n");
    write_file(root / "sys/class/hwmon/hwmon1/name", "k10temp\n");
    write_file(root / "sys/class/hwmon/hwmon1/temp1_label", "Tctl\n");
    write_file(root / "sys/class/hwmon/hwmon1/temp1_input", "54250\n");
    write_file(root / "proc/meminfo", "MemTotal:       16000000 kB\nMemFree:  1000 kB\nMemAvailable:    4000000 kB\n");
    write_file(root / "proc/1/status", "Name:\tpollen\nVmRSS:\t   51200 kB\n");
    write_file(root / "proc/4242/comm", "VRChat.exe\n");
    write_file(root / "proc/4242/status", "Name:\tVRChat.exe\nVmRSS:\t 4194304 kB\n");
    write_file(root / "proc/uptime", "93784.12 1000.0\n");
    write_file(root / "sys/class/drm/card0/device/vendor", "0x1002\n");
    write_file(root / "sys/class/drm/card0/device/device", "0x73bf\n");
    write_file(root / "sys/class/drm/card0/device/mem_info_vram_total", "17163091968\n");
    write_file(root / "sys/class/drm/card0/device/mem_info_vram_used", "2147483648\n");
    write_file(root / "sys/class/drm/card0/device/gpu_busy_percent", "37\n");
    write_file(root / "sys/class/drm/card0/device/uevent", "DRIVER=amdgpu\nPCI_SLOT_NAME=0000:03:00.0\n");
    write_file(root / "sys/class/drm/card0/device/hwmon/hwmon5/temp1_label", "edge\n");
    write_file(root / "sys/class/drm/card0/device/hwmon/hwmon5/temp1_input", "61000\n");
    write_file(root / "sys/class/drm/card0/device/hwmon/hwmon5/freq1_input", "2100000000\n");
    write_file(root / "sys/class/drm/card0-DP-1/status", "connected\n");
    write_file(root / "usr/share/hwdata/pci.ids", "1002  Advanced Micro Devices, Inc. [AMD/ATI]\n\t73bf  Navi 21 [Radeon RX 6800/6800 XT / 6900 XT]\n\t73ff  Navi 23\n10de  NVIDIA Corporation\n");
    write_file(root / "proc/4242/fdinfo/5", "pos:\t0\ndrm-driver:\tamdgpu\ndrm-pdev:\t0000:03:00.0\ndrm-client-id:\t12\ndrm-memory-vram:\t1048576 KiB\ndrm-memory-gtt:\t2048 KiB\n");
    write_file(root / "proc/4242/fdinfo/6", "drm-pdev:\t0000:03:00.0\ndrm-client-id:\t12\ndrm-memory-vram:\t1048576 KiB\n");
    write_file(root / "proc/4242/fdinfo/7", "drm-pdev:\t0000:03:00.0\ndrm-client-id:\t13\ndrm-memory-vram:\t512 MiB\n");

    using namespace pollen::linux_sensors;
    CHECK(read_cpu_name(root) == "AMD Ryzen 7 5800X 8-Core Processor");
    const auto mhz = read_cpu_mhz(root);
    CHECK(mhz && std::fabs(*mhz - 4000.0) < 0.01);
    const auto temp = read_cpu_temp(root);
    CHECK(temp && std::fabs(*temp - 54.25) < 0.01);
    CHECK(find_process(root, "VRChat.exe") == 4242);
    const auto rss = read_process_rss(root, 4242);
    CHECK(rss && *rss == 4194304.0 * 1024.0);
    CHECK(pci_name(root, 0x1002, 0x73bf) == "AMD Radeon RX 6800/6800 XT / 6900 XT");
    const auto vram = fdinfo_vram(root, 4242, "0000:03:00.0");
    CHECK(vram && *vram == (1024.0 + 512.0) * 1024.0 * 1024.0);
    CHECK(!fdinfo_vram(root, 4242, "0000:09:00.0"));
    const auto gpu = read_drm_gpu(root);
    CHECK(gpu && gpu->slot == "0000:03:00.0" && gpu->busy && *gpu->busy == 37.0 && gpu->temp && *gpu->temp == 61.0 && gpu->mhz && *gpu->mhz == 2100.0);

#if !defined(_WIN32) && !defined(__APPLE__)
    pollen::Sampler sampler(root);
    pollen::SensorSnapshot snap;
    sampler.sample(snap);
    write_file(root / "proc/stat", "cpu  150 0 150 900 0 0 0 0 0 0\n");
    sampler.sample(snap);
    CHECK(snap.cpu_percent && std::fabs(*snap.cpu_percent - 50.0) < 0.01);
    CHECK(snap.ram_total && *snap.ram_total == 16000000.0 * 1024.0);
    CHECK(snap.ram_used && *snap.ram_used == 12000000.0 * 1024.0);
    CHECK(snap.ram_app && *snap.ram_app == 51200.0 * 1024.0);
    CHECK(snap.vrchat_running && snap.ram_vrchat);
    CHECK(snap.gpu_name == "AMD Radeon RX 6800/6800 XT / 6900 XT");
    CHECK(snap.vram_total && snap.vram_used && snap.vram_vrchat);
    CHECK(snap.uptime && *snap.uptime > 93784.0);
#endif
    fs::remove_all(root);
}

static void test_modules() {
    using namespace pollen;
    ModuleOptions o;
    ModuleInputs in;
    in.sensors.cpu_percent = 23.4;
    in.sensors.cpu_mhz = 4210.0;
    in.sensors.cpu_temp = 54.6;
    in.sensors.cpu_name = "Ryzen";
    in.sensors.ram_total = 16.0 * 1024 * 1024 * 1024;
    in.sensors.ram_used = 6.0 * 1024 * 1024 * 1024;
    in.sensors.ram_app = 200.0 * 1024 * 1024;
    in.sensors.vram_total = 12.0 * 1024 * 1024 * 1024;
    in.sensors.vram_used = 1.0 * 1024 * 1024 * 1024;
    in.sensors.uptime = 93784.0;
    in.connected_seconds = 3725.0;
    std::tm t{};
    t.tm_year = 126;
    t.tm_mon = 9;
    t.tm_mday = 28;
    t.tm_hour = 15;
    t.tm_min = 4;
    t.tm_sec = 9;
    t.tm_isdst = -1;
    in.now = std::chrono::system_clock::from_time_t(std::mktime(&t));

    StatusValues v = all_module_values(o, in);
    CHECK(v["cpu"] == "23%");
    CHECK(v["cpu_clock"] == "4.21 GHz");
    CHECK(v["cpu_temp"] == "55\xc2\xb0" "C");
    CHECK(v.count("cpu_threads") == 0);
    CHECK(v["gpu_used"] == "1.0 GB" && v["gpu_total"] == "12.0 GB");
    CHECK(render_status("{gpu_used} / {gpu_total}", v) == "1.0 GB / 12.0 GB");
    CHECK(v["ram_used"] == "6.0 GB" && v["ram_total"] == "16.0 GB");
    CHECK(v.count("ram_app") == 0);
    CHECK(v["time"] == "3:04 PM");
    CHECK(v.count("second") == 0 && v.count("time24") == 0);
    CHECK(v["date"] == "October 28, 2026");
    CHECK(v["uptime"] == "1d 2h 3m");
    CHECK(v["connected"] == "1h 2m");
    CHECK(render_status("{cpu_threads}", v) == "{cpu_threads}");

    CHECK(set_option(o, "time", "hour24", "1"));
    CHECK(set_option(o, "time", "seconds", "1"));
    CHECK(set_option(o, "date", "month", "short"));
    CHECK(set_option(o, "date", "format", "long_eu"));
    CHECK(set_option(o, "ram", "percent", "1"));
    CHECK(set_option(o, "ram", "app", "1"));
    CHECK(set_option(o, "units", "bytes_unit", "MB"));
    CHECK(set_option(o, "units", "bytes_decimals", "0"));
    CHECK(set_option(o, "units", "temp_unit", "F"));
    CHECK(set_option(o, "uptime", "seconds", "1"));
    CHECK(set_option(o, "uptime", "style", "long"));
    CHECK(set_option(o, "time", "tai", "1"));
    CHECK(!set_option(o, "date", "month", "sideways"));
    CHECK(!set_option(o, "nope", "x", "1"));
    std::string error;
    CHECK(set_option(o, "weather", "latitude", "123", &error));
    CHECK(option_value(o, "weather", "latitude") == "90");
    v = all_module_values(o, in);
    CHECK(v["time"] == "15:04:09" && v["second"] == "09");
    CHECK(v.count("ampm") == 0);
    CHECK(v["date"] == "28 Oct 2026");
    CHECK(v["ram_used"] == "38%" && v["ram_app"] == "1%" && v["ram_total"] == "16384 MB");
    CHECK(v["gpu_used"] == "1024 MB");
    CHECK(v["cpu_temp"] == "130\xc2\xb0" "F");
    CHECK(set_option(o, "units", "temp_also", "C"));
    v = all_module_values(o, in);
    CHECK(v["cpu_temp"] == "130\xc2\xb0" "F (55\xc2\xb0" "C)");
    CHECK(set_option(o, "units", "temp_unit", "C"));
    CHECK(set_option(o, "units", "temp_also", "F"));
    CHECK(set_option(o, "units", "both_style", "slash"));
    CHECK(set_option(o, "units", "temp_decimals", "1"));
    v = all_module_values(o, in);
    CHECK(v["cpu_temp"] == "54.6\xc2\xb0" "C / 130.3\xc2\xb0" "F");
    CHECK(set_option(o, "units", "temp_also", "C"));
    v = all_module_values(o, in);
    CHECK(v["cpu_temp"] == "54.6\xc2\xb0" "C");
    CHECK(!set_option(o, "units", "temp_also", "R"));
    Units minus;
    CHECK(format_temp(-0.3, minus) == "0\xc2\xb0" "C");
    CHECK(set_option(o, "units", "temp_unit", "F"));
    CHECK(set_option(o, "units", "temp_also", "none"));
    CHECK(set_option(o, "units", "both_style", "paren"));
    CHECK(set_option(o, "units", "temp_decimals", "0"));
    v = all_module_values(o, in);
    CHECK(v["uptime"] == "1 day, 2 hours, 3 minutes, 4 seconds");
    CHECK(!v["tai"].empty() && v["tai"] != v["utc"]);

    ModuleOptions each;
    CHECK(set_option(each, "gpu", "gpu_used.percent", "1"));
    CHECK(set_option(each, "gpu", "gpu_total.unit", "MB"));
    CHECK(set_option(each, "cpu", "cpu_temp.unit", "F"));
    CHECK(set_option(each, "cpu", "cpu_temp.also", "C"));
    CHECK(set_option(each, "cpu", "cpu_clock.unit", "MHz"));
    CHECK(!set_option(each, "gpu", "gpu_used.unit", "XB"));
    CHECK(!set_option(each, "gpu", "gpu_name.unit", "GB"));
    CHECK(!set_option(each, "gpu", "gpu_total.percent", "1"));
    v = all_module_values(each, in);
    CHECK(v["gpu_used"] == "8%");
    CHECK(v["gpu_total"] == "12288.0 MB");
    CHECK(v["gpu"].empty());
    CHECK(v["ram_used"] == "6.0 GB");
    CHECK(v["cpu_temp"] == "130\xc2\xb0" "F (55\xc2\xb0" "C)");
    CHECK(v["cpu_clock"] == "4210 MHz");
    CHECK(set_option(each, "units", "temp_unit", "K"));
    v = all_module_values(each, in);
    CHECK(v["cpu_temp"] == "130\xc2\xb0" "F (55\xc2\xb0" "C)");
    CHECK(set_option(each, "cpu", "cpu_temp.unit", "default"));
    v = all_module_values(each, in);
    CHECK(v["cpu_temp"] == "328 K (55\xc2\xb0" "C)");
    const ModuleDef* gpu = find_module("gpu");
    const PlaceholderDef* used = gpu ? find_placeholder(*gpu, "gpu_used") : nullptr;
    CHECK(used && visible_placeholder_settings(each, *gpu, *used).size() == 1);
    CHECK(set_option(each, "gpu", "gpu_used.percent", "0"));
    CHECK(used && visible_placeholder_settings(each, *gpu, *used).size() == 2);
    CHECK(used && placeholder_default(each, *gpu, *used, "unit") == "auto");
    CHECK(option_value(each, "gpu", "gpu_free.unit") == "default");
    CHECK(set_option(each, "vram", "percent", "1"));
    v = all_module_values(each, in);
    CHECK(v["vram_used"] == "8%" && v["vram_total"] == "12.0 GB");

    CHECK(format_offset(-18000, "auto", "named", "UTC") == "UTC-5");
    CHECK(format_offset(19800, "auto", "named", "UTC") == "UTC+5:30");
    CHECK(format_offset(-18037, "auto", "named", "TAI") == "TAI-5:00:37");
    CHECK(format_offset(-18037, "hours", "bare", "TAI") == "-5");
    CHECK(format_offset(19800, "hours", "bare", "UTC") == "+6");
    CHECK(format_offset(-18000, "minutes", "padded", "UTC") == "-05:00");
    CHECK(format_offset(-18000, "seconds", "named_padded", "GMT") == "GMT-05:00:00");
    CHECK(format_offset(0, "auto", "named", "UTC") == "UTC+0");
    ModuleOptions offsets;
    CHECK(set_option(offsets, "time", "offset", "1"));
    CHECK(set_option(offsets, "time", "tai_offset.precision", "seconds"));
    v = all_module_values(offsets, in);
    const long long here = utc_offset_seconds(std::chrono::system_clock::to_time_t(in.now));
    CHECK(v["utc_offset"] == format_offset(here, "auto", "named", "UTC"));
    CHECK(v["gmt_offset"] == format_offset(here, "auto", "named", "GMT"));
    CHECK(v["tai_offset"] == format_offset(here - 37, "seconds", "named", "TAI"));
    CHECK(set_option(offsets, "time", "offset_precision", "minutes"));
    v = all_module_values(offsets, in);
    CHECK(v["utc_offset"] == format_offset(here, "minutes", "named", "UTC"));
    CHECK(v["tai_offset"] == format_offset(here - 37, "seconds", "named", "TAI"));

    const auto enabled = enabled_placeholders(o);
    CHECK(std::find(enabled.begin(), enabled.end(), "ram_app") != enabled.end());
    CHECK(std::find(enabled.begin(), enabled.end(), "ampm") == enabled.end());
    const ModuleDef* time = find_module("time");
    CHECK(time && requirement_text(*time, time->placeholders[4]) == "Turn off \"24-hour clock\"");

    ModuleInputs blank;
    blank.now = in.now;
    v = all_module_values(ModuleOptions{}, blank);
    CHECK(render_status("{time}\nGPU {gpu_temp}", v) == "3:04 PM");
    ModuleOptions dashes;
    set_option(dashes, "units", "unknown", "-");
    v = all_module_values(dashes, blank);
    CHECK(render_status("GPU {gpu_temp}", v) == "GPU -");
    pollen::StatusValues tidy{ { "a", "1" }, { "b", "" }, { "c", "3" } };
    CHECK(render_status("x {a} \xc2\xb7 {b} \xc2\xb7 {c}", tidy) == "x 1 \xc2\xb7 3");
    CHECK(render_status("{a} \xc2\xb7 {b}", tidy) == "1");
    CHECK(render_status("{b} | {a}", tidy) == "1");
    CHECK(render_status("{a} / {b}", tidy) == "1");
    CHECK(render_status("-5 {a} -", tidy) == "-5 1 -");

    CHECK(format_span(59, true, false, "clock") == "0:00");
    CHECK(format_span(90061, false, true, "clock") == "25:01:01");
}

static void test_weather_parsing() {
    using namespace pollen;
    const std::string geo = "{\"results\":[{\"name\":\"Springfield\",\"latitude\":37.2,\"longitude\":-93.3,\"admin1\":\"Missouri\",\"country_code\":\"US\",\"country\":\"United States\"},"
                            "{\"name\":\"Springfield\",\"latitude\":39.8,\"longitude\":-89.6,\"admin1\":\"Illinois\",\"country_code\":\"US\",\"country\":\"United States\"}]}";
    const auto p = parse_geocode(geo, "Springfield, Illinois");
    CHECK(p && p->name == "Springfield, Illinois" && std::fabs(p->latitude - 39.8) < 1e-9);
    const auto first = parse_geocode(geo, "Springfield");
    CHECK(first && first->name == "Springfield, Missouri");
    std::string error;
    CHECK(!parse_geocode("{}", "Nowhere", &error));
    CHECK(error.find("Nowhere") != std::string::npos);
    const std::string fc = "{\"latitude\":41.875,\"current_units\":{\"temperature_2m\":\"\xc2\xb0" "C\"},\"current\":{\"time\":\"2026-10-28T10:00\",\"interval\":900,\"temperature_2m\":12.4,\"apparent_temperature\":10.1,\"relative_humidity_2m\":71,\"weather_code\":61,\"wind_speed_10m\":18.0,\"is_day\":1}}";
    const auto w = parse_forecast(fc, &error);
    CHECK(w && w->ok && w->code == 61 && w->humidity == 71.0);
    CHECK(weather_condition(61) == "Light rain");
    CHECK(!parse_forecast("{\"error\":true,\"reason\":\"Latitude must be in range\"}", &error));
    CHECK(error == "Latitude must be in range");
    CHECK(url_encode("San Jos\xc3\xa9 CA") == "San%20Jos%C3%A9%20CA");

    ModuleOptions o;
    set_option(o, "weather", "wind", "1");
    set_option(o, "weather", "wind_unit", "mph");
    set_option(o, "units", "temp_unit", "F");
    ModuleInputs in;
    in.weather = *w;
    in.weather.place = "Chicago, Illinois";
    StatusValues v;
    module_values(*find_module("weather"), o, in, v);
    CHECK(v["weather_temp"] == "54\xc2\xb0" "F");
    CHECK(v["weather_wind"] == "11 mph");
    CHECK(v.count("weather_place") == 0);

    set_option(o, "units", "temp_also", "C");
    set_option(o, "weather", "wind_also", "kmh");
    module_values(*find_module("weather"), o, in, v);
    CHECK(v["weather_temp"] == "54\xc2\xb0" "F (12\xc2\xb0" "C)");
    CHECK(v["weather_wind"] == "11 mph (18 km/h)");
    set_option(o, "weather", "wind_unit", "ms");
    set_option(o, "weather", "wind_also", "fts");
    set_option(o, "weather", "wind_decimals", "1");
    set_option(o, "units", "both_style", "slash");
    module_values(*find_module("weather"), o, in, v);
    CHECK(v["weather_wind"] == "5.0 m/s / 16.4 ft/s");
    CHECK(format_speed(18.0, "kn", 1) == "9.7 kn");
    CHECK(format_speed(18.0, "kmh", 0) == "18 km/h");
    const ModuleDef* weather = find_module("weather");
    const OptionDef* also = weather ? find_option(*weather, "wind_also") : nullptr;
    CHECK(also && !option_visible(ModuleOptions{}, *weather, *also));
}

static void test_rotation_without_sending() {
    pollen::StatusRotation r;
    r.set_template("a\n---\nb\n---\nc");
    const pollen::StatusValues v;
    const auto base = std::chrono::steady_clock::time_point(std::chrono::seconds(300));
    std::set<std::string> seen;
    for (int i = 0; i < 3; ++i) seen.insert(r.showing(v, base + std::chrono::seconds(3 * i), 3.0, false));
    CHECK(seen.size() == 3);
    const auto now = std::chrono::steady_clock::now();
    CHECK(r.next(v, now) == "a");
    CHECK(r.showing(v, now, 3.0, true) == "a");
    CHECK(r.next(v, now) == "b");
    CHECK(r.showing(v, now + std::chrono::seconds(30), 3.0, true) == "b");
}

static void test_weather_fetch() {
    namespace http = fizmo::networking::http;
    if (!fizmo::networking::background_running()) fizmo::networking::start_background(1);
    std::atomic<int> geocodes{ 0 }, forecasts{ 0 };
    std::string last_url;
    std::mutex url_mutex;
    http::Server server = http::create_server([&](http::IncomingMessage req, http::ServerResponse res) {
        {
            std::lock_guard<std::mutex> lock(url_mutex);
            last_url = req.url();
        }
        if (req.path() == "/search") {
            ++geocodes;
            res.json("{\"results\":[{\"name\":\"Chicago\",\"latitude\":41.85,\"longitude\":-87.65,\"admin1\":\"Illinois\",\"country_code\":\"US\"}]}");
        } else {
            ++forecasts;
            res.json("{\"current\":{\"temperature_2m\":3.5,\"apparent_temperature\":0.2,\"relative_humidity_2m\":80,\"weather_code\":71,\"wind_speed_10m\":20,\"is_day\":0}}");
        }
    });
    std::promise<void> ready;
    server.listen(0, "127.0.0.1", [&ready] { ready.set_value(); });
    if (ready.get_future().wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
        std::printf("  skipped weather fetch: test server didn't start\n");
        return;
    }
    const std::string base = "http://127.0.0.1:" + std::to_string(server.port());
    pollen::Weather weather;
    pollen::WeatherConfig c;
    c.city = "Chicago, IL";
    c.forecast_url = base + "/forecast";
    c.geocode_url = base + "/search";
    weather.configure(c);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    pollen::WeatherSnapshot w;
    while (std::chrono::steady_clock::now() < deadline) {
        w = weather.snapshot();
        if (w.ok || (!w.loading && !w.error.empty())) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!w.ok) std::printf("  weather error: %s\n", w.error.c_str());
    CHECK(w.ok && w.code == 71 && !w.is_day && w.place == "Chicago, Illinois");
    CHECK(geocodes.load() == 1 && forecasts.load() == 1);
    {
        std::lock_guard<std::mutex> lock(url_mutex);
        CHECK(last_url.find("latitude=41.8500") != std::string::npos);
    }
    weather.stop();
    server.close();
}

static void test_vrchat_log() {
    using namespace pollen;
    namespace fs = std::filesystem;
    LocationInfo a = parse_location("wrld_4432ea9b-729c-46e3-8eaf-846aa0a37fdd:12345~hidden(usr_aaa)~region(eu)");
    CHECK(a.valid && a.world_id == "wrld_4432ea9b-729c-46e3-8eaf-846aa0a37fdd" && a.instance == "12345");
    CHECK(a.access == "friends_plus" && a.owner_id == "usr_aaa" && region_label(a.region) == "Europe");
    CHECK(access_label(parse_location("wrld_x:1~private(usr_b)~canRequestInvite~region(us)").access) == "Invite+");
    CHECK(access_label(parse_location("wrld_x:1~private(usr_b)~region(use)").access) == "Invite");
    CHECK(access_label(parse_location("wrld_x:1~friends(usr_b)").access) == "Friends");
    CHECK(access_label(parse_location("wrld_x:1~group(grp_1)~groupAccessType(plus)~region(jp)").access) == "Group+");
    CHECK(access_label(parse_location("wrld_x:1~group(grp_1)~groupAccessType(public)").access) == "Group Public");
    CHECK(access_label(parse_location("wrld_x:Bunny~region(us)").access) == "Public");
    CHECK(region_label(parse_location("wrld_x:1").region) == "US West");
    CHECK(!parse_location("private").valid && parse_location("private").special == "private");
    CHECK(parse_location("traveling:traveling").special == "traveling");
    CHECK(location_label(parse_location("offline")) == "Offline");

    const auto t = parse_log_time("2026.10.28 14:03:12 Debug      -  [Behaviour] OnLeftRoom");
    CHECK(t.has_value());
    auto e = parse_log_line("2026.10.28 14:03:12 Debug      -  [Behaviour] Joining wrld_abc:777~friends(usr_me)~region(eu)\r");
    CHECK(e && e->kind == LogKind::Joining && e->text == "wrld_abc:777~friends(usr_me)~region(eu)" && e->has_time);
    e = parse_log_line("2026.10.28 14:03:13 Debug      -  [Behaviour] Joining or Creating Room: The Black Cat");
    CHECK(e && e->kind == LogKind::Entering && e->text == "The Black Cat");
    e = parse_log_line("2026.10.28 14:03:13 Log        -  [Behaviour] Entering Room: The Black Cat");
    CHECK(e && e->kind == LogKind::Entering && e->text == "The Black Cat");
    e = parse_log_line("2026.10.28 14:03:20 Debug      -  [Behaviour] OnPlayerJoined Some Name (usr_123)");
    CHECK(e && e->kind == LogKind::PlayerJoined && e->text == "Some Name" && e->user_id == "usr_123");
    e = parse_log_line("2026.10.28 14:03:21 Debug      -  [Behaviour] OnPlayerJoined Old Style");
    CHECK(e && e->text == "Old Style" && e->user_id.empty());
    e = parse_log_line("2026.10.28 14:05:00 Debug      -  [Behaviour] OnPlayerLeft Some Name (usr_123)");
    CHECK(e && e->kind == LogKind::PlayerLeft && e->user_id == "usr_123");
    CHECK(!parse_log_line("2026.10.28 14:05:00 Debug      -  [Behaviour] OnPlayerLeftRoom"));
    CHECK(!parse_log_line("2026.10.28 14:05:00 Debug      -  [Network] something else"));

    const fs::path dir = fs::temp_directory_path() / "pollen-vrclog-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const fs::path file = dir / "output_log_2026-10-28_14-00-00.txt";
    {
        std::ofstream out(file);
        out << "2026.10.28 14:03:12 Debug      -  [Behaviour] Joining wrld_abc:777~hidden(usr_me)~region(use)\n";
        out << "2026.10.28 14:03:13 Debug      -  [Behaviour] Joining or Creating Room: The Black Cat\n";
        out << "2026.10.28 14:03:20 Debug      -  [Behaviour] OnPlayerJoined Me (usr_me)\n";
        out << "2026.10.28 14:03:21 Debug      -  [Behaviour] OnPlayerJoined Alex (usr_alex)\n";
    }
    VrchatLog log;
    log.set_folders({ dir });
    CHECK(log.poll().empty());
    CHECK(log.found() && log.world().in_world && log.world().world_name == "The Black Cat");
    CHECK(log.world().players.size() == 2 && log.world().info.access == "friends_plus");
    {
        std::ofstream out(file, std::ios::app);
        out << "2026.10.28 14:04:00 Debug      -  [Behaviour] OnPlayerLeft Alex (usr_alex)\n";
        out << "2026.10.28 14:04:01 Debug      -  [Behaviour] OnPlayerJoined Sam (usr_s";
    }
    auto fresh = log.poll();
    CHECK(fresh.size() == 1 && fresh[0].kind == LogKind::PlayerLeft && log.world().players.size() == 1);
    {
        std::ofstream out(file, std::ios::app);
        out << "am)\n2026.10.28 14:06:00 Debug      -  [Behaviour] OnLeftRoom\n";
    }
    fresh = log.poll();
    CHECK(fresh.size() == 2 && fresh[0].user_id == "usr_sam" && !log.world().in_world && log.world().players.empty());
    fs::remove_all(dir, ec);

    ModuleOptions o;
    ModuleInputs in;
    in.world.in_world = true;
    in.world.world_name = "The Black Cat";
    in.world.info = parse_location("wrld_abc:777~hidden(usr_me)~region(eu)");
    in.world.players = { { "Me", "usr_me", in.now }, { "Alex", "usr_alex", in.now } };
    in.world.entered = in.now - std::chrono::minutes(62);
    StatusValues v;
    module_values(*find_module("world"), o, in, v);
    CHECK(v["world"] == "The Black Cat" && v["world_type"] == "Friends+" && v["players"] == "2" && v["world_time"] == "1h 2m");
    CHECK(v.count("world_region") == 0);
    set_option(o, "world", "region", "1");
    module_values(*find_module("world"), o, in, v);
    CHECK(v["world_region"] == "Europe");
}

static void test_friend_book() {
    using namespace pollen;
    CHECK(base64_encode("user:pa ss") == "dXNlcjpwYSBzcw==");
    CHECK(base64_encode("ab") == "YWI=" && base64_encode("abc") == "YWJj");
    CHECK(form_encode("a b@c") == "a%20b%40c");
    CHECK(status_label("busy") == "Do Not Disturb" && status_color("ask me") == "Orange" && status_label("join me") == "Join Me");

    FriendBook book;
    book.set_world_namer([](const std::string& id) { return id == "wrld_cat" ? std::string("The Black Cat") : std::string(); });
    VrcUser alex;
    alex.id = "usr_alex";
    alex.name = "Alex";
    alex.status = "active";
    alex.state = "online";
    alex.location = "private";
    VrcUser sam;
    sam.id = "usr_sam";
    sam.name = "Sam";
    sam.state = "offline";
    std::vector<SocialEvent> events;
    book.replace_all({ alex, sam }, {}, events);
    CHECK(events.empty() && book.friends().size() == 2);

    VrcUser me;
    me.id = "usr_me";
    me.status = "active";
    CHECK(book.apply_pipeline("{\"type\":\"friend-location\",\"content\":\"{\\\"userId\\\":\\\"usr_alex\\\",\\\"location\\\":\\\"wrld_cat:1~hidden(usr_x)~region(us)\\\",\\\"user\\\":{\\\"displayName\\\":\\\"Alex\\\"}}\"}", me, events));
    CHECK(events.size() == 1 && events[0].kind == "location" && events[0].text == "Alex went to The Black Cat (Friends+)");
    CHECK(book.find("usr_alex")->last_public_location == "wrld_cat:1~hidden(usr_x)~region(us)");
    events.clear();
    book.apply_pipeline("{\"type\":\"friend-location\",\"content\":{\"userId\":\"usr_alex\",\"location\":\"private\"}}", me, events);
    CHECK(events.size() == 1 && events[0].text == "Alex went to a private world");
    CHECK(book.find("usr_alex")->last_public_location == "wrld_cat:1~hidden(usr_x)~region(us)" && book.find("usr_alex")->location_since_known);
    events.clear();
    book.apply_pipeline("{\"type\":\"friend-update\",\"content\":\"{\\\"userId\\\":\\\"usr_alex\\\",\\\"user\\\":{\\\"status\\\":\\\"busy\\\",\\\"statusDescription\\\":\\\"\\\"}}\"}", me, events);
    CHECK(events.size() == 1 && events[0].text == "Alex is now Do Not Disturb");
    events.clear();
    book.apply_pipeline("{\"type\":\"friend-online\",\"content\":{\"userId\":\"usr_sam\",\"location\":\"private\",\"platform\":\"android\",\"user\":{\"displayName\":\"Sam\"}}}", me, events);
    CHECK(events.size() == 1 && events[0].text == "Sam is online" && book.find("usr_sam")->state == "online");
    events.clear();
    book.apply_pipeline("{\"type\":\"friend-offline\",\"content\":{\"userId\":\"usr_sam\",\"platform\":\"\"}}", me, events);
    CHECK(events.size() == 1 && events[0].text == "Sam went offline");
    events.clear();
    book.apply_pipeline("{\"type\":\"friend-delete\",\"content\":{\"userId\":\"usr_sam\"}}", me, events);
    CHECK(events.size() == 1 && events[0].kind == "removed" && !book.is_friend("usr_sam"));
    events.clear();
    book.apply_pipeline("{\"type\":\"user-update\",\"content\":{\"userId\":\"usr_me\",\"user\":{\"status\":\"ask me\",\"statusDescription\":\"\"}}}", me, events);
    CHECK(me.status == "ask me" && events.size() == 1 && events[0].text == "Your status is now Ask Me");
    events.clear();
    book.apply_pipeline("{\"type\":\"notification\",\"content\":{\"type\":\"friendRequest\",\"senderUserId\":\"usr_new\",\"senderUsername\":\"Newbie\"}}", me, events);
    CHECK(events.size() == 1 && events[0].kind == "request");
    CHECK(!book.apply_pipeline("not json", me, events));

    events.clear();
    VrcUser kim;
    kim.id = "usr_kim";
    kim.name = "Kim";
    book.replace_all({ *book.find("usr_alex"), kim }, { { "usr_alex", "Alex" }, { "usr_gone", "Gone" } }, events);
    CHECK(events.size() == 2);
    CHECK(events[0].kind == "removed" && events[0].name == "Gone" && events[1].kind == "added" && events[1].name == "Kim");
    CHECK(book.find("usr_alex")->last_public_location == "wrld_cat:1~hidden(usr_x)~region(us)");

    ModuleOptions o;
    ModuleInputs in;
    StatusValues v;
    module_values(*find_module("friends"), o, in, v);
    CHECK(v["status"].empty() && v["friends_online"].empty());
    in.account.state = LoginState::LoggedIn;
    in.account.me.status = "join me";
    in.friends_online = 4;
    in.friends_here = { "Alex", "Kim", "Sam", "Jo" };
    set_option(o, "friends", "here", "1");
    module_values(*find_module("friends"), o, in, v);
    CHECK(v["status"] == "Join Me" && v["status_color"] == "Blue" && v["friends_online"] == "4");
    CHECK(v["friends_here"] == "4" && v["friends_here_names"] == "Alex, Kim, Sam +1");
}

int main() {
    test_spec_examples();
    test_round_trip();
    test_bundles();
    test_bad_packets();
    test_values();
    test_patterns();
    test_dispatcher();
    test_vrchat();
    test_status();
    test_message_lines();
    test_settings();
    test_loopback();
    test_mdns_messages();
    test_query_server();
    test_query_discovery();
    test_fake_linux_sensors();
    test_modules();
    test_weather_parsing();
    test_vrchat_log();
    test_friend_book();
    test_rotation_without_sending();
    test_weather_fetch();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
