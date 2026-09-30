#pragma once

#include "Core.h"
#include "TArray.h"
#include "FMeshDescription.h"
#include "FAABB.h"
#include "RayCast.h"
#include "EngineMathLibrary.h"
#include <algorithm>
#include <cmath>

struct FMeshOctreeQueryStats
{
    uint32 VisitedNodes = 0;
    uint32 TestedEntries = 0;
    uint32 AcceptedEntries = 0;
};

// 기존 Octree의 구축·광선 탐색만 분리한 불변 메시용 트리입니다.
// 컴포넌트, 월드, 컬링, LOD 관리에는 의존하지 않습니다.
class FMeshPickingOctree
{
public:
    // 정점과 인덱스는 복제하지 않습니다. 원본 형상이 바뀌면 다시 구축해야 합니다.
    void Build(const TArray<FVertex>& Vertices, const TArray<uint32>& Indices, const FAABB& LocalBounds, uint32 MaxDepth = 8)
    {
        Nodes.Empty(); Entries.Empty(); EntryIndices.Empty();
        BuildMaxDepth = (std::min)(MaxDepth, uint32{32});
        TArray<uint32> Order;
        FAABB Bounds;
        for (uint32 I = 0; I + 2 < static_cast<uint32>(Indices.Num()); I += 3)
        {
            if (Indices[I] >= static_cast<uint32>(Vertices.Num())
                || Indices[I+1] >= static_cast<uint32>(Vertices.Num())
                || Indices[I+2] >= static_cast<uint32>(Vertices.Num())) continue;
            const FVector A = Vertices[Indices[I]].Pos;
            const FVector B = Vertices[Indices[I+1]].Pos;
            const FVector C = Vertices[Indices[I+2]].Pos;
            if (!Finite(A) || !Finite(B) || !Finite(C)) continue;
            FAABB Triangle(A, A);
            Triangle.ExpandToInclude(B); Triangle.ExpandToInclude(C);
            if (Order.IsEmpty()) Bounds = Triangle;
            else Expand(Bounds, Triangle);
            Order.Add(Entries.Add({ Triangle, I }));
        }
        if (Order.IsEmpty()) return;
        // 제공된 로컬 경계가 모든 삼각형을 포함하지 않으면 합집합 경계로 대체합니다.
        if (Valid(LocalBounds) && Contains(LocalBounds, Bounds)) Bounds = LocalBounds;
        BuildNode(Bounds, Order, 0);
    }

    // StaticMesh Picking에서 로컬 좌표로 변환한 Ray를 전달하는 조회 API입니다.
    // MaxHitT는 원래 Near~Far 구간의 비율이며 반환 T도 동일한 구간을 기준으로 합니다.
    bool RayCast(const FPickingRay& Ray, const TArray<FVertex>& Vertices, const TArray<uint32>& Indices, float& OutHitT,
                FMeshOctreeQueryStats* OutStats = nullptr, float MaxHitT = 1.0f) const
    {
        FMeshOctreeQueryStats Stats;
        if (OutStats) *OutStats = Stats;
        FPreparedRay Prepared;
        if (Nodes.IsEmpty() || !std::isfinite(MaxHitT) || MaxHitT < 0 || !Prepared.Initialize(Ray)) return false;
        float BestT = (std::min)(MaxHitT, 1.0f);
        float BestDistance = BestT * Ray.Length;
        bool Hit = false;
        auto Visit = [&](uint32 First)
        {
            float T, U, V;
            if (RayIntersectsTriangle(Ray.Near, Ray.Far, Vertices[Indices[First]].Pos,
                Vertices[Indices[First+1]].Pos, Vertices[Indices[First+2]].Pos, T, U, V)
                && std::isfinite(T) && T <= BestT)
            {
                BestT = T;
                BestDistance = T * Ray.Length;
                Hit = true;
            }
        };
        float Enter;
        ++Stats.VisitedNodes;
        if (RaySlab(Prepared, Nodes[0].ContentBounds, BestDistance, Enter))
            VisitNode(0, Prepared, BestDistance, Visit, Stats);
        if (OutStats) *OutStats = Stats;
        if (Hit) OutHitT = BestT;
        return Hit;
    }

