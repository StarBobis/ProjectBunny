#include "Resources/DX12ResourceFileIO.hpp"
#include "Mods/DX12ModInterfaces.hpp"
#include "Tracking/DX12ResourceInfrastructure.hpp"
#include "Mods/DX12ConfigResources.hpp"
#include "Presentation/DX12UiRuntime.hpp"

#include <Windows.h>
#include <Shlwapi.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <iterator>
#include <new>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Implementation/DX12ShaderAnalysis.inl"
#include "Implementation/DX12PipelineStateTracking.inl"
#include "Implementation/DX12ShaderDumpRuntime.inl"
#include "Implementation/DX12ShaderHuntState.inl"
#include "Implementation/DX12ShaderHuntRuntime.inl"
#include "Implementation/DX12ShaderHuntUi.inl"
