#pragma once
// Stable C ABI between UWP C++/CX and the clang-cl guest code.
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct HeadlessBootReport {
    uint32_t entry_address;
    uint32_t recompiled_function_count;
    uint32_t recompiled_import_count;
    uint32_t guest_result;
    uint32_t host_address;
    uint32_t host_result;
    uint32_t unknown_address_rejected;
} HeadlessBootReport;
// 0 on complete success. Nonzero is a bootstrap validation failure;
// the existing UWP GUI remains operational for diagnostics.
int nsmbu_headless_public_boot(HeadlessBootReport* report);
#ifdef __cplusplus
}
#endif
