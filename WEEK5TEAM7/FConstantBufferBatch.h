#pragma once

#include "Core.h"
#include "TArray.h"
#include <d3d11_1.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <type_traits>

// 개별 Draw의 상수를 64KB 페이지에 모아 업로드합니다. Draw 수는 바꾸지 않습니다.
class FConstantBufferBatch
{
public:
    static constexpr uint32 SlotBytes = 256;
    static constexpr uint32 PageBytes = 65536;
    static constexpr uint32 SlotsPerPage = PageBytes / SlotBytes;

    void Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InContext)
    {
        Reset();
        D3D11_FEATURE_DATA_D3D11_OPTIONS Options{};
        if (!InDevice || !InContext || InContext->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE
            || FAILED(InDevice->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &Options, sizeof(Options)))
            || !Options.ConstantBufferOffsetting) return;
        if (FAILED(InContext->QueryInterface(IID_PPV_ARGS(Context.GetAddressOf())))) return;
        Device = InDevice;
    }

    void Reset()
    {
        Pages.Empty();
        Context.Reset();
        Device.Reset();
        UploadedCount = 0;
        UploadMapCount = 0;
    }

    bool IsSupported() const { return Context != nullptr; }
    uint32 GetUploadMapCount() const { return UploadMapCount; }

    // 모든 업로드가 성공한 경우에만 구간 바인딩을 허용합니다. 실패 시 호출자가 기존 경로를 사용합니다.
    template<typename T, typename FWriter>
    bool Upload(uint32 Count, FWriter WriteConstant)
    {
        static_assert(sizeof(T) <= SlotBytes);
        static_assert(std::is_trivially_copyable_v<T>);
        UploadedCount = 0;
        UploadMapCount = 0;
        if (!IsSupported() || Count == 0) return false;
        const uint32 PageCount = (Count - 1) / SlotsPerPage + 1;
        while (static_cast<uint32>(Pages.Num()) < PageCount)
        {
            D3D11_BUFFER_DESC Desc{};
            Desc.ByteWidth = PageBytes;
            Desc.Usage = D3D11_USAGE_DYNAMIC;
            Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            Microsoft::WRL::ComPtr<ID3D11Buffer> Page;
            if (FAILED(Device->CreateBuffer(&Desc, nullptr, Page.GetAddressOf()))) return false;
            Pages.Emplace(std::move(Page));
        }
        for (uint32 PageIndex = 0; PageIndex < PageCount; ++PageIndex)
        {
            D3D11_MAPPED_SUBRESOURCE Mapped{};
            if (FAILED(Context->Map(Pages[PageIndex].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped))) return false;
            ++UploadMapCount;
            const uint32 First = PageIndex * SlotsPerPage;
            const uint32 LocalCount = (std::min)(SlotsPerPage, Count - First);
            for (uint32 Slot = 0; Slot < LocalCount; ++Slot)
            {
                // 쓰기 전용 메모리를 읽지 않고, CPU에서 완성한 값을 단방향으로 복사합니다.
                const T Value = WriteConstant(First + Slot);
                std::memcpy(static_cast<uint8*>(Mapped.pData) + Slot * SlotBytes, &Value, sizeof(T));
            }
            Context->Unmap(Pages[PageIndex].Get(), 0);
        }
        UploadedCount = Count;
        return true;
    }

    // 오프셋과 크기는 16바이트 상수 단위입니다. API 제약에 맞게 256바이트 경계를 사용합니다.
    ID3D11Buffer* Bind(uint32 Index)
    {
        assert(Index < UploadedCount);
        ID3D11Buffer* Buffer = Pages[Index / SlotsPerPage].Get();
        const UINT FirstConstant = (Index % SlotsPerPage) * (SlotBytes / 16);
        const UINT NumConstants = SlotBytes / 16;
        Context->VSSetConstantBuffers1(0, 1, &Buffer, &FirstConstant, &NumConstants);
        Context->PSSetConstantBuffers1(0, 1, &Buffer, &FirstConstant, &NumConstants);
        return Buffer;
    }

private:
    Microsoft::WRL::ComPtr<ID3D11Device> Device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> Context;
    TArray<Microsoft::WRL::ComPtr<ID3D11Buffer>> Pages;
    uint32 UploadedCount = 0;
    uint32 UploadMapCount = 0;
};
