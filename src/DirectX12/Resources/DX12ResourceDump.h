#pragma once

#include "DX12Runtime.h"
#include "DX12Tracking.h"

#include <d3d11.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// Resource collection, GPU readback, format conversion, and frame dump.
// Kept separate from lifetime tracking and resource creation hooks.
// Umbrella header: split into per-responsibility headers under Resources/Dump.
// Include order matches the original declaration order.

#include "Dump/DX12PsoInputLayoutUtils.h"
#include "Dump/DX12ResourceCollectQueue.h"
#include "Dump/DX12ResourceDumpCore.h"
#include "Dump/DX12ResourceDumpBufferText.h"
#include "Dump/DX12ResourceDumpDedupedNaming.h"
#include "Dump/DX12ResourceDumpDds.h"
#include "Dump/DX12ResourceDumpPolicy.h"
#include "Dump/DX12ResourceDumpNaming.h"
#include "Dump/DX12TextureHashUtils.h"
#include "Dump/DX12ResourceDumpFileWriter.h"
#include "Dump/DX12ResourceDumpPrepare.h"
#include "Dump/DX12ResourceDumpGpuCopy.h"
#include "Dump/DX12ResourceDumpIndexWriter.h"
#include "Dump/DX12ResourceDump.h"
