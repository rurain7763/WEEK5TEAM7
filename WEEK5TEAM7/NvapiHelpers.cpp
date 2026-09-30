// Minimal NVAPI examples for a Windows / Direct3D 11 application.
// Add the NVAPI SDK include directory and link nvapi64.lib (x64) or nvapi.lib (x86).

#include <Windows.h>
#include "NvapiHelpers.h"
#include <nvapi/nvapi_lite_sli.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace nvapi_example {

// NVAPI initialization is process-wide. Call once before using any helper here.
NvAPI_Status Initialize() {
    return NvAPI_Initialize();
}

bool ToNvString(const wchar_t* source, NvAPI_UnicodeString destination) {
    if (!source || !destination) return false;

    size_t i = 0;
    for (; source[i] != L'\0' && i < NVAPI_UNICODE_STRING_MAX - 1; ++i) {
        destination[i] = static_cast<NvU16>(source[i]);
    }
    destination[i] = 0;
    return source[i] == L'\0';
}

NvAPI_Status FindProfileForExecutable(
    NvDRSSessionHandle session,
    const wchar_t* executablePath,
    NvDRSProfileHandle* profile) {
    if (!session || !executablePath || !profile) return NVAPI_INVALID_ARGUMENT;

    // NVIDIA recommends a fully-qualified path to avoid matching another EXE
    // with the same basename. Its documentation shows forward slashes.
    std::wstring normalizedPath(executablePath);
    for (wchar_t& ch : normalizedPath) {
        if (ch == L'\\') ch = L'/';
    }

    NvAPI_UnicodeString nvPath{};
    if (!ToNvString(normalizedPath.c_str(), nvPath)) return NVAPI_INVALID_ARGUMENT;

    NVDRS_APPLICATION app{};
    app.version = NVDRS_APPLICATION_VER;
    NvAPI_Status status = NvAPI_DRS_FindApplicationByName(session, nvPath, profile, &app);

    // If the executable is not registered in any profile, create a new profile for it.
    if (status == NVAPI_EXECUTABLE_NOT_FOUND) {
        size_t lastSlash = normalizedPath.find_last_of(L'/');
        std::wstring exeName = (lastSlash != std::wstring::npos) ? normalizedPath.substr(lastSlash + 1) : normalizedPath;
        std::wstring profileName = exeName + L" Profile";

        NVDRS_PROFILE profileInfo{};
        profileInfo.version = NVDRS_PROFILE_VER;
        profileInfo.isPredefined = 0;
        ToNvString(profileName.c_str(), profileInfo.profileName);

        status = NvAPI_DRS_CreateProfile(session, &profileInfo, profile);
        if (status == NVAPI_PROFILE_NAME_IN_USE) {
            status = NvAPI_DRS_FindProfileByName(session, profileInfo.profileName, profile);
            if (status != NVAPI_OK) return status;
        } else if (status != NVAPI_OK) {
            return status;
        }

        NVDRS_APPLICATION newApp{};
        newApp.version = NVDRS_APPLICATION_VER;
        newApp.isPredefined = 0;
        ToNvString(normalizedPath.c_str(), newApp.appName);
        ToNvString(exeName.c_str(), newApp.userFriendlyName);

        status = NvAPI_DRS_CreateApplication(session, *profile, &newApp);
        if (status != NVAPI_OK && status != NVAPI_EXECUTABLE_ALREADY_IN_USE) {
            return status;
        }
        return NVAPI_OK;
    }

    return status;
}

NvAPI_Status GetCurrentExecutablePath(std::wstring& path) {
    // Long-path-safe buffer. GetModuleFileNameW returns the path of this process' EXE.
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return NVAPI_ERROR;

    path.assign(buffer.data(), length);
    return NVAPI_OK;
}

class DrsSession {
public:
    DrsSession() {
        status_ = NvAPI_DRS_CreateSession(&handle_);
        if (status_ == NVAPI_OK) {
            status_ = NvAPI_DRS_LoadSettings(handle_);
        }
    }

