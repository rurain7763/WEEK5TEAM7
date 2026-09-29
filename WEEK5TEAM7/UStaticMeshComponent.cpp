#include "UStaticMeshComponent.h"
#include "FAssetManager.h"
#include "RenderInfo.h"
#include "ShowFlags.h"
#include "Actor.h"
#include "JsonUtil.h"
#include "EngineMathLibrary.h"
#include "FLogManager.h"

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
    if (!mMeshAsset)
    {
        return;
    }

    if (!FShowFlags::Get().IsEnabled(EShowFlag::Primitive))
    {
        return;
    }

	const FTransform& Transform = GetTransform();

    for (int32 SectionIndex = 0; SectionIndex < mMeshAsset->GetSections().Num(); ++SectionIndex)
    {
        const FStaticMeshSection& Section = mMeshAsset->GetSections()[SectionIndex];

        TSharedPtr<FMaterialAsset> Material = mMaterialAssets[SectionIndex];

        uint16 PipelineID = Material ? Material->GetPipelineID() : 1;
        uint32 MaterialID = Material ? Material->GetMaterialID() : 0;
        uint32 MeshID = mMeshAsset ? mMeshAsset->GetMeshID() : 0;

        FRenderInfo& RenderInfo = RenderCollector.RenderInfos.Emplace();
        RenderInfo.SortKey = MakeRenderSortKey(PipelineID, MaterialID, MeshID);
        RenderInfo.Pipeline = Material ? Material->GetPipeline() : nullptr;
        RenderInfo.VertexBuffer = mMeshAsset->GetVertexBuffer();
        RenderInfo.IndexBuffer = mMeshAsset->GetIndexBuffer();
        RenderInfo.StartIndex = Section.FirstIndex;
        RenderInfo.IndexCount = Section.IndexCount;
        RenderInfo.Texture = Material ? Material->GetDiffuseTexture() : nullptr;
        RenderInfo.UVOffset = mUVOffsets[SectionIndex];
        RenderInfo.Model = Transform.MakeMatrix();
        RenderInfo.Color = Material ? FVector4(Material->GetDiffuseColor().x, Material->GetDiffuseColor().y, Material->GetDiffuseColor().z, Material->GetOpacity()) : Color;
        RenderInfo.UseVertexColor = Material == nullptr;
        RenderInfo.ObjectInternalIndex = mOwner->InternalIndex;
    }
}

bool UStaticMeshComponent::RayCastComponent(const FPickingRay& PickingRay, float& OutHitT) const
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
    // 향후 외부 BVH가 최단 거리를 제공하면 네 번째 인자로 BestWorldDistance / PickingRay.Length를 전달합니다.
    return mMeshAsset->RayCastLocal(LocalRay, OutHitT);
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

void UStaticMeshComponent::SetMesh(const TSharedPtr<FStaticMeshAsset>& InMesh)
{
    mbAABBDirty = true;

    if (!InMesh)
    {
		mMeshAsset = nullptr;
		mMaterialAssets.Empty();
		mUVOffsets.Empty();
        MarkBoundsDirty();
		return;
    }

    const auto& Sections = InMesh->GetSections();
    mMaterialAssets.SetNum(Sections.Num());
    mUVOffsets.SetNum(Sections.Num());
    for (int32 i = 0; i < Sections.Num(); i++)
    {
        auto& Section = Sections[i];
        mMaterialAssets[i] = FAssetManager::Get().GetAssetAs<FMaterialAsset>(Section.MaterialAssetID, true);
    }
    mMeshAsset = InMesh;

    MarkBoundsDirty();
}
