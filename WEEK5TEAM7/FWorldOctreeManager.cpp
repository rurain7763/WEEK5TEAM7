#include "FWorldOctreeManager.h"
#include "Actor.h"
#include "PrimitiveComponent.h"
#include "RenderInfo.h"
#include "FInstrumentor.h"
#include "ShowFlags.h"

void FWorldOctreeManager::MarkPrimitiveDirty(UPrimitiveComponent* Primitive)
{
    if (bDirty) return;
    const uint32 Index = Primitive->SpatialRegistrationIndex;
    // 분리되었거나 이전 월드·재구축의 등록이면 무시합니다. 추가·제거는 항상 MarkDirty를 거칩니다.
    if (Primitive->SpatialRegistrationGeneration != RegistrationGeneration
        || Index >= static_cast<uint32>(Registrations.Num())
        || Registrations[Index].Primitive != Primitive) return;
    FRegistration& Registration = Registrations[Index];
    if (Registration.bPending) return;
    Registration.bPending = true;
    ChangedRegistrations.Add(Index);
}

void FWorldOctreeManager::EnsureBuilt(const TArray<AActor*>& Actors)
{
    if (!bDirty && !ChangedRegistrations.IsEmpty())
    {
        PROFILE_SCOPE("World/OctreeUpdate");
        uint32 UpdatedEntries = 0;
        for (uint32 Index : ChangedRegistrations)
        {
            FRegistration& Registration = Registrations[Index];
            Registration.bPending = false;
            const FAABB Bounds = Registration.Primitive->GetBoundingBox();
            const bool bValid = FSpatialOctree::IsValidBounds(Bounds);
            if (Registration.EntryIndex == InvalidEntry && !bValid) continue;
            if (Registration.EntryIndex == InvalidEntry || !bValid
                || !Tree.UpdateEntryBounds(Registration.EntryIndex, Bounds))
            {
                // 부분 갱신 도중 실패해도 조회 전에 전체를 다시 구축하므로 중간 결과는 노출하지 않습니다.
                bDirty = true;
                break;
            }
            ++UpdatedEntries;
        }
        ChangedRegistrations.Empty();
        if (!bDirty && UpdatedEntries > 0)
        {
            UpdateCount += UpdatedEntries;
            ++UpdateBatchCount;
        }
    }
    if (!bDirty) return;
    PROFILE_SCOPE("World/OctreeBuild");
    // 삭제된 Primitive가 이전 등록에 남아 있어도 역참조하지 않고 목록만 폐기합니다.
    Registrations.Empty();
    ChangedRegistrations.Empty();
    ++RegistrationGeneration;
    Primitives.Empty();
    TArray<FSpatialEntry> Entries;
    for (AActor* Actor : Actors)
        for (UActorComponent* Component : Actor->GetComponents())
        {
            UPrimitiveComponent* Primitive = Component->Cast<UPrimitiveComponent>();
            if (!Primitive) continue;
            const FAABB Bounds = Primitive->GetBoundingBox();
            const bool bValid = FSpatialOctree::IsValidBounds(Bounds);
            const uint32 EntryIndex = bValid ? static_cast<uint32>(Entries.Num()) : InvalidEntry;
            Primitive->SpatialRegistrationGeneration = RegistrationGeneration;
            Primitive->SpatialRegistrationIndex = Registrations.Add({ Primitive, EntryIndex });
            // Invalid도 등록 연결은 유지하여 나중에 유효 경계로 바뀌는 시점을 감지합니다.
            if (bValid) Entries.Add({ Bounds, Primitives.Add(Primitive) });
        }
    Tree.Build(Entries);
    SpatialPrimitives.Empty();
    SpatialPrimitives.Reserve(Tree.GetEntryCount());
    for (uint32 I = 0; I < Tree.GetEntryCount(); ++I)
        SpatialPrimitives.Add(Primitives[Tree.GetObjectIndexAt(I)]);
    bDirty = false;
    ++BuildCount;
}

void FWorldOctreeManager::Render(FRenderCollector& Collector)
{
    const FShowFlags& Flags = FShowFlags::Get();
    {
        PROFILE_SCOPE("World/OctreeQuery");
        if (Flags.IsEnabled(EShowFlag::FrustumCulling))
            Tree.QueryFrustumRanges(Collector.Frustum, Visibility, CullingStats,
                Flags.IsEnabled(EShowFlag::Octree));
        else
        {
            Visibility.Reset();
            Visibility.bAllVisible = true;
            CullingStats = {};
            CullingStats.AcceptedEntries = static_cast<uint32>(Primitives.Num());
        }
    }
    PROFILE_SCOPE("World/RenderVisible");
    // 전체·부분 가시 모두 동일한 연속 포인터 순회로 처리합니다.
    // 부분 가시의 비용은 범위 수와 실제 Render 호출 수에 비례합니다.
    const auto RenderRange = [&](uint32 First, uint32 Count)
    {
        if (Count == 0) return;
        UPrimitiveComponent* const* Current = SpatialPrimitives.Data() + First;
        UPrimitiveComponent* const* End = Current + Count;
        for (; Current != End; ++Current) (*Current)->Render(Collector);
    };
    if (Visibility.bAllVisible)
        RenderRange(0, static_cast<uint32>(SpatialPrimitives.Num()));
    else
        for (const FVisibleEntryRange& Range : Visibility.Ranges)
            RenderRange(Range.First, Range.Count);
}

void FWorldOctreeManager::QueryPickTargets(const FPickingRay& Ray, TArray<UPrimitiveComponent*>& Out)
{
    PROFILE_SCOPE("World/OctreeRayQuery");
    Tree.QueryRay(Ray, RayIndices, PickingStats, FShowFlags::Get().IsEnabled(EShowFlag::Octree));
    Out.Empty();
    for (uint32 Index : RayIndices) Out.Add(Primitives[Index]);
}

UPrimitiveComponent* FWorldOctreeManager::RayCastClosest(const FPickingRay& Ray)
{
    PROFILE_SCOPE("World/OctreeRayQuery");
    UPrimitiveComponent* Closest = nullptr;
    uint32 ClosestIndex = ~uint32{0};
    Tree.VisitRay(Ray, PickingStats, [&](uint32 Index, float& BestDistance)
    {
        float HitT;
        if (!Primitives[Index]->RayCastComponent(Ray, HitT)
            || !std::isfinite(HitT) || HitT < 0 || HitT > 1) return;
        // 메시의 매개변수를 AABB 검사와 동일한 월드 거리로 변환합니다.
        const float Distance = HitT * Ray.Length;
        if (Distance < BestDistance || (Distance == BestDistance && Index < ClosestIndex))
        {
            BestDistance = Distance;
            ClosestIndex = Index;
            Closest = Primitives[Index];
        }
    }, FShowFlags::Get().IsEnabled(EShowFlag::Octree));
    return Closest;
}
