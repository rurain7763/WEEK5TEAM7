#pragma once

#include "Core.h"
#include "TArray.h"
#include "FFrustum.h"
#include "RayCast.h"
#include <algorithm>
#include <cmath>
#include <limits>

struct FSpatialEntry
{
    FAABB Bounds;
    uint32 ObjectIndex = 0;
};

struct FSpatialQueryStats
{
    uint32 VisitedNodes = 0;
    uint32 TestedEntries = 0;
    uint32 AcceptedEntries = 0;
};

// 범위는 트리의 EntryIndices를 참조하며 다음 재구축 전까지만 유효합니다.
struct FVisibleEntryRange
{
    uint32 First;
    uint32 Count;
};

struct FFrustumQueryResult
{
    bool bAllVisible = false;
    TArray<FVisibleEntryRange> Ranges;

    void Reset()
    {
        bAllVisible = false;
        Ranges.Empty();
    }
};

// 객체 수명은 월드가 관리합니다. 트리는 경계와 객체 목록의 인덱스만 보관합니다.
class FSpatialOctree
{
public:
    // GridSize는 객체 중심 배치의 기준 간격입니다. 0이면 기존 루트 크기 정책을 사용합니다.
    // 기본 1은 현재 테스트 씬의 월드 단위 격자에 맞춘 값이며 자동 추정값이 아닙니다.
    void Build(const TArray<FSpatialEntry>& InEntries, float GridSize = 1.0f)
    {
        Entries.Empty();
        // 유효하지 않은 경계는 등록하지 않습니다. 조회 중에는 다시 검증하지 않습니다.
        for (const FSpatialEntry& Entry : InEntries)
            if (IsValidBounds(Entry.Bounds)) Entries.Add(Entry);
        Nodes.Empty();
        EntryIndices.Empty();
        Root = Invalid;
        StraddlingEntryCount = 0;
        TArray<uint32> Indices;
        FAABB Bounds;
        for (uint32 I = 0; I < static_cast<uint32>(Entries.Num()); ++I)
        {
            if (Indices.IsEmpty()) Bounds = Entries[I].Bounds;
            else
            {
                Bounds.ExpandToInclude(Entries[I].Bounds.Min);
                Bounds.ExpandToInclude(Entries[I].Bounds.Max);
            }
            Indices.Add(I);
        }
        if (Indices.IsEmpty()) return;

        // 최소점에 고정한 정육면체로 평면 배치가 짧은 축의 중앙 분할면에 몰리는 것을 줄입니다.
        float Side = 0.001f;
        for (uint32 Axis = 0; Axis < 3; ++Axis)
            Side = (std::max)(Side, Bounds.Max[Axis] - Bounds.Min[Axis]);
        FAABB Cube = Bounds;
        for (uint32 Axis = 0; Axis < 3; ++Axis)
            Cube.Max[Axis] = (std::max)(Bounds.Max[Axis], Bounds.Min[Axis] + Side);
        // 극단적인 좌표에서 루트 확장이 넘치면 기존 합집합 경계를 사용합니다.
        if (!IsValidBounds(Cube)) Cube = Bounds;
        Cube = MakeAlignedRoot(Bounds, Cube, GridSize);
        Root = BuildNode(Cube, Indices, 0);
    }

    void QueryFrustumRanges(const FFrustum& Frustum, FFrustumQueryResult& Out,
        FSpatialQueryStats& Stats, bool bUseTree = true) const
    {
        Out.Reset();
        Stats = {};
        if (bUseTree)
        {
            if (Root != Invalid) QueryFrustumNode(Root, Frustum, Out, Stats);
        }
        else
        {
            // 비교용 선형 경로도 같은 범위 표현을 사용합니다.
            for (uint32 Position = 0; Position < static_cast<uint32>(EntryIndices.Num()); ++Position)
            {
                ++Stats.TestedEntries;
                if (Frustum.Intersects(Entries[EntryIndices[Position]].Bounds))
                    AddVisibleRange(Out, Stats, Position, 1);
            }
        }
    }

