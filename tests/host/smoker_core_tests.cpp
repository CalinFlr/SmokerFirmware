#include "smoker/core/domain.hpp"
#include "smoker/core/heater_demand.hpp"
#include "smoker/core/temperature.hpp"

#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>

namespace {

class TestContext final {
public:
    void expect(const bool condition, const std::string_view description)
    {
        if (condition) {
            return;
        }

        ++failure_count_;
        std::cerr << "FAIL: " << description << '\n';
    }

    [[nodiscard]] int failure_count() const noexcept
    {
        return failure_count_;
    }

private:
    int failure_count_{0};
};

void test_temperature(TestContext& context)
{
    using smoker::core::Temperature;

    const auto below_freezing = Temperature::from_celsius(-20.5F);
    const auto chamber_temperature = Temperature::from_celsius(107.5F);

    context.expect(below_freezing.has_value(), "finite negative Celsius is valid");
    context.expect(chamber_temperature.has_value(), "finite positive Celsius is valid");

    if (below_freezing && chamber_temperature) {
        context.expect(below_freezing->celsius() == -20.5F, "Temperature preserves Celsius");
        context.expect(*below_freezing < *chamber_temperature, "Temperature values are ordered");
    }

    context.expect(
        !Temperature::from_celsius(std::numeric_limits<float>::quiet_NaN()).has_value(),
        "NaN is not a valid Temperature"
    );
    context.expect(
        !Temperature::from_celsius(std::numeric_limits<float>::infinity()).has_value(),
        "positive infinity is not a valid Temperature"
    );
    context.expect(
        !Temperature::from_celsius(-std::numeric_limits<float>::infinity()).has_value(),
        "negative infinity is not a valid Temperature"
    );

    const std::optional<Temperature> absent_reading;
    context.expect(!absent_reading.has_value(), "an absent reading uses std::optional");
}

void test_monotonic_time_types(TestContext& context)
{
    using smoker::core::Duration;
    using smoker::core::MonotonicTimePoint;

    context.expect(
        !std::is_same_v<Duration, MonotonicTimePoint>,
        "duration and monotonic time point are distinct domain types"
    );

    const MonotonicTimePoint started_at{Duration{100}};
    const auto finished_at = started_at + Duration{25};
    context.expect(
        finished_at - started_at == Duration{25},
        "monotonic time-point arithmetic produces a duration"
    );
}

void test_heater_demand(TestContext& context)
{
    using smoker::core::HeaterDemand;

    context.expect(HeaterDemand::off().percent() == 0.0F, "off demand is exactly zero percent");

    const auto minimum = HeaterDemand::from_percent(0.0F);
    const auto midpoint = HeaterDemand::from_percent(42.5F);
    const auto maximum = HeaterDemand::from_percent(100.0F);

    context.expect(minimum.has_value(), "zero percent demand is valid");
    context.expect(midpoint.has_value(), "demand inside the normalized range is valid");
    context.expect(maximum.has_value(), "one hundred percent demand is valid");

    if (midpoint) {
        context.expect(midpoint->percent() == 42.5F, "HeaterDemand preserves a valid percentage");
    }

    context.expect(!HeaterDemand::from_percent(-0.1F).has_value(), "negative demand is rejected");
    context.expect(!HeaterDemand::from_percent(100.1F).has_value(), "demand above 100 is rejected");
    context.expect(
        !HeaterDemand::from_percent(std::numeric_limits<float>::quiet_NaN()).has_value(),
        "NaN demand is rejected"
    );
    context.expect(
        !HeaterDemand::from_percent(std::numeric_limits<float>::infinity()).has_value(),
        "infinite demand is rejected"
    );
}

} // namespace

int main()
{
    TestContext context;

    test_temperature(context);
    test_heater_demand(context);
    test_monotonic_time_types(context);

    if (context.failure_count() != 0) {
        std::cerr << context.failure_count() << " test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All smoker_core domain tests passed\n";
    return 0;
}