    uint32 GetNodeCount() const { return static_cast<uint32>(Nodes.Num()); }
    uint32 GetTriangleCount() const { return static_cast<uint32>(Entries.Num()); }

private:
    static constexpr uint32 Invalid = ~uint32{0};
    static constexpr uint32 LeafCapacity = 32;
    struct FEntry { FAABB Bounds; uint32 FirstIndex; };
    struct FNode
    {
        FAABB ContentBounds;
        uint32 Children[8] = { Invalid, Invalid, Invalid, Invalid, Invalid, Invalid, Invalid, Invalid };
        uint32 FirstEntry = 0;
        uint32 EntryCount = 0;
    };
    struct FPreparedRay
    {
        FVector Origin;
        double InvDir[3];
        bool Parallel[3];
        bool Initialize(const FPickingRay& Ray)
        {
            if (!std::isfinite(Ray.Length) || Ray.Length <= 0
                || !Finite(Ray.Near) || !Finite(Ray.Far) || !Finite(Ray.Direction)) return false;
            Origin = Ray.Near;
            for (uint32 A = 0; A < 3; ++A)
            {
                Parallel[A] = Ray.Direction[A] == 0;
                InvDir[A] = Parallel[A] ? 0.0 : 1.0 / static_cast<double>(Ray.Direction[A]);
            }
            return !(Parallel[0] && Parallel[1] && Parallel[2]);
        }
    };
    static bool Finite(const FVector& V)
    {
        return std::isfinite(V.x) && std::isfinite(V.y) && std::isfinite(V.z);
    }
    static bool Valid(const FAABB& B)
    {
        return Finite(B.Min) && Finite(B.Max)
            && B.Min.x <= B.Max.x && B.Min.y <= B.Max.y && B.Min.z <= B.Max.z;
    }
    static bool Contains(const FAABB& A, const FAABB& B)
    {
        for (uint32 Axis = 0; Axis < 3; ++Axis)
            if (B.Min[Axis] < A.Min[Axis] || B.Max[Axis] > A.Max[Axis]) return false;
        return true;
    }
    static void Expand(FAABB& A, const FAABB& B)
    {
        A.ExpandToInclude(B.Min); A.ExpandToInclude(B.Max);
    }
    static FAABB ChildBounds(const FAABB& Parent, uint32 Child)
    {
        const FVector Center = Parent.Min * .5f + Parent.Max * .5f;
        FAABB Bounds = Parent;
        // 자식 번호의 각 비트가 해당 축의 위쪽 절반을 선택합니다.
        for (uint32 Axis = 0; Axis < 3; ++Axis)
            if ((Child & (1u << Axis)) != 0) Bounds.Min[Axis] = Center[Axis];
            else Bounds.Max[Axis] = Center[Axis];
        return Bounds;
    }
    uint32 BuildNode(const FAABB& Cell, const TArray<uint32>& Order, uint32 Depth)
    {
        FNode Node;
        Node.ContentBounds = Entries[Order[0]].Bounds;
        Node.FirstEntry = static_cast<uint32>(EntryIndices.Num());
        const uint32 Index = Nodes.Add(Node);
        if (static_cast<uint32>(Order.Num()) <= LeafCapacity || Depth >= BuildMaxDepth)
        {
            Nodes[Index].EntryCount = static_cast<uint32>(Order.Num());
            for (uint32 Entry : Order)
            {
                EntryIndices.Add(Entry);
                Expand(Nodes[Index].ContentBounds, Entries[Entry].Bounds);
            }
            return Index;
        }
        TArray<uint32> Buckets[8];
        FAABB Cells[8];
        for (uint32 C = 0; C < 8; ++C) Cells[C] = ChildBounds(Cell, C);
        for (uint32 Entry : Order)
        {
            uint32 Target = Invalid;
            for (uint32 C = 0; C < 8; ++C)
                if (Contains(Cells[C], Entries[Entry].Bounds)) { Target = C; break; }
            if (Target == Invalid)
            {
                // 경계에 걸친 삼각형은 중복 삽입하지 않고 부모에서 검사합니다.
                EntryIndices.Add(Entry);
                ++Nodes[Index].EntryCount;
                Expand(Nodes[Index].ContentBounds, Entries[Entry].Bounds);
            }
            else Buckets[Target].Add(Entry);
        }
        for (uint32 C = 0; C < 8; ++C)
        {
            if (Buckets[C].IsEmpty()) continue;
            const uint32 Child = BuildNode(Cells[C], Buckets[C], Depth + 1);
            // 재귀 중 노드 배열이 재할당되어도 인덱스는 유지됩니다.
            Nodes[Index].Children[C] = Child;
            Expand(Nodes[Index].ContentBounds, Nodes[Child].ContentBounds);
        }
        return Index;
    }
    static bool RaySlab(const FPreparedRay& Ray, const FAABB& B, float Limit, float& OutEnter)
    {
        double Enter = 0, Exit = Limit;
        for (uint32 Axis = 0; Axis < 3; ++Axis)
        {
            if (Ray.Parallel[Axis])
            {
                if (Ray.Origin[Axis] < B.Min[Axis] || Ray.Origin[Axis] > B.Max[Axis]) return false;
                continue;
            }
            double A = (static_cast<double>(B.Min[Axis]) - Ray.Origin[Axis]) * Ray.InvDir[Axis];
            double Z = (static_cast<double>(B.Max[Axis]) - Ray.Origin[Axis]) * Ray.InvDir[Axis];
            if (A > Z) std::swap(A, Z);
            Enter = (std::max)(Enter, A); Exit = (std::min)(Exit, Z);
            if (Enter > Exit) return false;
        }
        OutEnter = static_cast<float>(Enter);
        return true;
    }
    template<typename Visitor>
    void VisitNode(uint32 Index, const FPreparedRay& Ray, float& BestDistance, Visitor& Visit, FMeshOctreeQueryStats& Stats) const
    {
        const FNode& Node = Nodes[Index];
        for (uint32 I = 0; I < Node.EntryCount; ++I)
        {
            const FEntry& Entry = Entries[EntryIndices[Node.FirstEntry + I]];
            float Enter;
            ++Stats.TestedEntries;
            if (RaySlab(Ray, Entry.Bounds, BestDistance, Enter))
            {
                ++Stats.AcceptedEntries;
                Visit(Entry.FirstIndex);
            }
        }
        struct FChildHit { uint32 Index; float Enter; };
        FChildHit Hits[8];
        uint32 Count = 0;
        for (uint32 Child : Node.Children)
        {
            if (Child == Invalid) continue;
            float Enter;
            ++Stats.VisitedNodes;
            if (!RaySlab(Ray, Nodes[Child].ContentBounds, BestDistance, Enter)) continue;
            uint32 Position = Count++;
            while (Position > 0 && Hits[Position-1].Enter > Enter)
            {
                Hits[Position] = Hits[Position-1]; --Position;
            }
            Hits[Position] = { Child, Enter };
        }
        for (uint32 I = 0; I < Count; ++I)
        {
            if (Hits[I].Enter > BestDistance) break;
            VisitNode(Hits[I].Index, Ray, BestDistance, Visit, Stats);
        }
    }
    uint32 BuildMaxDepth = 8;
    TArray<FNode> Nodes;
    TArray<FEntry> Entries;
    TArray<uint32> EntryIndices;
};