    // 전체 인덱스가 필요한 호출용입니다. 렌더링에서는 범위 조회를 직접 사용합니다.
    void QueryFrustum(const FFrustum& Frustum, TArray<uint32>& Out,
        FSpatialQueryStats& Stats, bool bUseTree = true) const
    {
        FFrustumQueryResult Result;
        QueryFrustumRanges(Frustum, Result, Stats, bUseTree);
        Out.Empty();
        if (Result.bAllVisible)
        {
            for (const FSpatialEntry& Entry : Entries) Out.Add(Entry.ObjectIndex);
        }
        else
        {
            for (const FVisibleEntryRange& Range : Result.Ranges)
                for (uint32 I = 0; I < Range.Count; ++I)
                    Out.Add(GetObjectIndexAt(Range.First + I));
        }
    }

    uint32 GetObjectIndexAt(uint32 Position) const
    {
        return Entries[EntryIndices[Position]].ObjectIndex;
    }

    void QueryRay(const FPickingRay& Ray, TArray<uint32>& Out,
        FSpatialQueryStats& Stats, bool bUseTree = true) const
    {
        Out.Empty();
        VisitRay(Ray, Stats, [&Out](uint32 Index, float&) { Out.Add(Index); }, bUseTree);
    }

    // 방문자는 실제 충돌을 확인한 경우에만 월드 단위 최단 거리를 줄입니다.
    template<typename Visitor>
    void VisitRay(const FPickingRay& Ray, FSpatialQueryStats& Stats,
        Visitor Visit, bool bUseTree = true) const
    {
        Stats = {};
        FRayPrecomp Prepared;
        if (!Prepared.Initialize(Ray)) return;
        float BestDistance = Ray.Length;
        if (bUseTree)
        {
            float Enter;
            if (Root != Invalid)
            {
                ++Stats.VisitedNodes;
                if (RaySlab(Prepared, Nodes[Root].ContentBounds, BestDistance, Enter))
                    VisitRayNode(Root, Prepared, BestDistance, Visit, Stats);
            }
        }
        else
        {
            for (const FSpatialEntry& Entry : Entries)
            {
                float Enter;
                ++Stats.TestedEntries;
                if (RaySlab(Prepared, Entry.Bounds, BestDistance, Enter))
                {
                    ++Stats.AcceptedEntries;
                    Visit(Entry.ObjectIndex, BestDistance);
                }
            }
        }
    }

    // 분할을 시도했지만 어느 자식에도 완전히 들어가지 못한 객체 수입니다.
    uint32 GetStraddlingEntryCount() const { return StraddlingEntryCount; }

    uint32 GetNodeCount() const { return static_cast<uint32>(Nodes.Num()); }
    uint32 GetEntryCount() const { return static_cast<uint32>(Entries.Num()); }

    static bool IsValidBounds(const FAABB& B)
    {
        if (B.IsUnset()) return false;
        for (uint32 Axis = 0; Axis < 3; ++Axis)
            if (!std::isfinite(B.Min[Axis]) || !std::isfinite(B.Max[Axis])
                || B.Min[Axis] > B.Max[Axis]) return false;
        return true;
    }

    static bool IntersectsRay(const FPickingRay& Ray, const FAABB& B)
    {
        FRayPrecomp Prepared;
        float Enter;
        return IsValidBounds(B) && Prepared.Initialize(Ray)
            && RaySlab(Prepared, B, Ray.Length, Enter);
    }

private:
    static constexpr uint32 Invalid = ~uint32{ 0 };
    static constexpr uint32 MaxDepth = 10;
    static constexpr int32 LeafCapacity = 32;
    struct FNode
    {
        // 조회용 경계입니다. 분할용 셀은 구축 인자로만 전달합니다.
        FAABB ContentBounds;
        uint32 Children[8] = { Invalid, Invalid, Invalid, Invalid, Invalid, Invalid, Invalid, Invalid };
        uint32 FirstEntry = 0;
        uint32 EntryCount = 0;
        // 깊이 우선 구축으로 자신과 모든 자손의 엔트리는 연속 범위에 놓입니다.
        uint32 SubtreeEntryCount = 0;
    };