    ~DrsSession() {
        if (handle_) NvAPI_DRS_DestroySession(handle_);
    }

    NvAPI_Status Status() const { return status_; }
    NvDRSSessionHandle Get() const { return handle_; }

private:
    NvDRSSessionHandle handle_ = nullptr;
    NvAPI_Status status_ = NVAPI_ERROR;
};

// Resolve a public/driver-known setting name to its numeric DRS ID.
// For example, this lets you check whether the exact name "LoadBalance" exists.
NvAPI_Status FindSettingId(const wchar_t* settingName, NvU32* settingId) {
    if (!settingName || !settingId) return NVAPI_INVALID_ARGUMENT;

    NvAPI_UnicodeString nvName{};
    if (!ToNvString(settingName, nvName)) return NVAPI_INVALID_ARGUMENT;
    return NvAPI_DRS_GetSettingIdFromName(nvName, settingId);
}

// Read a DWORD setting from a named NVIDIA driver profile (for example "Base Profile").
NvAPI_Status ReadDwordSetting(
    const wchar_t* profileName,
    const wchar_t* settingName,
    NvU32* value) {
    if (!profileName || !settingName || !value) return NVAPI_INVALID_ARGUMENT;

    NvU32 settingId = 0;
    NvAPI_Status status = FindSettingId(settingName, &settingId);
    if (status != NVAPI_OK) return status;

    DrsSession session;
    if (session.Status() != NVAPI_OK) return session.Status();

    NvAPI_UnicodeString nvProfileName{};
    if (!ToNvString(profileName, nvProfileName)) return NVAPI_INVALID_ARGUMENT;

    NvDRSProfileHandle profile = nullptr;
    status = NvAPI_DRS_FindProfileByName(session.Get(), nvProfileName, &profile);
    if (status != NVAPI_OK) return status;

    NVDRS_SETTING setting{};
    setting.version = NVDRS_SETTING_VER;
    status = NvAPI_DRS_GetSetting(session.Get(), profile, settingId, &setting);
    if (status != NVAPI_OK) return status;
    if (setting.settingType != NVDRS_DWORD_TYPE) return NVAPI_INVALID_ARGUMENT;

    *value = setting.u32CurrentValue;
    return NVAPI_OK;
}

// Write a DWORD setting to a named driver profile and save it.
// Only call this with an ID/value confirmed as valid for the target driver.
NvAPI_Status WriteDwordSetting(
    const wchar_t* profileName,
    const wchar_t* settingName,
    NvU32 value) {
    if (!profileName || !settingName) return NVAPI_INVALID_ARGUMENT;

    NvU32 settingId = 0;
    NvAPI_Status status = FindSettingId(settingName, &settingId);
    if (status != NVAPI_OK) return status;

    DrsSession session;
    if (session.Status() != NVAPI_OK) return session.Status();

    NvAPI_UnicodeString nvProfileName{};
    if (!ToNvString(profileName, nvProfileName)) return NVAPI_INVALID_ARGUMENT;

    NvDRSProfileHandle profile = nullptr;
    status = NvAPI_DRS_FindProfileByName(session.Get(), nvProfileName, &profile);
    if (status != NVAPI_OK) return status;

    NVDRS_SETTING setting{};
    setting.version = NVDRS_SETTING_VER;
    setting.settingId = settingId;
    setting.settingType = NVDRS_DWORD_TYPE;
    setting.u32CurrentValue = value;

    status = NvAPI_DRS_SetSetting(session.Get(), profile, &setting);
    if (status != NVAPI_OK) return status;
    return NvAPI_DRS_SaveSettings(session.Get());
}

