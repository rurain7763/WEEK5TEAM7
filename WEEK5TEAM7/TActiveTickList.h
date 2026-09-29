#pragma once
#include "TArray.h"
#include "TMap.h"

// 활성 객체만 보관합니다. Tick 중 삭제는 빈 슬롯으로 표시해 대상을 즉시 제외합니다.
template<typename T>
class TActiveTickList
{
public:
    void Add(T* Object)
    {
        if (!Indices.Contains(Object)) Indices.Add(Object, Items.Add(Object));
    }
    void Remove(T* Object)
    {
        const uint32* Found = Indices.Find(Object);
        if (!Found) return;
        const uint32 Index = *Found;
        Indices.Remove(Object);
        if (bTicking)
        {
            Items[Index] = nullptr;
            bNeedsCompact = true;
        }
        else
        {
            Items.RemoveAtSwap(Index);
            if (Index < static_cast<uint32>(Items.Num())) Indices.Add(Items[Index], Index);
        }
    }
    void Tick(float DeltaTime)
    {
        assert(!bTicking);
        bTicking = true;
        bCancelled = false;
        // 이 목록을 순회하는 중 새로 활성화한 대상은 다음 순회부터 실행합니다.
        const uint32 Count = Items.Num();
        try
        {
            for (uint32 I = 0; I < Count && !bCancelled; ++I)
                if (T* Object = Items[I]) Object->Tick(DeltaTime);
        }
        catch (...) { FinishTick(); throw; }
        FinishTick();
    }
    uint32 Num() const { return Indices.Num(); }
    // 목록은 보존하고 진행 중인 순회만 중단합니다. 월드 재진입 시 다시 사용할 수 있습니다.
    void CancelTick() { if (bTicking) bCancelled = true; }
private:
    void FinishTick()
    {
        bTicking = false;
        if (!bNeedsCompact) return;
        for (uint32 I = 0; I < static_cast<uint32>(Items.Num());)
        {
            if (Items[I]) { ++I; continue; }
            Items.RemoveAtSwap(I);
            if (I < static_cast<uint32>(Items.Num()) && Items[I]) Indices.Add(Items[I], I);
        }
        bNeedsCompact = false;
    }
    TArray<T*> Items;
    TMap<T*, uint32> Indices;
    bool bTicking = false;
    bool bNeedsCompact = false;
    bool bCancelled = false;
};
