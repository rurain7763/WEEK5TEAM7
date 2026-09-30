#pragma once

#include "FMeshDescription.h"
#include <cmath>

// 모든 메시가 공유하는 고정 설정입니다. 변경 시 다시 빌드하고 에셋을 로드합니다.
namespace MeshLOD
{
    inline constexpr float TriangleRatios[2] = { 0.35f, 0.12f };
    inline constexpr float MaxErrors[2] = { 0.06f, 0.2f };
    inline constexpr uint32 OctreeDepths[3] = { 3, 2, 1 };
    inline constexpr float Distances[2] = { 30.0f, 60.0f };

    constexpr uint32 Select(float DistanceSquared)
    {
        if (DistanceSquared >= Distances[1] * Distances[1]) return 2;
        if (DistanceSquared >= Distances[0] * Distances[0]) return 1;
        return 0;
    }
}

// 원본 위치와 속성은 이동하지 않고 인덱스를 단순화한 뒤 사용 정점만 압축합니다.
// 따라서 모든 생성 LOD는 원본 AABB 안에 남습니다.
bool BuildSimplifiedMeshLOD(const FStaticMeshBuildData& Source, float Ratio, float MaxError,
    FStaticMeshBuildData& Out, float& OutError);
