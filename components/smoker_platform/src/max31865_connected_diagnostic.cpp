#include "smoker/platform/max31865_connected_diagnostic.hpp"

#include "smoker/platform/max31865_board_pins.hpp"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "max31865.h"

#include <cinttypes>
#include <cstdint>
#include <initializer_list>

namespace smoker::platform {
namespace {

constexpr char tag[] = "max31865_diag";
constexpr std::uint32_t diagnostic_clock_hz = 100'000U;
constexpr TickType_t first_conversion_wait = pdMS_TO_TICKS(100U);
constexpr TickType_t sample_interval = pdMS_TO_TICKS(1'000U);
constexpr TickType_t pull_settle_wait = pdMS_TO_TICKS(20U);
constexpr std::uint32_t sample_count = 10U;

// The datasheet permits SPI modes 1 and 3 and requires CPHA=1. This diagnostic
// uses mode 1: idle-low SCLK, shift/change on rising, latch/sample on falling.
constexpr std::uint32_t software_spi_half_period_us = 10U;
constexpr std::uint8_t config_register = 0x00U;
constexpr std::uint8_t write_register_mask = 0x80U;
constexpr std::uint8_t stable_configuration_bits_mask = 0xD1U;
constexpr std::uint8_t three_wire_bit = 0x10U;
constexpr std::uint8_t filter_50_hz_bit = 0x01U;
constexpr std::uint8_t idle_2wire_60hz_pattern = 0x00U;
constexpr std::uint8_t idle_bias_3wire_50hz_pattern = 0x91U;
constexpr std::uint8_t active_auto_3wire_50hz_pattern = 0xD1U;
// Exact terminal value: VBIAS=0, AUTO=0, 1SHOT=0, three-wire,
// fault-detection control=00, fault-clear=0, and 50 Hz filtering.
constexpr std::uint8_t terminal_quiescent_configuration_pattern = 0x11U;

[[nodiscard]] constexpr std::uint64_t gpio_bit(const gpio_num_t gpio) noexcept
{
    return 1ULL << static_cast<unsigned>(gpio);
}

void software_spi_delay() noexcept
{
    esp_rom_delay_us(software_spi_half_period_us);
}

[[nodiscard]] bool set_gpio_level(
    const gpio_num_t gpio,
    const std::uint32_t level,
    const char* const operation
) noexcept
{
    const esp_err_t result = gpio_set_level(gpio, level);
    if (result != ESP_OK) {
        ESP_LOGE(tag, "%s failed for GPIO%d: %s", operation, gpio, esp_err_to_name(result));
        return false;
    }
    return true;
}

void reset_diagnostic_pins() noexcept
{
    for (const gpio_num_t gpio : {
             max31865_chip_select_gpio,
             max31865_mosi_gpio,
             max31865_miso_gpio,
             max31865_sck_gpio,
         }) {
        const esp_err_t result = gpio_reset_pin(gpio);
        if (result != ESP_OK) {
            ESP_LOGE(tag, "GPIO%d release failed: %s", gpio, esp_err_to_name(result));
        }
    }
}

[[nodiscard]] bool software_spi_transfer(
    const std::uint8_t sent,
    std::uint8_t& received
) noexcept
{
    received = 0U;
    for (std::uint8_t bit = 0x80U; bit != 0U; bit >>= 1U) {
        // Mode 1 leading/rising edge: MAX31865 shifts SDO and the master
        // changes SDI. Hold the new bit for a full half-cycle before the
        // trailing/falling latch edge; then sample SDO at that edge.
        if (!set_gpio_level(max31865_sck_gpio, 1U, "software SPI rising edge")
            || !set_gpio_level(
                max31865_mosi_gpio,
                (sent & bit) != 0U ? 1U : 0U,
                "software SPI MOSI"
            )) {
            return false;
        }
        software_spi_delay();
        if (!set_gpio_level(max31865_sck_gpio, 0U, "software SPI falling edge")) {
            return false;
        }
        if (gpio_get_level(max31865_miso_gpio) != 0) {
            received = static_cast<std::uint8_t>(received | bit);
        }
        software_spi_delay();
    }
    return true;
}

[[nodiscard]] bool software_spi_select() noexcept
{
    if (!set_gpio_level(max31865_chip_select_gpio, 0U, "software SPI select")) {
        return false;
    }
    // 10 us is deliberately above the datasheet's 400 ns CS-to-SCLK minimum.
    software_spi_delay();
    return true;
}

[[nodiscard]] bool software_spi_deselect() noexcept
{
    // 10 us is deliberately above the datasheet's 100 ns SCLK-to-CS minimum.
    software_spi_delay();
    if (!set_gpio_level(max31865_chip_select_gpio, 1U, "software SPI deselect")) {
        return false;
    }
    software_spi_delay();
    return true;
}

[[nodiscard]] bool software_spi_force_idle_frame_boundary() noexcept
{
    // An earlier GPIO/transfer failure can leave SCLK high or CS low midway
    // through a byte. Best-effort cleanup first restores mode-1 idle and a
    // CS-high frame boundary so its exact configuration write is not appended
    // to a partial transaction.
    bool succeeded = set_gpio_level(
        max31865_sck_gpio,
        0U,
        "software SPI cleanup idle SCLK"
    );
    software_spi_delay();
    if (!set_gpio_level(
            max31865_chip_select_gpio,
            1U,
            "software SPI cleanup frame boundary"
        )) {
        succeeded = false;
    }
    software_spi_delay();
    return succeeded;
}

[[nodiscard]] bool software_spi_read_config(std::uint8_t& result) noexcept
{
    std::uint8_t ignored = 0U;
    return software_spi_select()
        && software_spi_transfer(config_register, ignored)
        && software_spi_transfer(0U, result)
        && software_spi_deselect();
}

[[nodiscard]] bool software_spi_write_config(const std::uint8_t value) noexcept
{
    std::uint8_t ignored = 0U;
    return software_spi_select()
        && software_spi_transfer(
            static_cast<std::uint8_t>(config_register | write_register_mask),
            ignored
        )
        && software_spi_transfer(value, ignored)
        && software_spi_deselect();
}

[[nodiscard]] bool set_miso_pull(
    const gpio_pull_mode_t pull_mode,
    const char* const label
) noexcept
{
    const esp_err_t result = gpio_set_pull_mode(max31865_miso_gpio, pull_mode);
    if (result != ESP_OK) {
        ESP_LOGE(tag, "%s setup failed: %s", label, esp_err_to_name(result));
        return false;
    }
    vTaskDelay(pull_settle_wait);
    return true;
}

[[nodiscard]] bool software_write_and_verify_config(
    const char* const label,
    const std::uint8_t requested
) noexcept
{
    std::uint8_t observed = 0U;
    if (!software_spi_write_config(requested)
        || !software_spi_read_config(observed)) {
        ESP_LOGE(tag, "%s software-SPI transaction failed", label);
        return false;
    }
    ESP_LOGI(
        tag,
        "%s software-SPI config: requested=0x%02x observed=0x%02x stable=0x%02x",
        label,
        static_cast<unsigned>(requested),
        static_cast<unsigned>(observed),
        static_cast<unsigned>(observed & stable_configuration_bits_mask)
    );
    if ((observed & stable_configuration_bits_mask) != requested) {
        ESP_LOGE(tag, "%s software-SPI stable-bit readback mismatch", label);
        return false;
    }
    return true;
}

[[nodiscard]] bool software_write_and_verify_exact_config(
    const char* const label,
    const std::uint8_t requested
) noexcept
{
    std::uint8_t observed = 0U;
    if (!software_spi_write_config(requested)
        || !software_spi_read_config(observed)) {
        ESP_LOGE(tag, "%s software-SPI transaction failed", label);
        return false;
    }
    ESP_LOGI(
        tag,
        "%s software-SPI exact config: requested=0x%02x observed=0x%02x",
        label,
        static_cast<unsigned>(requested),
        static_cast<unsigned>(observed)
    );
    if (observed != requested) {
        ESP_LOGE(tag, "%s software-SPI exact readback mismatch", label);
        return false;
    }
    return true;
}

[[nodiscard]] bool quiesce_software_spi_converter(
    const char* const label
) noexcept
{
    const bool idle_boundary_ready = software_spi_force_idle_frame_boundary();
    if (!idle_boundary_ready) {
        ESP_LOGE(tag, "%s could not restore a clean software-SPI frame boundary", label);
    }

    std::uint8_t current = 0U;
    const bool current_read = software_spi_read_config(current);
    if (!current_read) {
        ESP_LOGW(
            tag,
            "%s could not read current filter; attempting fixed terminal quiescence",
            label
        );
    }

    // When the current register is readable, first leave AUTO without changing
    // its notch frequency. The exact write also forces every command bit to 0,
    // unlike driver 1.0.8's persistent-field read-modify-write setter.
    const std::uint8_t first_quiescent = current_read
        ? static_cast<std::uint8_t>(
            three_wire_bit | (current & filter_50_hz_bit)
        )
        : terminal_quiescent_configuration_pattern;
    if (!software_write_and_verify_exact_config(label, first_quiescent)) {
        return false;
    }
    if (first_quiescent == terminal_quiescent_configuration_pattern) {
        return idle_boundary_ready;
    }

    // AUTO and VBIAS are now verified off, so the datasheet permits changing
    // the notch selection to the diagnostic's fixed terminal 50 Hz setting.
    const bool terminal_verified = software_write_and_verify_exact_config(
        "software-SPI terminal quiescence",
        terminal_quiescent_configuration_pattern
    );
    return idle_boundary_ready && terminal_verified;
}

class SoftwareSpiPinsOwner final {
public:
    SoftwareSpiPinsOwner() = default;
    SoftwareSpiPinsOwner(const SoftwareSpiPinsOwner&) = delete;
    SoftwareSpiPinsOwner& operator=(const SoftwareSpiPinsOwner&) = delete;

    ~SoftwareSpiPinsOwner()
    {
        if (transactions_possible_ && !quiesced_) {
            ESP_LOGW(tag, "software-SPI destructor fallback quiescence");
            if (!quiesce_software_spi_converter(
                    "software-SPI destructor fallback"
                )) {
                ESP_LOGE(tag, "software-SPI fallback quiescence FAILED");
            }
        }
        reset_diagnostic_pins();
    }

    void arm() noexcept
    {
        transactions_possible_ = true;
    }

    [[nodiscard]] bool quiesce_checked() noexcept
    {
        if (quiesced_) return true;
        if (!transactions_possible_) return false;
        quiesced_ = quiesce_software_spi_converter(
            "software-SPI checked quiescence"
        );
        return quiesced_;
    }

private:
    bool transactions_possible_{false};
    bool quiesced_{false};
};

[[nodiscard]] bool run_software_spi_register_response_test() noexcept
{
    SoftwareSpiPinsOwner pins;
    gpio_config_t outputs{};
    outputs.pin_bit_mask = gpio_bit(max31865_chip_select_gpio)
        | gpio_bit(max31865_mosi_gpio)
        | gpio_bit(max31865_sck_gpio);
    outputs.mode = GPIO_MODE_OUTPUT;
    outputs.pull_up_en = GPIO_PULLUP_DISABLE;
    outputs.pull_down_en = GPIO_PULLDOWN_DISABLE;
    outputs.intr_type = GPIO_INTR_DISABLE;
    const esp_err_t output_result = gpio_config(&outputs);
    if (output_result != ESP_OK) {
        ESP_LOGE(tag, "software SPI output setup failed: %s", esp_err_to_name(output_result));
        return false;
    }

    gpio_config_t input{};
    input.pin_bit_mask = gpio_bit(max31865_miso_gpio);
    input.mode = GPIO_MODE_INPUT;
    input.pull_up_en = GPIO_PULLUP_ENABLE;
    input.pull_down_en = GPIO_PULLDOWN_DISABLE;
    input.intr_type = GPIO_INTR_DISABLE;
    const esp_err_t input_result = gpio_config(&input);
    if (input_result != ESP_OK) {
        ESP_LOGE(tag, "software SPI input setup failed: %s", esp_err_to_name(input_result));
        return false;
    }

    if (!set_gpio_level(max31865_chip_select_gpio, 1U, "software SPI idle CS")
        || !set_gpio_level(max31865_sck_gpio, 0U, "software SPI idle SCLK")
        || !set_gpio_level(max31865_mosi_gpio, 0U, "software SPI idle MOSI")) {
        return false;
    }
    software_spi_delay();
    pins.arm();

    std::uint8_t pull_up_observed = 0U;
    std::uint8_t pull_down_observed = 0U;
    const bool pull_reads_ok = set_miso_pull(
        GPIO_PULLUP_ONLY,
        "software SPI MISO pull-up"
    )
        && software_spi_read_config(pull_up_observed)
        && set_miso_pull(GPIO_PULLDOWN_ONLY, "software SPI MISO pull-down")
        && software_spi_read_config(pull_down_observed);
    ESP_LOGI(
        tag,
        "software-SPI config with MISO pulls: up=0x%02x down=0x%02x",
        static_cast<unsigned>(pull_up_observed),
        static_cast<unsigned>(pull_down_observed)
    );
    const std::uint8_t pull_up_stable = static_cast<std::uint8_t>(
        pull_up_observed & stable_configuration_bits_mask
    );
    const std::uint8_t pull_down_stable = static_cast<std::uint8_t>(
        pull_down_observed & stable_configuration_bits_mask
    );
    if (!pull_reads_ok || pull_up_stable != pull_down_stable) {
        ESP_LOGE(
            tag,
            "software-SPI MISO is not driven consistently; GPIO%d may be floating",
            max31865_miso_gpio
        );
        return false;
    }
    if (!set_miso_pull(GPIO_FLOATING, "software SPI MISO floating")) {
        return false;
    }

    // First leave any previously active automatic conversion without changing
    // its filter. Subsequent patterns touch only defined persistent bits
    // D7/D6/D4/D0. D5, D3:D2, and D1 are command/self-clearing fields and are
    // deliberately written as zero and excluded from persistent-bit pattern
    // comparisons; terminal quiescence later requires an exact-byte readback.
    const std::uint8_t quiescent = static_cast<std::uint8_t>(
        three_wire_bit | (pull_down_stable & filter_50_hz_bit)
    );
    const bool passed = software_write_and_verify_exact_config(
            "initial quiescence",
            quiescent
        )
        && software_write_and_verify_config("pattern A", idle_2wire_60hz_pattern)
        && software_write_and_verify_config(
            "pattern B",
            idle_bias_3wire_50hz_pattern
        )
        && software_write_and_verify_config(
            "active sampling configuration",
            active_auto_3wire_50hz_pattern
        );
    if (!passed) {
        ESP_LOGE(tag, "software-SPI register-response test FAILED");
        return false;
    }
    if (!pins.quiesce_checked()) {
        ESP_LOGE(tag, "software-SPI checked terminal quiescence FAILED");
        return false;
    }
    ESP_LOGI(tag, "software-SPI register-response test PASS");
    return true;
}

class SpiBusOwner final {
public:
    SpiBusOwner() = default;
    SpiBusOwner(const SpiBusOwner&) = delete;
    SpiBusOwner& operator=(const SpiBusOwner&) = delete;

    ~SpiBusOwner()
    {
        if (initialized_) {
            const esp_err_t result = spi_bus_free(max31865_spi_host);
            if (result != ESP_OK) {
                ESP_LOGE(tag, "SPI2 release failed: %s", esp_err_to_name(result));
            }
        }
        reset_diagnostic_pins();
    }

    [[nodiscard]] bool initialize() noexcept
    {
        spi_bus_config_t configuration{};
        configuration.mosi_io_num = max31865_mosi_gpio;
        configuration.miso_io_num = max31865_miso_gpio;
        configuration.sclk_io_num = max31865_sck_gpio;
        configuration.quadwp_io_num = -1;
        configuration.quadhd_io_num = -1;
        configuration.max_transfer_sz = 3;
        const esp_err_t result = spi_bus_initialize(
            max31865_spi_host,
            &configuration,
            SPI_DMA_DISABLED
        );
        if (result != ESP_OK) {
            ESP_LOGE(tag, "SPI2 initialization failed: %s", esp_err_to_name(result));
            return false;
        }
        initialized_ = true;
        return true;
    }

private:
    bool initialized_{false};
};

[[nodiscard]] esp_err_t driver_write_exact_config(
    max31865_t& device,
    const std::uint8_t value
) noexcept
{
    spi_transaction_t transaction{};
    const std::uint8_t transmit[]{
        static_cast<std::uint8_t>(config_register | write_register_mask),
        value,
    };
    transaction.tx_buffer = transmit;
    transaction.length = sizeof(transmit) * 8U;
    return spi_device_transmit(device.spi_dev, &transaction);
}

[[nodiscard]] esp_err_t driver_read_exact_config(
    max31865_t& device,
    std::uint8_t& value
) noexcept
{
    spi_transaction_t transaction{};
    const std::uint8_t transmit[]{config_register, 0U};
    std::uint8_t receive[sizeof(transmit)]{};
    transaction.tx_buffer = transmit;
    transaction.rx_buffer = receive;
    transaction.length = sizeof(transmit) * 8U;
    const esp_err_t result = spi_device_transmit(
        device.spi_dev,
        &transaction
    );
    if (result == ESP_OK) value = receive[1];
    return result;
}

[[nodiscard]] bool driver_write_and_verify_exact_config(
    max31865_t& device,
    const char* const label,
    const std::uint8_t requested
) noexcept
{
    const esp_err_t write_result = driver_write_exact_config(device, requested);
    if (write_result != ESP_OK) {
        ESP_LOGE(
            tag,
            "%s exact write failed: %s",
            label,
            esp_err_to_name(write_result)
        );
        return false;
    }

    std::uint8_t observed = 0U;
    const esp_err_t read_result = driver_read_exact_config(device, observed);
    if (read_result != ESP_OK) {
        ESP_LOGE(
            tag,
            "%s exact readback failed: %s",
            label,
            esp_err_to_name(read_result)
        );
        return false;
    }
    ESP_LOGI(
        tag,
        "%s exact config: requested=0x%02x observed=0x%02x",
        label,
        static_cast<unsigned>(requested),
        static_cast<unsigned>(observed)
    );
    if (observed != requested) {
        ESP_LOGE(tag, "%s exact configuration mismatch", label);
        return false;
    }
    return true;
}

[[nodiscard]] bool quiesce_driver_converter(
    max31865_t& device,
    const char* const label
) noexcept
{
    std::uint8_t current = 0U;
    const esp_err_t current_result = driver_read_exact_config(device, current);
    if (current_result != ESP_OK) {
        ESP_LOGW(
            tag,
            "%s current-config read failed (%s); attempting fixed terminal quiescence",
            label,
            esp_err_to_name(current_result)
        );
    }

    // Preserve the current notch in the first exact write while disabling
    // AUTO/VBIAS and zeroing 1SHOT, fault-cycle, and fault-clear commands.
    // This avoids driver 1.0.8's max31865_set_config() RMW preservation of
    // D5, D3:D2, and D1 and honors the datasheet's auto/filter ordering.
    const std::uint8_t first_quiescent = current_result == ESP_OK
        ? static_cast<std::uint8_t>(
            three_wire_bit | (current & filter_50_hz_bit)
        )
        : terminal_quiescent_configuration_pattern;
    if (!driver_write_and_verify_exact_config(
            device,
            label,
            first_quiescent
        )) {
        return false;
    }
    if (first_quiescent == terminal_quiescent_configuration_pattern) {
        return true;
    }

    return driver_write_and_verify_exact_config(
        device,
        "driver terminal quiescence",
        terminal_quiescent_configuration_pattern
    );
}

class Max31865DeviceOwner final {
public:
    Max31865DeviceOwner() = default;
    Max31865DeviceOwner(const Max31865DeviceOwner&) = delete;
    Max31865DeviceOwner& operator=(const Max31865DeviceOwner&) = delete;

    ~Max31865DeviceOwner()
    {
        if (!initialized_) return;
        if (!quiesced_) {
            ESP_LOGW(tag, "MAX31865 descriptor destructor fallback quiescence");
            if (!quiesce_driver_converter(
                    device_,
                    "driver destructor fallback"
                )) {
                ESP_LOGE(tag, "MAX31865 fallback quiescence FAILED");
            } else {
                quiesced_ = true;
            }
        }
        const esp_err_t result = max31865_free_desc(&device_);
        if (result != ESP_OK) {
            ESP_LOGE(tag, "MAX31865 descriptor release failed: %s", esp_err_to_name(result));
        }
        initialized_ = false;
    }

    [[nodiscard]] bool initialize() noexcept
    {
        // Temperature conversion fields intentionally remain zero: fitted Rref
        // is unconfirmed and this diagnostic never calls a temperature API.
        const esp_err_t result = max31865_init_desc(
            &device_,
            max31865_spi_host,
            diagnostic_clock_hz,
            max31865_chip_select_gpio
        );
        if (result != ESP_OK) {
            ESP_LOGE(
                tag,
                "MAX31865 descriptor initialization failed: %s",
                esp_err_to_name(result)
            );
            return false;
        }
        initialized_ = true;
        return true;
    }

    [[nodiscard]] max31865_t& get() noexcept
    {
        return device_;
    }

    [[nodiscard]] bool quiesce_checked() noexcept
    {
        if (quiesced_) return true;
        if (!initialized_) return false;
        quiesced_ = quiesce_driver_converter(
            device_,
            "driver checked quiescence"
        );
        return quiesced_;
    }

private:
    max31865_t device_{};
    bool initialized_{false};
    bool quiesced_{false};
};

[[nodiscard]] bool same_configuration(
    const max31865_config_t& left,
    const max31865_config_t& right
) noexcept
{
    return left.mode == right.mode
        && left.connection == right.connection
        && left.v_bias == right.v_bias
        && left.filter == right.filter;
}

void log_configuration(
    const char* const label,
    const max31865_config_t& configuration
) noexcept
{
    ESP_LOGI(
        tag,
        "%s: mode=%s wires=%s bias=%s filter=%s",
        label,
        configuration.mode == MAX31865_MODE_AUTO ? "auto" : "normally-off",
        configuration.connection == MAX31865_3WIRE ? "3" : "2/4",
        configuration.v_bias ? "on" : "off",
        configuration.filter == MAX31865_FILTER_50HZ ? "50Hz" : "60Hz"
    );
}

[[nodiscard]] bool write_and_verify_configuration(
    max31865_t& device,
    const char* const label,
    const max31865_config_t& requested
) noexcept
{
    const esp_err_t write_result = max31865_set_config(&device, &requested);
    if (write_result != ESP_OK) {
        ESP_LOGE(tag, "%s write failed: %s", label, esp_err_to_name(write_result));
        return false;
    }

    max31865_config_t observed{};
    const esp_err_t read_result = max31865_get_config(&device, &observed);
    if (read_result != ESP_OK) {
        ESP_LOGE(tag, "%s readback failed: %s", label, esp_err_to_name(read_result));
        return false;
    }
    log_configuration(label, observed);
    if (!same_configuration(requested, observed)) {
        ESP_LOGE(tag, "%s persistent-field readback mismatch", label);
        return false;
    }
    return true;
}

[[nodiscard]] bool read_configuration_with_pull(
    max31865_t& device,
    const gpio_pull_mode_t pull_mode,
    const char* const label,
    max31865_config_t& observed
) noexcept
{
    if (!set_miso_pull(pull_mode, label)) return false;
    const esp_err_t read_result = max31865_get_config(&device, &observed);
    if (read_result != ESP_OK) {
        ESP_LOGE(tag, "%s read failed: %s", label, esp_err_to_name(read_result));
        return false;
    }
    log_configuration(label, observed);
    return true;
}

[[nodiscard]] bool clear_fault_status(max31865_t& device) noexcept
{
    const esp_err_t result = max31865_clear_fault_status(&device);
    if (result != ESP_OK) {
        ESP_LOGE(tag, "fault-status clear failed: %s", esp_err_to_name(result));
        return false;
    }
    return true;
}

[[nodiscard]] bool run_driver_register_response_test(max31865_t& device) noexcept
{
    max31865_config_t pull_up_observed{};
    max31865_config_t pull_down_observed{};
    if (!read_configuration_with_pull(
            device,
            GPIO_PULLUP_ONLY,
            "driver MISO pull-up",
            pull_up_observed
        )
        || !read_configuration_with_pull(
            device,
            GPIO_PULLDOWN_ONLY,
            "driver MISO pull-down",
            pull_down_observed
        )) {
        return false;
    }
    if (!same_configuration(pull_up_observed, pull_down_observed)) {
        ESP_LOGE(
            tag,
            "driver MISO follows internal pulls; GPIO%d/SDO may be floating",
            max31865_miso_gpio
        );
        return false;
    }
    if (!set_miso_pull(GPIO_FLOATING, "driver MISO floating")) return false;

    const max31865_config_t pattern_a{
        .mode = MAX31865_MODE_SINGLE,
        .connection = MAX31865_2WIRE,
        .v_bias = false,
        .filter = MAX31865_FILTER_60HZ,
    };
    const max31865_config_t pattern_b{
        .mode = MAX31865_MODE_SINGLE,
        .connection = MAX31865_3WIRE,
        .v_bias = true,
        .filter = MAX31865_FILTER_50HZ,
    };
    const max31865_config_t final_configuration{
        .mode = MAX31865_MODE_AUTO,
        .connection = MAX31865_3WIRE,
        .v_bias = true,
        .filter = MAX31865_FILTER_50HZ,
    };

    const std::uint8_t initial_quiescent = static_cast<std::uint8_t>(
        three_wire_bit
        | (pull_down_observed.filter == MAX31865_FILTER_50HZ
            ? filter_50_hz_bit
            : 0U)
    );
    if (!driver_write_and_verify_exact_config(
            device,
            "driver initial quiescence",
            initial_quiescent
        )
        || !clear_fault_status(device)
        || !write_and_verify_configuration(device, "pattern A", pattern_a)
        || !write_and_verify_configuration(device, "pattern B", pattern_b)
        || !write_and_verify_configuration(device, "final", final_configuration)) {
        ESP_LOGE(tag, "driver register-response test FAILED");
        return false;
    }
    ESP_LOGI(tag, "driver register-response test PASS");
    return true;
}

void log_fault_bits(const std::uint8_t fault_status) noexcept
{
    ESP_LOGI(
        tag,
        "fault bits: high=%u low=%u refin_high=%u refin_low=%u rtdin_low=%u over_under=%u",
        static_cast<unsigned>((fault_status >> 7U) & 1U),
        static_cast<unsigned>((fault_status >> 6U) & 1U),
        static_cast<unsigned>((fault_status >> 5U) & 1U),
        static_cast<unsigned>((fault_status >> 4U) & 1U),
        static_cast<unsigned>((fault_status >> 3U) & 1U),
        static_cast<unsigned>((fault_status >> 2U) & 1U)
    );
}

} // namespace

bool run_max31865_connected_diagnostic() noexcept
{
    ESP_LOGW(tag, "DIAGNOSTIC IMAGE: application/control runtime and heater output are absent");
    ESP_LOGW(tag, "No temperature is calculated because fitted Rref is unconfirmed");
    ESP_LOGI(
        tag,
        "production wiring: SPI2 SCK=GPIO%d MOSI=GPIO%d MISO=GPIO%d CS=GPIO%d diagnostic_clock=%" PRIu32 "Hz",
        max31865_sck_gpio,
        max31865_mosi_gpio,
        max31865_miso_gpio,
        max31865_chip_select_gpio,
        diagnostic_clock_hz
    );

    ESP_LOGI(tag, "starting bounded software-SPI mode-1 register-response test");
    if (!run_software_spi_register_response_test()) return false;

    SpiBusOwner bus;
    if (!bus.initialize()) return false;

    Max31865DeviceOwner device_owner;
    if (!device_owner.initialize()) return false;
    max31865_t& device = device_owner.get();
    if (!run_driver_register_response_test(device)) return false;

    // The final 50 Hz continuous configuration needs at most 66 ms for its
    // first conversion. The bounded diagnostic waits 100 ms before reading.
    vTaskDelay(first_conversion_wait);

    std::uint32_t transaction_errors = 0U;
    std::uint32_t samples_with_fault = 0U;
    for (std::uint32_t sample = 1U; sample <= sample_count; ++sample) {
        std::uint16_t raw = 0U;
        bool raw_fault = false;
        std::uint8_t fault_status = 0U;
        const esp_err_t raw_result = max31865_read_raw(&device, &raw, &raw_fault);
        const esp_err_t fault_result = max31865_get_fault_status(&device, &fault_status);
        if (raw_result != ESP_OK || fault_result != ESP_OK) {
            ++transaction_errors;
            ESP_LOGE(
                tag,
                "sample=%" PRIu32 " transaction error raw=%s fault=%s",
                sample,
                esp_err_to_name(raw_result),
                esp_err_to_name(fault_result)
            );
        } else {
            const float reference_ratio = static_cast<float>(raw) / 32768.0F;
            ESP_LOGI(
                tag,
                "sample=%" PRIu32 " raw_rtd_code=%u rtd_to_reference_ratio=%.6f raw_fault=%s fault_status=0x%02x",
                sample,
                static_cast<unsigned>(raw),
                static_cast<double>(reference_ratio),
                raw_fault ? "yes" : "no",
                static_cast<unsigned>(fault_status)
            );
            if (raw_fault || fault_status != 0U) {
                ++samples_with_fault;
                log_fault_bits(fault_status);
            }
        }
        if (sample != sample_count) vTaskDelay(sample_interval);
    }

    ESP_LOGW(
        tag,
        "sampling complete: samples=%" PRIu32 " transaction_errors=%" PRIu32 " sensor_fault_samples=%" PRIu32,
        sample_count,
        transaction_errors,
        samples_with_fault
    );
    ESP_LOGW(
        tag,
        "sensor fault samples are distinct from SPI transaction or shutdown failure"
    );

    const bool shutdown_ok = device_owner.quiesce_checked();
    if (!shutdown_ok) {
        ESP_LOGE(tag, "checked MAX31865 terminal quiescence FAILED");
    }
    ESP_LOGW(
        tag,
        "bounded diagnostic complete: transaction_path=%s shutdown=%s sensor_fault_samples=%" PRIu32,
        transaction_errors == 0U ? "pass" : "fail",
        shutdown_ok ? "pass" : "fail",
        samples_with_fault
    );
    ESP_LOGW(tag, "These observations do not prove wiring, power, RTD health, accuracy, or electrical safety");
    return transaction_errors == 0U && shutdown_ok;
}

} // namespace smoker::platform
