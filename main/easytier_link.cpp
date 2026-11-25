#include "easytier_link.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>

#include "easytier-esp.hpp"
#include "esp_log.h"

// Symbols produced by EMBED_TXTFILES "../easytier.toml" in main/CMakeLists.txt.
extern "C" {
extern const uint8_t easytier_toml_start[] asm("_binary_easytier_toml_start");
extern const uint8_t easytier_toml_end[] asm("_binary_easytier_toml_end");
}

namespace {

constexpr const char *kTag = "easytier";
constexpr std::string_view kPlaceholder = "REPLACE_ME";

// Only the main task mutates s_inst; the component's control task only writes
// the atomics below through record_event(), so status reads need no lock.
std::optional<et::Inst> s_inst;
std::atomic<uint8_t> s_state{static_cast<uint8_t>(et::State::Uninitialized)};
std::atomic<uint32_t> s_virtual_ipv4{0};
std::atomic<uint8_t> s_prefix_len{24};
std::atomic<uint32_t> s_peer_id{0};
std::atomic<uint32_t> s_direct_peers{0};
std::atomic<uint32_t> s_reachable_routes{0};
std::atomic<int32_t> s_last_error{ESP_OK};
std::atomic<bool> s_started{false};

void cache_status(const et::Status &status) noexcept
{
    s_state.store(static_cast<uint8_t>(status.state), std::memory_order_relaxed);
    s_virtual_ipv4.store(status.virtual_ipv4, std::memory_order_relaxed);
    s_prefix_len.store(status.prefix_len, std::memory_order_relaxed);
    s_peer_id.store(status.peer_id, std::memory_order_relaxed);
    s_direct_peers.store(static_cast<uint32_t>(status.direct_peer_count),
                         std::memory_order_relaxed);
    s_reachable_routes.store(static_cast<uint32_t>(status.reachable_route_count),
                             std::memory_order_relaxed);
    s_last_error.store(static_cast<int32_t>(status.last_error), std::memory_order_relaxed);
}

// Runs in the component's control task: record the snapshot and return.
void record_event(const et::Event &event, void *user_data) noexcept
{
    (void)user_data;
    cache_status(event.status);
}

void format_ipv4(uint32_t address, char out[16]) noexcept
{
    std::snprintf(out, 16, "%u.%u.%u.%u",
                  static_cast<unsigned>((address >> 24) & 0xffU),
                  static_cast<unsigned>((address >> 16) & 0xffU),
                  static_cast<unsigned>((address >> 8) & 0xffU),
                  static_cast<unsigned>(address & 0xffU));
}

std::string_view embedded_toml() noexcept
{
    const uintptr_t begin = reinterpret_cast<uintptr_t>(easytier_toml_start);
    const uintptr_t end = reinterpret_cast<uintptr_t>(easytier_toml_end);
    size_t length = static_cast<size_t>(end - begin);
    // EMBED_TXTFILES appends one NUL byte; the component accepts it, but strip
    // it here so placeholder detection sees the actual text only.
    if (length > 0 && easytier_toml_start[length - 1] == '\0') {
        --length;
    }
    return {reinterpret_cast<const char *>(easytier_toml_start), length};
}

}  // namespace

// Keep the public enum in lockstep with et::State without exposing C++ types.
static_assert(static_cast<int>(et::State::Uninitialized) ==
                  EASYTIER_STATE_UNINITIALIZED,
              "easytier_state_t mapping changed");
static_assert(static_cast<int>(et::State::Stopped) == EASYTIER_STATE_STOPPED,
              "easytier_state_t mapping changed");
static_assert(static_cast<int>(et::State::WaitingUnderlay) ==
                  EASYTIER_STATE_WAITING_UNDERLAY,
              "easytier_state_t mapping changed");
static_assert(static_cast<int>(et::State::Connecting) == EASYTIER_STATE_CONNECTING,
              "easytier_state_t mapping changed");
static_assert(static_cast<int>(et::State::Handshaking) == EASYTIER_STATE_HANDSHAKING,
              "easytier_state_t mapping changed");
static_assert(static_cast<int>(et::State::Syncing) == EASYTIER_STATE_SYNCING,
              "easytier_state_t mapping changed");
static_assert(static_cast<int>(et::State::Ready) == EASYTIER_STATE_READY,
              "easytier_state_t mapping changed");
static_assert(static_cast<int>(et::State::Stopping) == EASYTIER_STATE_STOPPING,
              "easytier_state_t mapping changed");
