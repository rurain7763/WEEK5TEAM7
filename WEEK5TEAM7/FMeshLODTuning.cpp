#include "FMeshLODTuning.h"

#if ENABLE_MESH_LOD_TUNING
#include "UStaticMeshComponent.h"
#include "World.h"
#include "Actor.h"
#include "FInstrumentor.h"
#include "ImGui/imgui.h"
#include <algorithm>
#include <chrono>
#include <unordered_set>

void FLevelLODTuning::Render(UWorld& World, URenderer& Renderer)
{
    if (!ImGui::CollapsingHeader("Level LOD tuning")) return;
    ImGui::TextWrapped("Apply to all Static Mesh assets currently used in this level. Shared assets are processed once. Settings are temporary.");
    int Mode = Selection.ForcedLOD + 1;
    ImGui::Combo("Preview", &Mode, "Automatic\0LOD 0\0LOD 1\0LOD 2\0");
    Selection.ForcedLOD = Mode - 1;
    ImGui::DragFloat2("Transition distances", Selection.Distances, .5f, 0.f, 100000.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
    Selection.Distances[1] = (std::max)(Selection.Distances[0], Selection.Distances[1]);
    bool ApplySelection = ImGui::Button("Apply selection to level");
    for (int32 I = 0; I < 2; ++I)
    {
        ImGui::PushID(I);
        ImGui::Text("LOD %d", I + 1);
        ImGui::SliderFloat("Triangle ratio", &Generation.TriangleRatios[I], .01f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Allowed error", &Generation.MaxErrors[I], 0.f, 1.f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::PopID();
    }
    ImGui::InputInt3("Octree depths 0 / 1 / 2", Generation.OctreeDepths);
    for (int32& Depth : Generation.OctreeDepths) Depth = std::clamp(Depth, 0, 16);
    if (!Generation.IsValid()) ImGui::TextWrapped("LOD 2 ratio must not exceed LOD 1. Check the generation settings.");
    ImGui::BeginDisabled(!Generation.IsValid());
    bool Generate = ImGui::Button("Regenerate level LODs");
    ImGui::EndDisabled();
    bool TreesOnly = ImGui::Button("Rebuild level octrees only");
    ImGui::TextWrapped("Buttons apply the displayed values to the current level. Apply again after adding new mesh assets. Regeneration is synchronous; exclude that frame from measurements. Assets shared with other levels are affected too.");
    if (ApplySelection || Generate || TreesOnly)
    {
        const auto Start = std::chrono::steady_clock::now();
        // 매 프레임 5만 개 객체를 순회하지 않고 버튼을 누른 시점에만 수집합니다.
        TArray<TSharedPtr<FStaticMeshAsset>> Meshes;
        std::unordered_set<FStaticMeshAsset*> Seen;
        for (AActor* Actor : World.GetActors())
        {
            if (!Actor) continue;
            for (UActorComponent* Component : Actor->GetComponents())
            {
                if (!Component || !Component->IsA<UStaticMeshComponent>()) continue;
                auto Mesh = Component->Cast<UStaticMeshComponent>()->GetMesh();
                if (Mesh && Seen.insert(Mesh.get()).second) Meshes.Add(Mesh);
            }
        }
        MeshCount = static_cast<uint32>(Meshes.Num());
        FailureCount = 0;
        for (const auto& Mesh : Meshes)
        {
            if (ApplySelection || Generate) Mesh->SetLODSelection(Selection);
            // 실패한 에셋은 기존 데이터를 유지하며 다른 에셋은 계속 처리합니다.
            if (Generate && !Mesh->RebuildLODs(Renderer, Generation)) ++FailureCount;
            if (TreesOnly && !Mesh->RebuildLODOctrees(Generation)) ++FailureCount;
        }
        LastApplyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
        HasResult = true;
#if ENABLE_PROFILE
        // UI는 렌더링 뒤에 처리되므로 비교에는 마커 다음 프레임부터 사용합니다.
        if ((ApplySelection || Generate) && MeshCount > 0)
            FInstrumentor::Get().WriteLODMarker(Selection.ForcedLOD, MeshCount, FailureCount, Generate);
#endif
    }
    if (HasResult) ImGui::Text("Last operation: %u unique meshes | %u failures | %.2f ms", MeshCount, FailureCount, LastApplyMs);
}
#endif