NvAPI_Status GetProfileNameForExecutable(
    const wchar_t* executablePath,
    wchar_t* profileName,
    size_t profileNameCapacity) {
    if (!executablePath || !profileName || profileNameCapacity == 0)
        return NVAPI_INVALID_ARGUMENT;

    DrsSession session;
    if (session.Status() != NVAPI_OK) return session.Status();

    NvDRSProfileHandle profile = nullptr;
    NvAPI_Status status = FindProfileForExecutable(
        session.Get(), executablePath, &profile);
    if (status != NVAPI_OK) return status;

    NVDRS_PROFILE info{};
    info.version = NVDRS_PROFILE_VER;
    status = NvAPI_DRS_GetProfileInfo(session.Get(), profile, &info);
    if (status != NVAPI_OK) return status;

    size_t i = 0;
    for (; i + 1 < profileNameCapacity && info.profileName[i] != 0; ++i) {
        profileName[i] = static_cast<wchar_t>(info.profileName[i]);
    }
    profileName[i] = L'\0';
    if (info.profileName[i] != 0) return NVAPI_INVALID_ARGUMENT;
    return NVAPI_OK;
}

NvAPI_Status ReadDwordSettingForExecutable(
    const wchar_t* executablePath,
    const wchar_t* settingName,
    NvU32* value) {
    if (!executablePath || !settingName || !value) return NVAPI_INVALID_ARGUMENT;

    NvU32 settingId = 0;
    NvAPI_Status status = FindSettingId(settingName, &settingId);
    if (status != NVAPI_OK) return status;

    DrsSession session;
    if (session.Status() != NVAPI_OK) return session.Status();

    NvDRSProfileHandle profile = nullptr;
    status = FindProfileForExecutable(session.Get(), executablePath, &profile);
    if (status != NVAPI_OK) return status;

    NVDRS_SETTING setting{};
    setting.version = NVDRS_SETTING_VER;
    status = NvAPI_DRS_GetSetting(session.Get(), profile, settingId, &setting);
    if (status != NVAPI_OK) return status;
    if (setting.settingType != NVDRS_DWORD_TYPE) return NVAPI_INVALID_ARGUMENT;

    *value = setting.u32CurrentValue;
    return NVAPI_OK;
}

NvAPI_Status WriteDwordSettingForExecutable(
    const wchar_t* executablePath,
    const wchar_t* settingName,
    NvU32 value) {
    if (!executablePath || !settingName) return NVAPI_INVALID_ARGUMENT;

    NvU32 settingId = 0;
    NvAPI_Status status = FindSettingId(settingName, &settingId);
    if (status != NVAPI_OK) return status;

    DrsSession session;
    if (session.Status() != NVAPI_OK) return session.Status();

    NvDRSProfileHandle profile = nullptr;
    status = FindProfileForExecutable(session.Get(), executablePath, &profile);
    if (status != NVAPI_OK) return status;

    NVDRS_SETTING setting{};
    setting.version = NVDRS_SETTING_VER;
    setting.settingId = settingId;
    setting.settingType = NVDRS_DWORD_TYPE;
    setting.u32CurrentValue = value;

    status = NvAPI_DRS_SetSetting(session.Get(), profile, &setting);
    if (status != NVAPI_OK) return status;
    return NvAPI_DRS_SaveSettings(session.Get());
}

NvAPI_Status GetCurrentProfileName(
    wchar_t* profileName,
    size_t profileNameCapacity) {
    std::wstring executablePath;
    const NvAPI_Status status = GetCurrentExecutablePath(executablePath);
    if (status != NVAPI_OK) return status;
    return GetProfileNameForExecutable(
        executablePath.c_str(), profileName, profileNameCapacity);
}

NvAPI_Status ReadDwordSettingForCurrentExecutable(
    const wchar_t* settingName,
    NvU32* value) {
    if (!settingName || !value) return NVAPI_INVALID_ARGUMENT;

    std::wstring executablePath;
    const NvAPI_Status status = GetCurrentExecutablePath(executablePath);
    if (status != NVAPI_OK) return status;
    return ReadDwordSettingForExecutable(
        executablePath.c_str(), settingName, value);
}

NvAPI_Status WriteDwordSettingForCurrentExecutable(
    const wchar_t* settingName,
    NvU32 value) {
    if (!settingName) return NVAPI_INVALID_ARGUMENT;

    std::wstring executablePath;
    const NvAPI_Status status = GetCurrentExecutablePath(executablePath);
    if (status != NVAPI_OK) return status;
    return WriteDwordSettingForExecutable(
        executablePath.c_str(), settingName, value);
}

