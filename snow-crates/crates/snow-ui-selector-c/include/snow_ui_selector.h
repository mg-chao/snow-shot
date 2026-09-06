#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SnowUiSelectorServiceImpl SnowUiSelectorService;
typedef struct SnowUiSelectorHitPathImpl SnowUiSelectorHitPath;

typedef enum SnowUiSelectorBackend {
    SNOW_UI_SELECTOR_BACKEND_UIA = 0,
    SNOW_UI_SELECTOR_BACKEND_MSAA = 1,
} SnowUiSelectorBackend;

typedef enum SnowUiSelectorHitTestMode {
    SNOW_UI_SELECTOR_HIT_TEST_MODE_UI_ELEMENT = 0,
    SNOW_UI_SELECTOR_HIT_TEST_MODE_WINDOW = 1,
} SnowUiSelectorHitTestMode;

typedef struct SnowUiSelectorRect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} SnowUiSelectorRect;

typedef void (*SnowUiSelectorRefreshCallback)(
    uint64_t request_id,
    uint8_t ok,
    void* userdata);

typedef void (*SnowUiSelectorHitTestPointCallback)(
    uint64_t request_id,
    SnowUiSelectorHitPath* path,
    uint8_t ok,
    void* userdata);

SnowUiSelectorService* snow_ui_selector_service_create(SnowUiSelectorBackend backend);
void snow_ui_selector_service_destroy(SnowUiSelectorService* service);

/* Queues release of the current window/accessibility snapshot without
   destroying the long-lived selector service. */
uint8_t snow_ui_selector_service_release_cache(SnowUiSelectorService* service);

uint8_t snow_ui_selector_service_refresh(
    SnowUiSelectorService* service,
    const uintptr_t* excluded_hwnds,
    size_t excluded_count);

SnowUiSelectorHitPath* snow_ui_selector_service_hit_test_point(
    SnowUiSelectorService* service,
    int32_t x,
    int32_t y,
    SnowUiSelectorHitTestMode mode);

uint8_t snow_ui_selector_service_refresh_async(
    SnowUiSelectorService* service,
    uint64_t request_id,
    const uintptr_t* excluded_hwnds,
    size_t excluded_count,
    SnowUiSelectorRefreshCallback callback,
    void* userdata);

uint8_t snow_ui_selector_service_hit_test_point_async(
    SnowUiSelectorService* service,
    uint64_t request_id,
    int32_t x,
    int32_t y,
    SnowUiSelectorHitTestMode mode,
    SnowUiSelectorHitTestPointCallback callback,
    void* userdata);

size_t snow_ui_selector_hit_path_count(const SnowUiSelectorHitPath* path);
uint8_t snow_ui_selector_hit_path_rect(
    const SnowUiSelectorHitPath* path,
    size_t index,
    SnowUiSelectorRect* out_rect);
void snow_ui_selector_hit_path_destroy(SnowUiSelectorHitPath* path);

const char* snow_ui_selector_last_error_message(void);

#ifdef __cplusplus
}
#endif
