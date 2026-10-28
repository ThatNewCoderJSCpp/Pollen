#ifndef POLLEN_VRCHAT_API_HPP
#define POLLEN_VRCHAT_API_HPP

#include "vrchat_log.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace pollen {

struct VrcUser {
    std::string id;
    std::string name;
    std::string status;
    std::string status_text;
    std::string location;
    std::string platform;
    std::string state;
    std::string last_login;
    std::string last_public_location;
    std::chrono::system_clock::time_point location_since;
    bool location_since_known = false;
};

struct SocialEvent {
    std::chrono::system_clock::time_point time;
    std::string kind;
    std::string user_id;
    std::string name;
    std::string text;
    std::string location;
};

std::string status_label(const std::string& status);
std::string status_color(const std::string& status);
std::string status_icon(const std::string& status);
std::string base64_encode(const std::string& data);
std::string form_encode(const std::string& text);
std::optional<VrcUser> parse_vrc_user(const std::string& json);

class FriendBook {
public:
    using WorldNamer = std::function<std::string(const std::string& world_id)>;

    void set_world_namer(WorldNamer namer) { m_world_name = std::move(namer); }
    const std::map<std::string, VrcUser>& friends() const noexcept { return m_friends; }
    const VrcUser* find(const std::string& id) const;
    bool is_friend(const std::string& id) const { return m_friends.count(id) != 0; }
    void clear() { m_friends.clear(); }

    void replace_all(const std::vector<VrcUser>& list, const std::map<std::string, std::string>& known_before, std::vector<SocialEvent>& events);
    bool apply_pipeline(const std::string& message, VrcUser& me, std::vector<SocialEvent>& events);
    void forget(const std::string& id) { m_friends.erase(id); }
    void upsert(const VrcUser& user) { m_friends[user.id] = user; }
    std::map<std::string, std::string> names() const;

    std::string describe_location(const std::string& location) const;

private:
    void set_location(VrcUser& f, const std::string& location, std::chrono::system_clock::time_point now);

    std::map<std::string, VrcUser> m_friends;
    WorldNamer m_world_name;
};

enum class LoginState { LoggedOut, Working, NeedsCode, WaitingForApproval, LoggedIn };

struct AccountSnapshot {
    LoginState state = LoginState::LoggedOut;
    std::string message;
    std::string code_kind;
    std::string device_code;
    std::string device_url;
    VrcUser me;
    bool pipeline = false;
    std::string action_note;
    std::uint64_t friends_revision = 0;
    std::uint64_t collection_revision = 0;
};

struct CollectionRow {
    std::string kind;
    std::string id;
    std::string target_id;
    std::string name;
    std::string detail;
    std::string group;
    bool archived = false;
    bool custom = false;
};

struct FavoriteGroup {
    std::string type;
    std::string name;
    std::string display_name;
};

class VrchatApi {
public:
    VrchatApi();
    ~VrchatApi();

    VrchatApi(const VrchatApi&) = delete;
    VrchatApi& operator=(const VrchatApi&) = delete;

    void set_endpoints(const std::string& api_base, const std::string& pipeline_url);
    void start(const std::filesystem::path& folder, const std::string& user_agent);
    void stop();

    void login(const std::string& username, const std::string& password);
    void verify(const std::string& code);
    void login_with_code();
    void cancel_login();
    void logout();
    void refresh_friends();

    void load_collection(const std::string& kind);
    void act(const std::vector<std::string>& args);

    AccountSnapshot account() const;
    std::vector<VrcUser> friends() const;
    bool is_friend(const std::string& id) const;
    std::vector<CollectionRow> collection(const std::string& kind) const;
    std::string collection_status(const std::string& kind) const;
    std::vector<FavoriteGroup> favorite_groups(const std::string& type) const;
    std::vector<SocialEvent> take_events();
    std::string world_name(const std::string& world_id);
    std::string describe_location(const std::string& location) const;

    static std::filesystem::path session_file(const std::filesystem::path& folder);

private:
    struct Reply {
        int status = -1;
        std::string body;
        std::string error;
        std::vector<std::string> cookies;
    };

    Reply request(const std::string& method, const std::string& path, const std::string& body = std::string(), const std::string& authorization = std::string());
    void run();
    void post(std::function<void()> job);
    void take_cookies(const std::vector<std::string>& cookies);
    void save_session();
    void load_session();
    void save_friend_names();
    std::map<std::string, std::string> load_friend_names();
    bool fetch_me(bool quiet);
    void logged_in(const std::string& user_json);
    void set_state(LoginState state, const std::string& message);
    void note(const std::string& text);
    void fetch_friends();
    void fetch_collection(const std::string& kind);
    void fetch_groups();
    void fetch_world(const std::string& world_id);
    void open_pipeline();
    void close_pipeline();
    void on_pipeline(const std::string& text);
    void poll_device_code();
    void do_action(const std::vector<std::string>& args);
    void push_events(std::vector<SocialEvent>& events);
    void release_waiting(bool all);

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    std::deque<std::function<void()>> m_jobs;
    std::filesystem::path m_folder;
    std::string m_user_agent;
    std::string m_api_base = "https://api.vrchat.cloud/api/1";
    std::string m_pipeline_url = "wss://pipeline.vrchat.cloud/";
    std::string m_auth;
    std::string m_two_factor;
    AccountSnapshot m_account;
    FriendBook m_book;
    std::map<std::string, std::string> m_world_names;
    std::vector<std::string> m_world_queue;
    std::map<std::string, std::vector<CollectionRow>> m_collections;
    std::map<std::string, std::string> m_collection_status;
    std::vector<FavoriteGroup> m_groups;
    std::deque<SocialEvent> m_events;
    std::vector<std::string> m_code_methods;
    std::string m_device_secret;
    std::chrono::steady_clock::time_point m_device_next;
    std::chrono::steady_clock::time_point m_device_expires;
    int m_device_interval = 5;
    std::chrono::steady_clock::time_point m_last_request;
    std::chrono::steady_clock::time_point m_backoff_until;
    std::chrono::steady_clock::time_point m_next_friends;
    std::chrono::steady_clock::time_point m_pipeline_retry;
    std::string m_removing;
    bool m_friends_loaded = false;
    std::vector<std::pair<std::chrono::steady_clock::time_point, SocialEvent>> m_waiting;
    struct Pipeline;
    std::shared_ptr<Pipeline> m_pipeline;
};

} // namespace pollen

#endif // POLLEN_VRCHAT_API_HPP
