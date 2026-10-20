#pragma once
// Pure parser for fail-closed MCU stream inventory snapshots.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

inline bool inventory_matches(const std::string& dump, int32_t stock_id,
                              int32_t raw_id, bool allow_absent_stock) {
    if (allow_absent_stock != (stock_id <= 0) ||
        (stock_id > 0 && raw_id == stock_id)) return false;
    int count = -1, stock = 0, raw = 0, unknown = 0;
    std::istringstream input(dump);
    std::string line;
    while (std::getline(input, line)) {
        if (const char* p = std::strstr(line.c_str(), "CAN stream sessions:"))
            (void)std::sscanf(p, "CAN stream sessions: %d", &count);
        if (const char* p = std::strstr(line.c_str(), "id=")) {
            int id = 0, filters = -1;
            if (std::sscanf(p, "id=%d filters=%d", &id, &filters) != 2) {
                ++unknown; continue;
            }
            if (raw_id > 0 && id == raw_id && filters == 1) ++raw;
            else if (stock_id > 0 && id == stock_id && filters == 75) ++stock;
            else if (allow_absent_stock && raw_id > 0 && id != raw_id && filters == 75) ++stock;
            else ++unknown;
        }
    }
    if (unknown || stock > 1 || raw != (raw_id > 0 ? 1 : 0)) return false;
    if (allow_absent_stock)
        return (stock == 0 && count == 0 && raw_id <= 0) ||
               (raw_id > 0 && count == 1 + stock);
    return stock == 1 && count == 1 + (raw_id > 0 ? 1 : 0);
}
