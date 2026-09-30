#pragma once
#include "FMeshLODBuilder.h"

#if ENABLE_MESH_LOD_TUNING
class UWorld;
class URenderer;

// 레벨 전체에 적용할 편집값입니다. 메시 목록은 적용할 때만 수집합니다.
struct FLevelLODTuning
{
    FMeshLODSettings Generation;
    FMeshLODSelection Selection;
    uint32 MeshCount = 0;
    uint32 FailureCount = 0;
    double LastApplyMs = 0;
    bool HasResult = false;
    void Render(UWorld& World, URenderer& Renderer);
};
#endif
