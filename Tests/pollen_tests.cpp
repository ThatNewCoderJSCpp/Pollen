#include <osc/osc.hpp>
#include <pollen/session.hpp>
#include <pollen/status.hpp>

#include <chrono>
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
    const auto found = rd ? mdns_services(*rd, "_oscjson._tcp") : std::vector<Service>{};
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
        for (const Service& s : fake_mdns.services("_oscjson._tcp")) {
            if (s.instance == link.instance()) {
                const HttpResult hi = http_get(s.address, s.port, "/?HOST_INFO");
                const auto parsed = host_info_from_json(hi.body);
                saw_us = parsed && parsed->osc_port == 9124;
            }
        }
    }
    CHECK(saw_us);
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
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