static_assert(static_cast<int>(et::State::Error) == EASYTIER_STATE_ERROR,
              "easytier_state_t mapping changed");

extern "C" esp_err_t easytier_start(void)
{
    if (s_inst.has_value()) {
        return s_started.load(std::memory_order_relaxed) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    const std::string_view toml = embedded_toml();
    if (toml.find(kPlaceholder) != std::string_view::npos) {
        ESP_LOGE(kTag,
                 "easytier.toml still contains REPLACE_ME placeholders; EasyTier is disabled. "
                 "Set [network_identity] network_name/network_secret and [[peer]] uri "
                 "(tcp://<entry-ip>:11010), then rebuild and flash.");
        return ESP_ERR_INVALID_STATE;
    }

    et::RuntimeOptions options{};
    options.on_event = record_event;
    options.user_data = nullptr;
    s_inst.emplace(toml, options);
    if (!s_inst->valid()) {
        const esp_err_t err = s_inst->last_error();
        s_inst.reset();
        ESP_LOGE(kTag, "EasyTier configuration failed: %s", esp_err_to_name(err));
        return err;
    }

    const esp_err_t err = s_inst->start();
    if (err != ESP_OK) {
        s_inst.reset();
        ESP_LOGE(kTag, "EasyTier start failed: %s", esp_err_to_name(err));
        return err;
    }
    s_started.store(true, std::memory_order_relaxed);

    const et::Settings config = s_inst->get_config_info();
    char ipv4[16];
    format_ipv4(config.static_ipv4, ipv4);
    ESP_LOGI(kTag, "started: instance=%s network=%s ipv4=%s mtu=%u encryption=%u peers=%u",
             config.instance_name.c_str(), config.network_name.c_str(), ipv4,
             static_cast<unsigned>(config.mtu),
             static_cast<unsigned>(config.enable_encryption),
             static_cast<unsigned>(config.peers.size()));

    et::Status status{};
    if (s_inst->get_status(status) == ESP_OK) {
        cache_status(status);
    }
    return ESP_OK;
}

extern "C" esp_err_t easytier_connect(void)
{
    if (!s_inst.has_value() || !s_started.load(std::memory_order_relaxed)) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = s_inst->connect();
    if (err == ESP_OK) {
        ESP_LOGI(kTag, "underlay connect requested");
    } else {
        ESP_LOGW(kTag, "underlay connect request failed: %s", esp_err_to_name(err));
    }
    return err;
}

extern "C" esp_err_t easytier_disconnect(void)
{
    if (!s_inst.has_value() || !s_started.load(std::memory_order_relaxed)) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = s_inst->disconnect();
    if (err == ESP_OK) {
        ESP_LOGI(kTag, "underlay disconnect requested");
    } else {
        ESP_LOGW(kTag, "underlay disconnect request failed: %s", esp_err_to_name(err));
    }
    return err;
}

extern "C" esp_err_t easytier_status(easytier_status_t *out)
{
    if (out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    easytier_status_t snapshot = {};
    snapshot.state = static_cast<easytier_state_t>(s_state.load(std::memory_order_relaxed));
    snapshot.configured = s_inst.has_value();
    snapshot.started = s_started.load(std::memory_order_relaxed);
    snapshot.virtual_ipv4 = s_virtual_ipv4.load(std::memory_order_relaxed);
    snapshot.prefix_len = s_prefix_len.load(std::memory_order_relaxed);
    snapshot.peer_id = s_peer_id.load(std::memory_order_relaxed);
    snapshot.direct_peers = s_direct_peers.load(std::memory_order_relaxed);
    snapshot.reachable_routes = s_reachable_routes.load(std::memory_order_relaxed);
    snapshot.last_error = static_cast<esp_err_t>(s_last_error.load(std::memory_order_relaxed));

    if (s_inst.has_value()) {
        et::Status status{};
        if (s_inst->get_status(status) == ESP_OK) {
            cache_status(status);
            snapshot.state = static_cast<easytier_state_t>(status.state);
            snapshot.virtual_ipv4 = status.virtual_ipv4;
            snapshot.prefix_len = status.prefix_len;
            snapshot.peer_id = status.peer_id;
            snapshot.direct_peers = static_cast<uint32_t>(status.direct_peer_count);
            snapshot.reachable_routes = static_cast<uint32_t>(status.reachable_route_count);
            snapshot.last_error = status.last_error;
            snapshot.dropped_packets = status.dropped_packets;
        }
    }

    *out = snapshot;
    return ESP_OK;
}
