#pragma once

#include "FSpatialOctree.h"

class AActor;
class UPrimitiveComponent;
struct FRenderCollector;

// 월드가 소유하며 컴포넌트는 비소유 포인터로 참조합니다.
class FWorldOctreeManager
{
public:
    // 변경을 기록합니다. 실제 재구축은 다음 EnsureBuilt 호출까지 미룹니다.
    void MarkDirty() { bDirty = true; ChangedRegistrations.Empty(); }
    // 등록된 Primitive의 변경을 슬롯으로 기록합니다. 같은 슬롯은 조회 전까지 한 번만 넣습니다.
    void MarkPrimitiveDirty(UPrimitiveComponent* Primitive);
    // 변경 목록을 부분 갱신하고, 등록 변경·셀 이탈·유효성 전환이면 전체 재구축합니다.
    void EnsureBuilt(const TArray<AActor*>& Actors);
    // 현재 절두체의 가시 범위를 조회하고 해당 Primitive의 RenderInfo를 수집합니다.
    void Render(FRenderCollector& Collector);
    // 광선과 경계가 겹치는 모든 후보를 반환합니다. 실제 메시 충돌 검사는 하지 않습니다.
    void QueryPickTargets(const FPickingRay& Ray, TArray<UPrimitiveComponent*>& Out);
    // 가까운 노드부터 실제 충돌을 검사하고 최단 거리보다 먼 가지는 생략합니다.
    UPrimitiveComponent* RayCastClosest(const FPickingRay& Ray);

    // 최근 렌더 조회의 노드·객체 검사 수와 가시 객체 수를 반환합니다.
    const FSpatialQueryStats& GetCullingStats() const { return CullingStats; }
    // 최근 광선 조회의 노드·객체 검사 수와 후보 수를 반환합니다.
    const FSpatialQueryStats& GetPickingStats() const { return PickingStats; }
    // 완료된 전체 재구축의 누적 횟수를 반환합니다.
    uint32 GetBuildCount() const { return BuildCount; }
    // 현재 트리에 등록된 유효 Primitive 수를 반환합니다.
    uint32 GetEntryCount() const { return Tree.GetEntryCount(); }
    // 현재 구축된 트리의 전체 노드 수를 반환합니다.
    uint32 GetNodeCount() const { return Tree.GetNodeCount(); }

    // 전체 재구축 없이 갱신한 유효 엔트리 수와 갱신 묶음의 누적 횟수입니다.
    uint32 GetUpdateCount() const { return UpdateCount; }
    uint32 GetUpdateBatchCount() const { return UpdateBatchCount; }

private:
    struct FRegistration
    {
        UPrimitiveComponent* Primitive;
        uint32 EntryIndex;
        bool bPending = false;
    };
    static constexpr uint32 InvalidEntry = ~uint32{0};
    TArray<FRegistration> Registrations;
    TArray<uint32> ChangedRegistrations;
    uint64 RegistrationGeneration = 0;
    uint32 UpdateCount = 0;
    uint32 UpdateBatchCount = 0;
    FSpatialOctree Tree;
    TArray<UPrimitiveComponent*> Primitives;
    // 트리의 엔트리 순서로 재구축 시 한 번만 준비하는 비소유 포인터 배열입니다.
    TArray<UPrimitiveComponent*> SpatialPrimitives;
    FFrustumQueryResult Visibility;
    TArray<uint32> RayIndices;
    FSpatialQueryStats CullingStats;
    FSpatialQueryStats PickingStats;
    bool bDirty = true;
    uint32 BuildCount = 0;
};
