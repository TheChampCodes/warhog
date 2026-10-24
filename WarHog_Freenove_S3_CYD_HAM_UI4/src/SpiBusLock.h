#pragma once
// ================================================================
//  Scheduler Phase 4 (Step 1, CYD) — shared SPI-bus lock.
//  On the Freenove S3 CYD the SD card and the ILI9341 TFT share ONE physical
//  SPI bus (pins 10-13; sdSpi(HSPI) is a separate *driver instance* on the
//  *same wires*). Once Core-mode SD logging moves to a core-0 task, the TFT
//  flush on core 1 (ScreenUI::present / getTouch) and the SD write on core 0
//  can collide on the bus. This recursive mutex serializes every shared-bus
//  access so the two cores never drive the wires at once.
//  Flag-gated PIG_FEAT_CORE_ISO: OFF (default) -> lock/unlock compile to
//  no-ops, zero behavior change. The mutex itself is created in MeshNode
//  coreStoreStart() (before the SD task can run) and lives for the session.
// ================================================================
#ifndef PIG_FEAT_CORE_ISO
#define PIG_FEAT_CORE_ISO 0
#endif

#if PIG_FEAT_CORE_ISO
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
extern SemaphoreHandle_t gSpiBusMutex;   // defined + created in MeshNode.cpp
static inline void spiBusLock()   { if (gSpiBusMutex) xSemaphoreTakeRecursive(gSpiBusMutex, portMAX_DELAY); }
static inline void spiBusUnlock() { if (gSpiBusMutex) xSemaphoreGiveRecursive(gSpiBusMutex); }
#else
static inline void spiBusLock()   {}
static inline void spiBusUnlock() {}
#endif