NvAPI_Status WriteDwordSettingByIdForExecutable(
    const wchar_t* executablePath,
    NvU32 settingId,
    NvU32 value) {
    if (!executablePath) return NVAPI_INVALID_ARGUMENT;

    DrsSession session;
    if (session.Status() != NVAPI_OK) return session.Status();

    NvDRSProfileHandle profile = nullptr;
    NvAPI_Status status = FindProfileForExecutable(session.Get(), executablePath, &profile);
    if (status != NVAPI_OK) return status;

    NVDRS_SETTING setting{};
    setting.version = NVDRS_SETTING_VER;
    setting.settingId = settingId;
    setting.settingType = NVDRS_DWORD_TYPE;
    setting.u32CurrentValue = value;

    status = NvAPI_DRS_SetSetting(session.Get(), profile, &setting);
    if (status != NVAPI_OK) return status;
    return NvAPI_DRS_SaveSettings(session.Get());
}

NvAPI_Status WriteDwordSettingByIdForCurrentExecutable(
    NvU32 settingId,
    NvU32 value) {
    std::wstring executablePath;
    const NvAPI_Status status = GetCurrentExecutablePath(executablePath);
    if (status != NVAPI_OK) return status;
    return WriteDwordSettingByIdForExecutable(
        executablePath.c_str(), settingId, value);
}

NvAPI_Status ReadDwordSettingByIdForExecutable(
    const wchar_t* executablePath,
    NvU32 settingId,
    NvU32* value) {
    if (!executablePath || !value) return NVAPI_INVALID_ARGUMENT;

    DrsSession session;
    if (session.Status() != NVAPI_OK) return session.Status();

    NvDRSProfileHandle profile = nullptr;
    NvAPI_Status status = FindProfileForExecutable(session.Get(), executablePath, &profile);
    if (status != NVAPI_OK) return status;

    NVDRS_SETTING setting{};
    setting.version = NVDRS_SETTING_VER;
    status = NvAPI_DRS_GetSetting(session.Get(), profile, settingId, &setting);
    if (status != NVAPI_OK) return status;
    if (setting.settingType != NVDRS_DWORD_TYPE) return NVAPI_INVALID_ARGUMENT;

    *value = setting.u32CurrentValue;
    return NVAPI_OK;
}

NvAPI_Status ReadDwordSettingByIdForCurrentExecutable(
    NvU32 settingId,
    NvU32* value) {
    std::wstring executablePath;
    const NvAPI_Status status = GetCurrentExecutablePath(executablePath);
    if (status != NVAPI_OK) return status;
    return ReadDwordSettingByIdForExecutable(
        executablePath.c_str(), settingId, value);
}