    struct FRayPrecomp
    {
        FVector Origin;
        // 작은 방향 성분의 역수가 넘치지 않도록 중간 계산은 배정밀도를 사용합니다.
        double InvDir[3];
        bool Parallel[3];
        bool Initialize(const FPickingRay& Ray)
        {
            if (!std::isfinite(Ray.Length) || Ray.Length <= 0) return false;
            Origin = Ray.Near;
            for (uint32 Axis = 0; Axis < 3; ++Axis)
            {
                const float D = Ray.Direction[Axis];
                if (!std::isfinite(Origin[Axis]) || !std::isfinite(D)) return false;
                Parallel[Axis] = D == 0;
                InvDir[Axis] = Parallel[Axis] ? 0.0 : 1.0 / static_cast<double>(D);
            }
            return !(Parallel[0] && Parallel[1] && Parallel[2]);
        }
    };

    // 경계는 구축 시, 광선은 조회 시작 시 검증된 상태입니다.
    static bool RaySlab(const FRayPrecomp& Ray, const FAABB& Bounds, float Limit, float& OutEnter)
    {
        double Enter = 0, Exit = Limit;
        for (uint32 Axis = 0; Axis < 3; ++Axis)
        {
            if (Ray.Parallel[Axis])
            {
                if (Ray.Origin[Axis] < Bounds.Min[Axis] || Ray.Origin[Axis] > Bounds.Max[Axis]) return false;
                continue;
            }
            double A = (static_cast<double>(Bounds.Min[Axis]) - Ray.Origin[Axis]) * Ray.InvDir[Axis];
            double B = (static_cast<double>(Bounds.Max[Axis]) - Ray.Origin[Axis]) * Ray.InvDir[Axis];
            if (A > B) std::swap(A, B);
            Enter = (std::max)(Enter, A);
            Exit = (std::min)(Exit, B);
            if (Enter > Exit) return false;
        }
        OutEnter = static_cast<float>(Enter);
        return true;
    }

    static FAABB MakeAlignedRoot(const FAABB& Bounds, const FAABB& Fallback, float GridSize)
    {
        if (!std::isfinite(GridSize) || GridSize <= 0) return Fallback;
        const double Unit = GridSize;
        double Minimum[3];
        double RequiredSide = Unit;
        for (uint32 Axis = 0; Axis < 3; ++Axis)
        {
            // 정수 배치 중심 사이의 반 칸 경계로 내립니다. 음수 좌표도 floor로 처리합니다.
            Minimum[Axis] = (std::floor(static_cast<double>(Bounds.Min[Axis]) / Unit + 0.5) - 0.5) * Unit;
            RequiredSide = (std::max)(RequiredSide, static_cast<double>(Bounds.Max[Axis]) - Minimum[Axis]);
        }
        // 기준 간격에 도달할 때까지 반으로 나눈 길이가 간격의 정수 배수가 되도록 확장합니다.
        double Side = Unit;
        while (Side < RequiredSide) Side *= 2;
        FAABB Result;
        for (uint32 Axis = 0; Axis < 3; ++Axis)
        {
            const double Maximum = Minimum[Axis] + Side;
            // float로 표현할 수 없는 극단적인 좌표에서는 기존 안전한 루트를 유지합니다.
            if (Minimum[Axis] < -(std::numeric_limits<float>::max)()
                || Maximum > (std::numeric_limits<float>::max)()) return Fallback;
            Result.Min[Axis] = static_cast<float>(Minimum[Axis]);
            Result.Max[Axis] = static_cast<float>(Maximum);
        }
        // 반올림 때문에 객체 일부가 루트 밖으로 나가면 정렬을 포기합니다. 객체 경계는 줄이지 않습니다.
        return IsValidBounds(Result) && Contains(Result, Bounds) ? Result : Fallback;
    }

