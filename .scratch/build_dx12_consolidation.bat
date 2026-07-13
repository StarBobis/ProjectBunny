@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cmake -S . -B build\dx12-consolidation -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
if errorlevel 1 exit /b 1
cmake --build build\dx12-consolidation --target DirectX12 DX12SparseDescriptorStoreTests DX12DescriptorHeapShardStateTests DX12DescriptorStateTests DX12PreSkinCacheIdentityTests DX12DirectIaFastRejectCacheTests