// Print memory, temperature, and current graphics/memory clocks for NVIDIA GPUs.
NvAPI_Status PrintGpuInfo() {
    NvPhysicalGpuHandle gpus[NVAPI_MAX_PHYSICAL_GPUS]{};
    NvU32 gpuCount = 0;
    NvAPI_Status status = NvAPI_EnumPhysicalGPUs(gpus, &gpuCount);
    if (status != NVAPI_OK) return status;

    for (NvU32 i = 0; i < gpuCount; ++i) {
        std::wcout << L"GPU " << i << L":\n";

        NV_GPU_MEMORY_INFO_EX memory{};
        memory.version = NV_GPU_MEMORY_INFO_EX_VER;
        status = NvAPI_GPU_GetMemoryInfoEx(gpus[i], &memory);
        if (status == NVAPI_OK) {
            std::wcout << L"  VRAM total: " << (memory.dedicatedVideoMemory / 1024)
                       << L" MiB, available: "
                       << (memory.availableDedicatedVideoMemory / 1024) << L" MiB\n";
        } else {
            std::wcout << L"  Memory query failed: " << status << L"\n";
        }

        NV_GPU_THERMAL_SETTINGS thermal{};
        thermal.version = NV_GPU_THERMAL_SETTINGS_VER;
        status = NvAPI_GPU_GetThermalSettings(
            gpus[i], NVAPI_THERMAL_TARGET_ALL, &thermal);
        if (status == NVAPI_OK) {
            for (NvU32 sensor = 0; sensor < thermal.count; ++sensor) {
                std::wcout << L"  Temperature sensor " << sensor << L": "
                           << thermal.sensor[sensor].currentTemp << L" C\n";
            }
        } else {
            std::wcout << L"  Temperature query failed: " << status << L"\n";
        }

        NV_GPU_CLOCK_FREQUENCIES clocks{};
        clocks.version = NV_GPU_CLOCK_FREQUENCIES_VER;
        clocks.ClockType = NV_GPU_CLOCK_FREQUENCIES_CURRENT_FREQ;
        status = NvAPI_GPU_GetAllClockFrequencies(gpus[i], &clocks);
        if (status == NVAPI_OK) {
            const auto& graphics = clocks.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS];
            const auto& memoryClock = clocks.domain[NVAPI_GPU_PUBLIC_CLOCK_MEMORY];
            if (graphics.bIsPresent)
                std::wcout << L"  Graphics clock: " << (graphics.frequency / 1000)
                           << L" MHz\n";
            if (memoryClock.bIsPresent)
                std::wcout << L"  Memory clock: " << (memoryClock.frequency / 1000)
                           << L" MHz\n";
        } else {
            std::wcout << L"  Clock query failed: " << status << L"\n";
        }
    }
    return NVAPI_OK;
}

// Query SLI/AFR state for this D3D11 device. On a single-GPU system this may
// simply report no active AFR groups or return an unsupported/error status.
NvAPI_Status PrintSliState(ID3D11Device* device) {
    if (!device) return NVAPI_INVALID_ARGUMENT;

    NV_GET_CURRENT_SLI_STATE state{};
    state.version = NV_GET_CURRENT_SLI_STATE_VER;
    const NvAPI_Status status = NvAPI_D3D_GetCurrentSLIState(device, &state);
    if (status == NVAPI_OK) {
        std::wcout << L"SLI AFR groups: " << state.numAFRGroups
                   << L", current: " << state.currentAFRIndex
                   << L", next: " << state.nextFrameAFRIndex << L"\n";
    }
    return status;
}

// Configure Reflex low-latency mode once after creating the D3D11 device.
NvAPI_Status EnableLowLatency(ID3D11Device* device, bool boost) {
    if (!device) return NVAPI_INVALID_ARGUMENT;

    NV_SET_SLEEP_MODE_PARAMS mode{};
    mode.version = NV_SET_SLEEP_MODE_PARAMS_VER;
    mode.bLowLatencyMode = 1;
    mode.bLowLatencyBoost = boost ? 1 : 0;
    return NvAPI_D3D_SetSleepMode(device, &mode);
}

// Call at the start of every frame, before input sampling and rendering work.
NvAPI_Status BeginLowLatencyFrame(ID3D11Device* device) {
    if (!device) return NVAPI_INVALID_ARGUMENT;
    return NvAPI_D3D_Sleep(device);
}

} // namespace nvapi_example

/* Example integration:

if (nvapi_example::Initialize() == NVAPI_OK) {
    nvapi_example::PrintGpuInfo();

    NvU32 id = 0;
    const NvAPI_Status lookup = nvapi_example::FindSettingId(L"LoadBalance", &id);
    if (lookup == NVAPI_OK) {
        std::wcout << L"LoadBalance setting ID: 0x" << std::hex << id << L"\n";
    } else {
        std::wcout << L"LoadBalance lookup status: " << lookup << L"\n";
    }

    // After creating ID3D11Device* device:
    // nvapi_example::PrintSliState(device);
    // nvapi_example::EnableLowLatency(device);

    // Every frame, before input/update/render:
    // nvapi_example::BeginLowLatencyFrame(device);
}

Do not call WriteDwordSetting until you have verified the setting ID, type,
valid values, and the exact target profile. "Base Profile" changes are global.
*/