    static bool Contains(const FAABB& Outer, const FAABB& Inner)
    {
        for (uint32 Axis = 0; Axis < 3; ++Axis)
            if (Inner.Min[Axis] < Outer.Min[Axis] || Inner.Max[Axis] > Outer.Max[Axis]) return false;
        return true;
    }

    static FAABB MakeChildBounds(const FAABB& Parent, uint32 Child)
    {
        const FVector Center = Parent.Min * 0.5f + Parent.Max * 0.5f;
        FAABB Result = Parent;
        // 자식 번호에서 각 축의 위쪽 절반을 명시적으로 선택합니다.
        const bool bUpperX = Child == 1 || Child == 3 || Child == 5 || Child == 7;
        const bool bUpperY = Child == 2 || Child == 3 || Child == 6 || Child == 7;
        const bool bUpperZ = Child >= 4;
        if (bUpperX) Result.Min.x = Center.x; else Result.Max.x = Center.x;
        if (bUpperY) Result.Min.y = Center.y; else Result.Max.y = Center.y;
        if (bUpperZ) Result.Min.z = Center.z; else Result.Max.z = Center.z;
        return Result;
    }

    static void ExpandBounds(FAABB& Target, const FAABB& Other)
    {
        Target.ExpandToInclude(Other.Min);
        Target.ExpandToInclude(Other.Max);
    }

    uint32 BuildNode(const FAABB& CellBounds, const TArray<uint32>& Indices, uint32 Depth)
    {
        FNode Node;
        // 생성되는 노드는 항상 하나 이상의 유효 엔트리를 포함합니다.
        Node.ContentBounds = Entries[Indices[0]].Bounds;
        Node.SubtreeEntryCount = static_cast<uint32>(Indices.Num());
        const uint32 Index = Nodes.Add(Node);
        if (Indices.Num() <= LeafCapacity || Depth >= MaxDepth)
        {
            Nodes[Index].FirstEntry = static_cast<uint32>(EntryIndices.Num());
            Nodes[Index].EntryCount = static_cast<uint32>(Indices.Num());
            for (uint32 I : Indices)
            {
                EntryIndices.Add(I);
                ExpandBounds(Nodes[Index].ContentBounds, Entries[I].Bounds);
            }
            return Index;
        }
        Nodes[Index].FirstEntry = static_cast<uint32>(EntryIndices.Num());
        TArray<uint32> Buckets[8];
        FAABB ChildBounds[8];
        for (uint32 Child = 0; Child < 8; ++Child) ChildBounds[Child] = MakeChildBounds(CellBounds, Child);
        for (uint32 EntryIndex : Indices)
        {
            int32 Target = -1;
            for (uint32 Child = 0; Child < 8; ++Child)
                if (Contains(ChildBounds[Child], Entries[EntryIndex].Bounds))
                {
                    Target = static_cast<int32>(Child);
                    break;
                }
            if (Target < 0)
            {
                EntryIndices.Add(EntryIndex);
                ++Nodes[Index].EntryCount;
                ++StraddlingEntryCount;
                ExpandBounds(Nodes[Index].ContentBounds, Entries[EntryIndex].Bounds);
            }
            else Buckets[Target].Add(EntryIndex);
        }
        for (uint32 Child = 0; Child < 8; ++Child)
        {
            if (Buckets[Child].IsEmpty()) continue;
            const uint32 ChildIndex = BuildNode(ChildBounds[Child], Buckets[Child], Depth + 1);
            // 재귀 중 재할당될 수 있으므로 노드 참조 대신 인덱스를 사용합니다.
            Nodes[Index].Children[Child] = ChildIndex;
            // 자식이 계산한 합집합을 재사용하므로 하위 엔트리를 다시 순회하지 않습니다.
            ExpandBounds(Nodes[Index].ContentBounds, Nodes[ChildIndex].ContentBounds);
        }
        return Index;
    }

