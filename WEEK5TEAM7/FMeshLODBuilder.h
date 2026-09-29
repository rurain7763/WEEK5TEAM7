#pragma once

#include "FMeshDescription.h"
#include <cmath>

// 최종 빌드에서 조정 UI와 에셋의 편집용 설정을 제거할 수 있는 스위치입니다.
#ifndef ENABLE_MESH_LOD_TUNING
#define ENABLE_MESH_LOD_TUNING 1
#endif

struct FMeshLODSettings
{
    float TriangleRatios[2] = { 0.5f, 0.2f };
    float MaxErrors[2] = { 0.01f, 0.03f };
    int32 OctreeDepths[3] = { 8, 4, 1 };

    bool IsValid() const
    {
        for (uint32 I = 0; I < 2; ++I)
            if (!std::isfinite(TriangleRatios[I]) || TriangleRatios[I] <= 0 || TriangleRatios[I] > 1
                || !std::isfinite(MaxErrors[I]) || MaxErrors[I] < 0 || MaxErrors[I] > 1) return false;
        if (TriangleRatios[1] > TriangleRatios[0]) return false;
        for (int32 Depth : OctreeDepths) if (Depth < 0 || Depth > 16) return false;
        return true;
    }
};

struct FMeshLODSelection
{
    int32 ForcedLOD = -1;
    float Distances[2] = { 15.0f, 35.0f };
    uint32 Select(float DistanceSquared) const
    {
        if (ForcedLOD >= 0 && ForcedLOD <= 2) return static_cast<uint32>(ForcedLOD);
        if (DistanceSquared >= Distances[1] * Distances[1]) return 2;
        if (DistanceSquared >= Distances[0] * Distances[0]) return 1;
        return 0;
    }
};

// 원본 위치와 속성은 이동하지 않고 인덱스를 단순화한 뒤 사용 정점만 압축합니다.
// 따라서 모든 생성 LOD는 원본 AABB 안에 남습니다.
bool BuildSimplifiedMeshLOD(const FStaticMeshBuildData& Source, float Ratio, float MaxError,
    FStaticMeshBuildData& Out, float& OutError);
