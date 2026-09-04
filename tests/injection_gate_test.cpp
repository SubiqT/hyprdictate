#include <iostream>

#include "injection_gate.hpp"

namespace {

    bool require(bool condition, const char* message) {
        if (!condition)
            std::cerr << "FAIL: " << message << '\n';
        return condition;
    }

}

int main() {
    using namespace hyprdictate;

    if (!require(injectionGateDecision(false, 0, false) ==
                     InjectionGateDecision::Inject,
                 "released input before deadline injects"))
        return 1;

    if (!require(injectionGateDecision(true, 0, false) ==
                     InjectionGateDecision::Wait,
                 "active physical key or pointer button waits"))
        return 1;

    if (!require(injectionGateDecision(false, 1u << 6, false) ==
                     InjectionGateDecision::Wait,
                 "held Super modifier waits"))
        return 1;

    if (!require(injectionGateDecision(true, 1u << 6, true) ==
                     InjectionGateDecision::Drop,
                 "active input drops after deadline"))
        return 1;

    if (!require(injectionGateDecision(false, 0, true) ==
                     InjectionGateDecision::Drop,
                 "late callback drops even when input has since released"))
        return 1;

    return 0;
}
