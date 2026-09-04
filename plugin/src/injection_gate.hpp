#pragma once

#include <cstdint>

namespace hyprdictate {

    enum class InjectionGateDecision {
        Wait,
        Inject,
        Drop,
    };

    constexpr std::uint32_t kInjectionPollMs    = 25;
    constexpr std::uint32_t kInjectionTimeoutMs = 2000;

    constexpr InjectionGateDecision injectionGateDecision(
        bool          anyPhysicalInputActive,
        std::uint32_t physicalModifiers,
        bool          timedOut) noexcept {
        // Timeout wins even if input was released just before a delayed timer
        // callback. Once the safety deadline passes, never synthesize keys.
        if (timedOut)
            return InjectionGateDecision::Drop;
        if (!anyPhysicalInputActive && physicalModifiers == 0)
            return InjectionGateDecision::Inject;
        return InjectionGateDecision::Wait;
    }

}
