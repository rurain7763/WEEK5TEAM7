#include "Camera.h"
#include "UStaticMeshComponent.h"
#include "FAssetManager.h"
#include "RenderInfo.h"
#include "ShowFlags.h"
#include "Actor.h"
#include "JsonUtil.h"
#include "EngineMathLibrary.h"
#include "FLogManager.h"
#include "World.h"

UStaticMeshComponent::UStaticMeshComponent()
{
    // 비교 실험용: 가시성과 무관하게 모든 정적 메시를 기존 Tickable 목록에 등록합니다.
    SetTickable(true);
}

void UStaticMeshComponent::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!mMeshAsset || !mOwner || !mOwner->GetWorld()) return;

    const uint32 LOD = GetLODForView(mOwner->GetWorld()->GetLODViewOrigin());
    // 같은 LOD의 버퍼를 편집 도구에서 재생성한 경우에도 프록시를 갱신합니다.
    if (mLODIndex != LOD || mLODMeshID != mMeshAsset->GetMeshID(LOD))
        SetMesh(mMeshAsset, LOD);
}

void UStaticMeshComponent::SerializeClass(json::JSON& outJson) const
{
    USceneComponent::SerializeClass(outJson);

    FGuid AssetID = mMeshAsset ? mMeshAsset->GetAssetID() : FGuid();
    outJson["Properties"]["ObjStaticMeshAsset"] = JsonUtils::ToJson(AssetID);

	TArray<FGuid> MaterialAssetIDs;
	for (int32 i = 0; i < mMaterialAssets.Num(); ++i)
	{
		const TSharedPtr<FMaterialAsset>& MaterialAsset = mMaterialAssets[i];
		FGuid MaterialAssetID = MaterialAsset ? MaterialAsset->GetAssetID() : FGuid();
		MaterialAssetIDs.Add(MaterialAssetID);
	}
	outJson["Properties"]["ObjMaterialAssets"] = JsonUtils::ToJson(MaterialAssetIDs);
	outJson["Properties"]["UVOffsets"] = JsonUtils::ToJson(mUVOffsets);
}

void UStaticMeshComponent::DeserializeClass(const json::JSON& inJson)
{
    USceneComponent::DeserializeClass(inJson);

    const json::JSON& PropertiesJson = inJson.at("Properties");

    if (!PropertiesJson.hasKey("ObjStaticMeshAsset"))
    {
        throw std::runtime_error("UStaticMeshComponent: ObjStaticMeshAsset property is required");
    }

    if (PropertiesJson.at("ObjStaticMeshAsset").JSONType() != json::JSON::Class::Object)
    {
        throw std::runtime_error("UStaticMeshComponent: ObjStaticMeshAsset property requires an object");
    }

    FGuid AssetID = JsonUtils::FromJson<FGuid>(PropertiesJson.at("ObjStaticMeshAsset"));

    if (AssetID.IsValid())
    {
        SetMesh(FAssetManager::Get().GetAssetAs<FStaticMeshAsset>(AssetID, true));
    }

	TArray<FGuid> MaterialAssetIDs;
	if (PropertiesJson.hasKey("ObjMaterialAssets"))
	{
		JsonUtils::FromJson(PropertiesJson.at("ObjMaterialAssets"), MaterialAssetIDs);
	}

	for (int32 i = 0; i < MaterialAssetIDs.Num(); ++i)
	{
		if (i >= mMaterialAssets.Num())
		{
            break;
		}

		mMaterialAssets[i] = FAssetManager::Get().GetAssetAs<FMaterialAsset>(MaterialAssetIDs[i], true);
	}

	TArray<FVector2> UVOffsets;
	if (PropertiesJson.hasKey("UVOffsets"))
	{
		JsonUtils::FromJson(PropertiesJson.at("UVOffsets"), UVOffsets);
	}

	for (int32 i = 0; i < UVOffsets.Num(); ++i)
	{
		if (i >= mUVOffsets.Num())
		{
			break;
		}

		mUVOffsets[i] = UVOffsets[i];
	}
}

