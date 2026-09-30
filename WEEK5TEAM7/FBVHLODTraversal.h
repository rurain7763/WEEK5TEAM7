#pragma once

#include "FBVH.h"
#include "FFrustum.h"
#include "FMeshLODBuilder.h"

// 공간 트리와 분리된 월드의 LOD 상태입니다. 값은 하위 컴포넌트에 요청한 상태를 나타냅니다.
struct FBVHLODNodeState
{
    static constexpr int32 Unknown = -2;
    static constexpr int32 Mixed = -1;
    FMeshLODSelection Selection;
    uint64 BoundsRevision = 0;
    uint64 SettingsRevision = 0;
    bool bHasMeshes = false;
    bool bUniformSelection = true;
    int32 RequestedLOD = Unknown;
    // 부모에서 범위 전체를 바꿨다면 자식 캐시는 다음에 내려갈 때 동기화합니다.
    bool bPropagateLOD = false;
};

struct FBVHLODQueryStats
{
    uint32 VisitedNodes = 0;
    uint32 FrustumTests = 0;
    uint32 LODTests = 0;
    uint32 ReusedRanges = 0;
    uint32 ChangedComponents = 0;
};

struct FBVHLODTraversal
{
    static void PropagateLOD(const FBVHNode* Node, TArray<FBVHLODNodeState>& States)
    {
        auto& State = States[Node->Index];
        if (!State.bPropagateLOD) return;
        for (const FBVHNode* Child : { Node->Left, Node->Right })
        {
            if (!Child) continue;
            auto& ChildState = States[Child->Index];
            ChildState.RequestedLOD = State.RequestedLOD;
            ChildState.bPropagateLOD = !Child->IsLeaf();
        }
        State.bPropagateLOD = false;
    }

    static bool SameSelection(const FMeshLODSelection& A, const FMeshLODSelection& B)
    {
        return A.ForcedLOD == B.ForcedLOD && A.Distances[0] == B.Distances[0]
            && A.Distances[1] == B.Distances[1];
    }

    // Refit된 경로만 재요약합니다. 설정 버전이 바뀐 편집 시점에는 전체 요약을 갱신합니다.
    template <typename ReadSelectionFunc>
    static void Refresh(const FBVHNode* Node, TArray<FBVHLODNodeState>& States,
        uint64 SettingsRevision, ReadSelectionFunc& ReadSelection)
    {
        if (!Node) return;
        auto& State = States[Node->Index];
        if (State.BoundsRevision == Node->Revision && State.SettingsRevision == SettingsRevision) return;
        // 일부 리프가 변해도 나머지 자식이 부모의 마지막 적용값을 잃지 않도록 먼저 전달합니다.
        PropagateLOD(Node, States);
        State.RequestedLOD = FBVHLODNodeState::Unknown;
        State.BoundsRevision = Node->Revision;
        State.SettingsRevision = SettingsRevision;
        State.bUniformSelection = true;
        if (Node->IsLeaf())
        {
            State.bHasMeshes = ReadSelection(Node->ItemRange.Offset, State.Selection);
            return;
        }
        Refresh(Node->Left, States, SettingsRevision, ReadSelection);
        Refresh(Node->Right, States, SettingsRevision, ReadSelection);
        const auto& Left = States[Node->Left->Index];
        const auto& Right = States[Node->Right->Index];
        State.bHasMeshes = Left.bHasMeshes || Right.bHasMeshes;
        State.Selection = Left.bHasMeshes ? Left.Selection : Right.Selection;
        State.bUniformSelection = (!Left.bHasMeshes || Left.bUniformSelection)
            && (!Right.bHasMeshes || Right.bUniformSelection)
            && (!Left.bHasMeshes || !Right.bHasMeshes || SameSelection(Left.Selection, Right.Selection));
    }

