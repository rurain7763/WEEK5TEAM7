#include "FWorldOctreeManager.h"
#include "Actor.h"
#include "PrimitiveComponent.h"
#include "RenderInfo.h"
#include "FInstrumentor.h"
#include "ShowFlags.h"

void FWorldOctreeManager::EnsureBuilt(const TArray<AActor*>& Actors)
{
    if (!bDirty) return;
    PROFILE_SCOPE("World/OctreeBuild");
    Primitives.Empty();
    TArray<FSpatialEntry> Entries;
    for (AActor* Actor : Actors)
        for (UActorComponent* Component : Actor->GetComponents())
        {
            UPrimitiveComponent* Primitive = Component->Cast<UPrimitiveComponent>();
            if (!Primitive) continue;
            const FAABB Bounds = Primitive->GetBoundingBox();
            if (!FSpatialOctree::IsValidBounds(Bounds)) continue;
            Entries.Add({ Bounds, Primitives.Add(Primitive) });
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