void UStaticMeshComponent::Render(FRenderCollector& RenderCollector)
{
    Super::Render(RenderCollector);

    if (!mMeshAsset)
    {
        return;
    }

	const FTransform& Transform = GetTransform();
    // 거리 계산은 Tick에서 끝냈으므로 프록시 갱신 시에는 저장된 LOD만 사용합니다.
    const uint32 LOD = mLODIndex;
    const auto& Sections = mMeshAsset->GetSections(LOD);

	mRenderProxy->SetCollector(RenderCollector);
	mRenderProxy->ReserveRenderInfos(Sections.Num());

	int32 ActiveSectionCount = 0;
    for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
    {
        const FStaticMeshSection& Section = Sections[SectionIndex];
        if (Section.IndexCount == 0) continue;

        const FMaterialAsset* Material = mMaterialAssets[SectionIndex].get();

		FRenderInfo& RenderInfo = mRenderProxy->GetRenderInfo(ActiveSectionCount++);
        if (Material)
        {
		    const FVector& DiffuseColor = Material->GetDiffuseColor();
		    float Opacity = Material->GetOpacity();

            uint16 PipelineID = Material->GetPipelineID();
            uint32 MaterialID = Material->GetMaterialID();
            uint32 MeshID = mMeshAsset->GetMeshID(LOD);

            RenderInfo.SortKey = MakeRenderSortKey(PipelineID, MaterialID, MeshID);
            RenderInfo.Pipeline = Material->GetPipeline().get();
            RenderInfo.VertexBuffer = mMeshAsset->GetVertexBuffer(LOD);
            RenderInfo.IndexBuffer = mMeshAsset->GetIndexBuffer(LOD);
            RenderInfo.StartIndex = Section.FirstIndex;
            RenderInfo.IndexCount = Section.IndexCount;
            RenderInfo.Texture = Material->GetDiffuseTexture().get();
            RenderInfo.UVOffset = mUVOffsets[SectionIndex];
            RenderInfo.Model = Transform.MakeMatrix();
            RenderInfo.Color = FVector4(DiffuseColor.x, DiffuseColor.y, DiffuseColor.z, Opacity);
            RenderInfo.UseVertexColor = false;
            RenderInfo.ObjectInternalIndex = mOwner->InternalIndex;
        }
        else
        {
			RenderInfo.SortKey = MakeRenderSortKey(1, 0, mMeshAsset->GetMeshID(LOD));
			RenderInfo.Pipeline = nullptr;
			RenderInfo.VertexBuffer = mMeshAsset->GetVertexBuffer(LOD);
			RenderInfo.IndexBuffer = mMeshAsset->GetIndexBuffer(LOD);
			RenderInfo.StartIndex = Section.FirstIndex;
			RenderInfo.IndexCount = Section.IndexCount;
			RenderInfo.Texture = nullptr;
			RenderInfo.UVOffset = mUVOffsets[SectionIndex];
			RenderInfo.Model = Transform.MakeMatrix();
			RenderInfo.Color = Color;
			RenderInfo.UseVertexColor = true;
			RenderInfo.ObjectInternalIndex = mOwner->InternalIndex;
        }
    }

	mRenderProxy->SetActiveRenderInfoNum(ActiveSectionCount);
	mRenderedLODIndex = LOD;
}