    // 상자 안 모든 중심점이 같은 거리 구간이면 일괄 선택하고, 경계에 걸치면 Mixed를 반환합니다.
    static int32 ClassifyLOD(const FAABB& Bounds, const FVector& ViewOrigin,
        const FMeshLODSelection& Selection, bool bLeaf)
    {
        if (Selection.ForcedLOD >= 0) return static_cast<int32>(Selection.Select(0));
        if (bLeaf)
        {
            const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
            return static_cast<int32>(Selection.Select((Center - ViewOrigin).LengthSquared()));
        }

        float MinDistanceSquared = 0;
        float MaxDistanceSquared = 0;
        for (uint32 Axis = 0; Axis < 3; ++Axis)
        {
            const float ToMin = Bounds.Min[Axis] - ViewOrigin[Axis];
            const float ToMax = Bounds.Max[Axis] - ViewOrigin[Axis];
            const float Near = (std::max)(0.0f, (std::max)(ToMin, -ToMax));
            const float Far = (std::max)(fabsf(ToMin), fabsf(ToMax));
            MinDistanceSquared += Near * Near;
            MaxDistanceSquared += Far * Far;
        }
        const uint32 NearLOD = Selection.Select(MinDistanceSquared);
        const uint32 FarLOD = Selection.Select(MaxDistanceSquared);
        return NearLOD == FarLOD ? static_cast<int32>(NearLOD) : FBVHLODNodeState::Mixed;
    }

    // SubmitRange는 매 프레임 호출됩니다. LOD가 음수면 프록시 제출만 수행합니다.
    template <typename SubmitRangeFunc>
    static void Query(const FBVHNode* Node, TArray<FBVHLODNodeState>& States,
        const FFrustum& Frustum, const FVector& ViewOrigin, FBVHLODQueryStats& Stats,
        SubmitRangeFunc& SubmitRange, bool bUpdateLOD, bool bInside = false,
        int32 InheritedLOD = FBVHLODNodeState::Mixed)
    {
        if (!Node) return;
        ++Stats.VisitedNodes;
        if (!bInside) ++Stats.FrustumTests;
        const int32 Visibility = bInside ? 1 : Frustum.Intersects(Node->BoundingBox);
        if (Visibility == -1) return;
        bInside = Visibility == 1;

        FBVHLODNodeState& State = States[Node->Index];
        bUpdateLOD = bUpdateLOD && State.bHasMeshes;
        int32 DesiredLOD = InheritedLOD;
        if (bUpdateLOD && DesiredLOD < 0 && State.bUniformSelection)
        {
            ++Stats.LODTests;
            DesiredLOD = ClassifyLOD(Node->BoundingBox, ViewOrigin, State.Selection, Node->IsLeaf());
        }

        if (bUpdateLOD && DesiredLOD >= 0 && State.RequestedLOD == DesiredLOD)
        {
            // 현재 거리 판정도 같을 때만 생략합니다. 카메라가 움직여도 과거 결과만 믿지 않습니다.
            ++Stats.ReusedRanges;
            bUpdateLOD = false;
        }

        if ((bInside || Node->IsLeaf()) && (!bUpdateLOD || DesiredLOD >= 0))
        {
            SubmitRange(Node->ItemRange, bUpdateLOD ? DesiredLOD : FBVHLODNodeState::Mixed);
            if (bUpdateLOD)
            {
                State.RequestedLOD = DesiredLOD;
                State.bPropagateLOD = !Node->IsLeaf();
            }
            return;
        }

        if (bUpdateLOD && State.bPropagateLOD)
        {
            // 이전에 부모 범위로 적용한 값을 늦게 전달해 오래된 자식 캐시가 되살아나는 것을 막습니다.
            PropagateLOD(Node, States);
        }

        Query(Node->Left, States, Frustum, ViewOrigin, Stats, SubmitRange, bUpdateLOD, bInside, DesiredLOD);
        Query(Node->Right, States, Frustum, ViewOrigin, Stats, SubmitRange, bUpdateLOD, bInside, DesiredLOD);

        if (bUpdateLOD)
        {
            const auto& Left = States[Node->Left->Index];
            const auto& Right = States[Node->Right->Index];
            if (!Left.bHasMeshes) State.RequestedLOD = Right.RequestedLOD;
            else if (!Right.bHasMeshes) State.RequestedLOD = Left.RequestedLOD;
            else if (Left.RequestedLOD == Right.RequestedLOD) State.RequestedLOD = Left.RequestedLOD;
            else State.RequestedLOD = FBVHLODNodeState::Mixed;
        }
    }
};
