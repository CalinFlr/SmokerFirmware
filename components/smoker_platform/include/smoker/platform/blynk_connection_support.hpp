#pragma once

#include <atomic>
#include <cstdint>

namespace smoker::platform {

struct BlynkConnectionSnapshot final {
    std::uint32_t connection_generation{0U};
    std::uint32_t disconnect_generation{0U};
    bool connected{false};
    bool cleanup_required{false};
    bool connection_started{false};
};

// One ESP-MQTT callback producer and one BlynkTask consumer. Disconnect and
// connect generations are independent so a disconnect followed by reconnect
// between consumer polls cannot collapse into a connected-only observation.
class BlynkConnectionBoundary final {
public:
    [[nodiscard]] std::uint32_t callback_connected() noexcept;
    void callback_disconnected() noexcept;

    [[nodiscard]] std::uint32_t callback_connection_generation() const noexcept;
    [[nodiscard]] BlynkConnectionSnapshot poll() noexcept;
    [[nodiscard]] bool usable(const BlynkConnectionSnapshot& snapshot) const noexcept;
    [[nodiscard]] bool accepts(std::uint32_t connection_generation) const noexcept;

private:
    alignas(64) std::atomic<std::uint32_t> connection_generation_{0U};
    alignas(64) std::atomic<std::uint32_t> disconnect_generation_{0U};
    std::atomic_bool connected_{false};
    std::uint32_t observed_connection_generation_{0U};
    std::uint32_t observed_disconnect_generation_{0U};
};

} // namespace smoker::platform
