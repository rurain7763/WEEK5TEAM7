#pragma once

#include "FSpatialOctree.h"
#include "EngineMathLibrary.h"

// 메시 에셋마다 하나를 소유합니다. 월드와 컴포넌트에는 의존하지 않습니다.
class FMeshPickingOctree
{
public:
    template<typename VertexType>
    void Build(const TArray<VertexType>& Vertices, const TArray<uint32>& Indices,
        const FAABB& LocalBounds, uint32 MaxDepth)
    {
        TArray<FSpatialEntry> Entries;
        Entries.Reserve(Indices.Num() / 3);
        for (uint32 I = 0; I + 2 < static_cast<uint32>(Indices.Num()); I += 3)
        {
            if (Indices[I] >= static_cast<uint32>(Vertices.Num())
                || Indices[I + 1] >= static_cast<uint32>(Vertices.Num())
                || Indices[I + 2] >= static_cast<uint32>(Vertices.Num())) continue;
            const FVector A = Vertices[Indices[I]].GetPosition();
            FAABB Bounds(A, A);
            Bounds.ExpandToInclude(Vertices[Indices[I + 1]].GetPosition());
            Bounds.ExpandToInclude(Vertices[Indices[I + 2]].GetPosition());
            // 인덱스 버퍼의 시작 위치만 저장하며 정점 데이터는 복제하지 않습니다.
            Entries.Add({ Bounds, I });
        }
        Tree.Build(Entries, 0.0f, MaxDepth, &LocalBounds);
    }

    // 호출한 LOD의 정점과 인덱스에 대해 정확한 최단 충돌을 반환합니다.
    template<typename VertexType>
    bool RayCast(const FPickingRay& LocalRay, const TArray<VertexType>& Vertices,
        const TArray<uint32>& Indices, float& OutHitT,
        FSpatialQueryStats* OutStats = nullptr, float MaxHitT = 1.0f) const
    {
        if (OutStats) *OutStats = {};
        if (!std::isfinite(MaxHitT) || MaxHitT < 0) return false;
        bool bHit = false;
        float NearestT = (std::min)(MaxHitT, 1.0f);
        FSpatialQueryStats Stats;
        Tree.VisitRay(LocalRay, Stats, [&](uint32 First, float& BestDistance)
        {
            float T, U, V;
            if (RayIntersectsTriangle(LocalRay.Near, LocalRay.Far,
                Vertices[Indices[First]].GetPosition(),
                Vertices[Indices[First + 1]].GetPosition(),
                Vertices[Indices[First + 2]].GetPosition(), T, U, V)
                && std::isfinite(T) && T <= NearestT)
            {
                bHit = true;
                NearestT = T;
                BestDistance = T * LocalRay.Length;
            }
        }, true, NearestT * LocalRay.Length);
        if (OutStats) *OutStats = Stats;
        if (bHit) OutHitT = NearestT;
        return bHit;
    }

    uint32 GetNodeCount() const { return Tree.GetNodeCount(); }
private:
    FSpatialOctree Tree;
};
