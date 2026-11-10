#pragma once

// Keep failing Windows test processes noninteractive while preserving CRT reports
// and the original failure/abort exit status.
#if defined(_MSC_VER)
#include <cstdlib>
#include <crtdbg.h>
#endif

inline void configure_test_crt_diagnostics() {
#if defined(_MSC_VER)
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
    const int report_types[] = {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT};
    for (int report_type : report_types) {
        _CrtSetReportMode(report_type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(report_type, _CRTDBG_FILE_STDERR);
    }
#endif
#endif
}