    static void AddVisibleRange(FFrustumQueryResult& Out, FSpatialQueryStats& Stats,
        uint32 First, uint32 Count)
    {
        Stats.AcceptedEntries += Count;
        // 인접 범위는 하나로 합쳐 부분 가시 결과의 관리 비용을 줄입니다.
        if (!Out.Ranges.IsEmpty() && Out.Ranges.Last().First + Out.Ranges.Last().Count == First)
            Out.Ranges.Last().Count += Count;
        else
            Out.Ranges.Add({ First, Count });
    }

    void QueryFrustumNode(uint32 Index, const FFrustum& Frustum,
        FFrustumQueryResult& Out, FSpatialQueryStats& Stats) const
    {
        ++Stats.VisitedNodes;
        const FNode& Node = Nodes[Index];
        bool bFullyInside;
        if (!Frustum.Intersects(Node.ContentBounds, bFullyInside)) return;
        if (bFullyInside)
        {
            if (Index == Root)
            {
                // 루트 완전 포함은 엔트리 순회나 결과 배열 작성 없이 끝납니다.
                Out.bAllVisible = true;
                Stats.AcceptedEntries = Node.SubtreeEntryCount;
            }
            else
                AddVisibleRange(Out, Stats, Node.FirstEntry, Node.SubtreeEntryCount);
            return;
        }
        for (uint32 Offset = 0; Offset < Node.EntryCount; ++Offset)
        {
            const uint32 Position = Node.FirstEntry + Offset;
            ++Stats.TestedEntries;
            if (Frustum.Intersects(Entries[EntryIndices[Position]].Bounds))
                AddVisibleRange(Out, Stats, Position, 1);
        }
        for (uint32 Child : Node.Children)
            if (Child != Invalid) QueryFrustumNode(Child, Frustum, Out, Stats);
    }

    template<typename Visitor>
    void VisitRayNode(uint32 Index, const FRayPrecomp& Ray, float& BestDistance,
        Visitor& Visit, FSpatialQueryStats& Stats) const
    {
        const FNode& Node = Nodes[Index];
        for (uint32 Offset = 0; Offset < Node.EntryCount; ++Offset)
        {
            const FSpatialEntry& Entry = Entries[EntryIndices[Node.FirstEntry + Offset]];
            float Enter;
            ++Stats.TestedEntries;
            if (RaySlab(Ray, Entry.Bounds, BestDistance, Enter))
            {
                ++Stats.AcceptedEntries;
                Visit(Entry.ObjectIndex, BestDistance);
            }
        }
        // 최대 8개 자식만 스택에 저장하고, 계산한 진입 거리를 재사용합니다.
        struct FChildHit { uint32 Index; float Enter; };
        FChildHit Hits[8];
        uint32 Count = 0;
        for (uint32 Child : Node.Children)
        {
            if (Child == Invalid) continue;
            ++Stats.VisitedNodes;
            float Enter;
            if (!RaySlab(Ray, Nodes[Child].ContentBounds, BestDistance, Enter)) continue;
            uint32 Position = Count++;
            while (Position > 0 && Hits[Position - 1].Enter > Enter)
            {
                Hits[Position] = Hits[Position - 1];
                --Position;
            }
            Hits[Position] = { Child, Enter };
        }
        for (uint32 I = 0; I < Count; ++I)
        {
            // 남은 형제들의 진입 거리도 더 크므로 이 노드의 순회를 끝냅니다.
            if (Hits[I].Enter > BestDistance) break;
            VisitRayNode(Hits[I].Index, Ray, BestDistance, Visit, Stats);
        }
    }

    TArray<FSpatialEntry> Entries;
    TArray<FNode> Nodes;
    TArray<uint32> EntryIndices;
    uint32 Root = Invalid;
    uint32 StraddlingEntryCount = 0;
};
