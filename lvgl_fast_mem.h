// LVGL's working memory in fast internal RAM.
// ESPHome hands every LVGL allocation to PSRAM, including the scratch buffers the
// software renderer uses for each anti-aliased line and arc, which makes all drawing
// slow. The linker redirects LVGL's calls here (see build_flags in desk-clock.yaml):
// small blocks come from internal RAM, big ones and any overflow from PSRAM.
#pragma once
#include <esp_heap_caps.h>

static const size_t LV_FAST_MEM_MAX = 32 * 1024;

extern "C" void *__wrap_lv_malloc_core(size_t size) {
  void *p = nullptr;
  if (size <= LV_FAST_MEM_MAX)
    p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (p == nullptr)
    p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p == nullptr)
    p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
  return p;
}

extern "C" void *__wrap_lv_realloc_core(void *ptr, size_t size) {
  // Either kind of memory is fine; the block only moves if it has to grow
  return heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
}