bool UStaticMeshComponent::RayCastComponent(const FPickingRay& PickingRay, float& OutHitT, float MaxHitT) const
{
    if (!mMeshAsset) return false;

    const FMatrix& InvWorld = GetTransform().InverseMatrix();
    if (InvWorld == FMatrix::Zero) return false;

    // 에셋의 트리는 로컬 좌표계이므로 월드 Ray의 양 끝점을 역행렬로 변환합니다.
    // 변환된 끝점으로 방향과 길이를 다시 구하면 비균일·음수 스케일에도 대응합니다.
    const FPickingRay LocalRay(InvWorld.TransformPosition(PickingRay.Near),
        InvWorld.TransformPosition(PickingRay.Far));

    // 전체 삼각형 순회 대신 공유 트리에서 후보를 찾고 해당 삼각형만 검사합니다.
    // 반환 T는 원래 Near~Far 구간의 비율(0~1)이므로 호출자의 최단 거리 비교에 그대로 사용합니다.
    // 끝점을 줄이지 않고 T 상한만 전달합니다. 비균일 스케일에서도 같은 T가 같은 충돌점을 나타냅니다.
    // 로컬 트리는 MaxHitT * LocalRay.Length를 사용해 먼 노드와 삼각형 AABB를 즉시 제외합니다.
    return mMeshAsset->RayCastLocal(LocalRay, OutHitT, nullptr, MaxHitT, GetLODForView(PickingRay.ViewOrigin));
}

FAABB UStaticMeshComponent::GetBoundingBox() const
{
    if (!mMeshAsset)
    {
        return FAABB();
    }

    const FTransform& Transform = GetTransform();
    const uint32 CurrentTransformVersion = Transform.GetTransformVersion();
    if (mbAABBDirty || mCachedTransformVersion != CurrentTransformVersion)
    {
        mCachedWorldAABB = mMeshAsset->GetLocalBoundingBox().ToWorld(Transform.MakeMatrix());
        mCachedTransformVersion = CurrentTransformVersion;
        mbAABBDirty = false;
    }

    return mCachedWorldAABB;
}

void UStaticMeshComponent::SetMesh(const TSharedPtr<FStaticMeshAsset>& InMesh, uint32 LODIndex)
{
    const uint32 ResolvedLOD = InMesh && InMesh->HasLOD(LODIndex) ? LODIndex : 0;
    const uint32 MeshID = InMesh ? InMesh->GetMeshID(ResolvedLOD) : 0;
    if (mMeshAsset == InMesh)
    {
        if (mLODIndex == ResolvedLOD && mLODMeshID == MeshID) return;
        mLODIndex = ResolvedLOD;
        mLODMeshID = MeshID;
        MarkRenderDirty();
        return;
    }

    mLODIndex = ResolvedLOD;
    mLODMeshID = MeshID;
    mbAABBDirty = true;

    if (!InMesh)
    {
        mMeshAsset = nullptr;
        mMaterialAssets.Empty();
        mUVOffsets.Empty();
        mRenderProxy->ReleaseRenderInfos();
    }
    else
    {
        const auto& Sections = InMesh->GetSections();
        mMaterialAssets.SetNum(Sections.Num());
        mUVOffsets.SetNum(Sections.Num());
        for (int32 i = 0; i < Sections.Num(); i++)
        {
            auto& Section = Sections[i];
            mMaterialAssets[i] = Section.MaterialAssetID.IsValid() ? FAssetManager::Get().GetAssetAs<FMaterialAsset>(Section.MaterialAssetID, true) : nullptr;
        }
        mMeshAsset = InMesh;
    }

    MarkBoundsDirty();
    MarkRenderDirty();
}

uint32 UStaticMeshComponent::GetLODForView(const FVector& ViewOrigin) const
{
    if (!mMeshAsset) return 0;
    // 강제 선택은 카메라 거리와 무관하므로 중심 변환과 거리 계산을 생략합니다.
    if (mMeshAsset->GetLODSelection().ForcedLOD >= 0) return mMeshAsset->SelectLOD(0);
    const FAABB& Bounds = mMeshAsset->GetLocalBoundingBox();
    const FVector Center = GetTransform().MakeMatrix().TransformPosition(Bounds.Min * .5f + Bounds.Max * .5f);
    return mMeshAsset->SelectLOD((Center - ViewOrigin).LengthSquared());
}
