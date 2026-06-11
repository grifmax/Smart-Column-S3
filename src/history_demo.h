#ifndef HISTORY_DEMO_H
#define HISTORY_DEMO_H

#include <Arduino.h>
#include <stdint.h>

struct DemoHistorySeedResult {
    uint16_t imported = 0;
    uint16_t skipped = 0;
    uint16_t removed = 0;
};

bool seedPublicDemoDataset(DemoHistorySeedResult& result, bool replaceExisting);
bool clearPublicDemoDataset(DemoHistorySeedResult& result);
uint16_t countPublicDemoDatasetEntries();

#endif
