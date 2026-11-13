#pragma once

#include "CommandList.h"
#include "globals.h"

// Existing hash/readback fixtures explicitly opt into the new default-off
// policy. This does not fabricate a snapshot or change production defaults.
class DataCacheTestScope {
    DataCacheBindFlags saved = G->cache_resource_data.load();
public:
    explicit DataCacheTestScope(DataCacheBindFlags selected = DataCacheBindFlags::ALL_MASK)
    {
        G->cache_resource_data.store(selected);
    }
    ~DataCacheTestScope() { G->cache_resource_data.store(saved); }
};
