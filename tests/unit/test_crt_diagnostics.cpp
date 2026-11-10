#include "test_crt_diagnostics.h"

#include <cstdio>
#include <exception>

namespace {

[[noreturn]] void report_unhandled_test_exception() noexcept {
    if (const auto exception = std::current_exception()) {
        try {
            std::rethrow_exception(exception);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "Unhandled test exception: %s\n", error.what());
        } catch (...) {
            std::fputs("Unhandled non-standard test exception\n", stderr);
        }
    } else {
        std::fputs("Test called std::terminate without an active exception\n", stderr);
    }
    std::fflush(stderr);
    std::abort();
}

struct TestCrtDiagnosticsBootstrap {
    TestCrtDiagnosticsBootstrap() noexcept {
        configure_test_crt_diagnostics();
        std::set_terminate(report_unhandled_test_exception);
    }
};

const TestCrtDiagnosticsBootstrap test_crt_diagnostics_bootstrap;

} // namespace
