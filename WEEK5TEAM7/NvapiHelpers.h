#pragma once

#include <d3d11.h>
#include <nvapi_lite_common.h> // Defines NvAPI_Status, NvU32, and shared NVAPI types.
#include <nvapi.h>
#include <cstddef>

namespace nvapi_example 
{
    NvAPI_Status Initialize();

    NvAPI_Status FindSettingId(const wchar_t* settingName, NvU32* settingId);
    NvAPI_Status ReadDwordSetting(
        const wchar_t* profileName,
        const wchar_t* settingName,
        NvU32* value);
NvAPI_Status WriteDwordSetting(
    const wchar_t* profileName,
    const wchar_t* settingName,
    NvU32 value);

// EXE-based variants. Pass the full executable path, e.g. L"C:/Games/MyGame/MyGame.exe".
NvAPI_Status GetProfileNameForExecutable(
    const wchar_t* executablePath,
    wchar_t* profileName,
    size_t profileNameCapacity);
NvAPI_Status ReadDwordSettingForExecutable(
    const wchar_t* executablePath,
    const wchar_t* settingName,
    NvU32* value);
NvAPI_Status WriteDwordSettingForExecutable(
    const wchar_t* executablePath,
    const wchar_t* settingName,
    NvU32 value);

// Current-process variants discover the installed executable path at runtime.
NvAPI_Status GetCurrentProfileName(wchar_t* profileName, size_t profileNameCapacity);
NvAPI_Status ReadDwordSettingForCurrentExecutable(
    const wchar_t* settingName,
    NvU32* value);
NvAPI_Status WriteDwordSettingForCurrentExecutable(
    const wchar_t* settingName,
    NvU32 value);

    NvAPI_Status PrintGpuInfo();
    NvAPI_Status PrintSliState(ID3D11Device* device);
    NvAPI_Status EnableLowLatency(ID3D11Device* device, bool boost = false);
    NvAPI_Status BeginLowLatencyFrame(ID3D11Device* device);

}
