#include "FPropertyWindow.h"
#include "Vector.h"
#include "ImGui/imgui.h"
#include "Actor.h"
#include "Transform.h"
#include "UTextComponent.h"
#include "UAtlasAnimationComponent.h"
#include "FLogManager.h"
#include "UStaticMeshComponent.h"
#include "FAssetManager.h"
#include "AssetDragDrop.h"
#include "FEditorUIManager.h"
#include "SceneManager.h"
#include "Assets.h"

void FPropertyWindow::Render(const FGuiReference& GuiReference)
{
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

	ImGui::Begin("Jungle Property Window", nullptr, flags);

	AActor* TargetActor = GuiReference.SceneManager->GetSelectedActor();

	if (TargetActor)
	{
		mAssetManager = GuiReference.AssetManager;

		RenderTransformProperties(TargetActor);

		for (UActorComponent* component : TargetActor->GetComponents())
		{
			ImGui::SeparatorText(component->GetClass()->Name.c_str());

			if (component->IsA<UText3DComponent>())
			{
				RenderText3DComponent(component->Cast<UText3DComponent>());
			}
			else if (component->IsA<USpotLightComponent>())
			{
				RenderSpotLightComponent(component->Cast<USpotLightComponent>());
			}
			else if (component->IsA< UAtlasAnimationComponent>())
			{
				RenderAtlasAnimationComponent(component->Cast<UAtlasAnimationComponent>());
			}
			else if (component->IsA<UStaticMeshComponent>())
			{
				RenderStaticMeshComponent(component->Cast<UStaticMeshComponent>());
			}
		}
	}

	ImGui::End();
}

void FPropertyWindow::RenderTransformProperties(AActor* TargetActor)
{
	FTransform OriginalTransform = TargetActor->GetTransform();
	FVector translationInput = OriginalTransform.GetLocation();
	FVector rotationInput = {
		OriginalTransform.GetRotation().Roll,
		OriginalTransform.GetRotation().Pitch,
		OriginalTransform.GetRotation().Yaw
	};
	FVector scaleInput = OriginalTransform.GetScale();

	if (ImGui::DragFloat3("Translation", &translationInput.x, 0.1f))
	{
		TargetActor->SetLocation(translationInput);
	}

	if (ImGui::DragFloat3("Rotation", &rotationInput.x, 0.1f))
	{
		TargetActor->SetRotation({
			rotationInput.y, // Pitch
			rotationInput.z, // Yaw
			rotationInput.x  // Roll
			});
	}

	if (ImGui::DragFloat3("Scale", &scaleInput.x, 0.1f, MIN_SCALE, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp))
	{
		TargetActor->SetScale(scaleInput);
	}
}

void FPropertyWindow::RenderText3DComponent(UText3DComponent* text3DComponent)
{
	char textBuffer[256] = {};
	const FString currentText = Wide2Utf(text3DComponent->GetText());
	strncpy_s(textBuffer, currentText.CStr(), sizeof(textBuffer) - 1);

	if (ImGui::InputText("Display Text", textBuffer, sizeof(textBuffer)))
	{
		text3DComponent->SetText(Utf2Wide(FString(textBuffer)));
	}
}

void FPropertyWindow::RenderSpotLightComponent(USpotLightComponent* spotLightComponent)
{
	FVector4 colorInput = spotLightComponent->GetColor();
	if (ImGui::ColorPicker3("Color", &colorInput.x,
		ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_DisplayHex))
	{
		spotLightComponent->SetColor(colorInput);
	}

	float innerAngleInput = spotLightComponent->GetInnerConeAngle();
	if (ImGui::DragFloat("InnerAngle", &innerAngleInput, 0.1f, 0.f, spotLightComponent->GetOuterConeAngle(), "%.3f", ImGuiSliderFlags_AlwaysClamp))
	{
		spotLightComponent->SetInnerConeAngle(innerAngleInput);
	}

	float outerAngleInput = spotLightComponent->GetOuterConeAngle();
	if (ImGui::DragFloat("OuterAngle", &outerAngleInput, 0.1f, 0.f, 89.f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
	{
		spotLightComponent->SetOuterConeAngle(outerAngleInput);
	}
}

void FPropertyWindow::RenderAtlasAnimationComponent(UAtlasAnimationComponent* atlasAnimationComponent)
{
	TArray<FString> spriteAtlasAssetNames;
	mAssetManager->ForEachMetaInfo([&spriteAtlasAssetNames](const FAssetMetaInfo& metaInfo) {
		if (metaInfo.AssetType != EAssetType::SpriteAtlas)
		{
			return;
		}
		spriteAtlasAssetNames.Add(metaInfo.AssetName.ToString());
		});

	const TSharedPtr<FSpriteAtlasAsset>& currentAtlas = atlasAnimationComponent->GetAtlas();
	FString currentAtlasName = currentAtlas ? currentAtlas->GetAssetName().ToString() : "None";
	if (ImGui::BeginCombo("Sprite Atlas", currentAtlasName.CStr()))
	{
		for (const FString& assetName : spriteAtlasAssetNames)
		{
			bool isSelected = (currentAtlasName == assetName);
			if (ImGui::Selectable(assetName.CStr(), isSelected))
			{
				atlasAnimationComponent->SetAtlas(mAssetManager->GetAssetAs<FSpriteAtlasAsset>(FName(assetName), true));
			}
			if (isSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	if (ImGui::Button("Play"))
	{
		atlasAnimationComponent->Play(0, atlasAnimationComponent->IsLooping(), atlasAnimationComponent->IsBackward());
	}
	ImGui::SameLine();
	if (ImGui::Button("Pause"))
	{
		atlasAnimationComponent->Pause();
	}
	ImGui::SameLine();
	if (ImGui::Button("Resume"))
	{
		atlasAnimationComponent->Resume();
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset"))
	{
		atlasAnimationComponent->Reset();
	}

	ImGui::Text(atlasAnimationComponent->IsPlaying() ? "State: Playing" : "State: Stopped");

	bool loopInput = atlasAnimationComponent->IsLooping();
	if (ImGui::Checkbox("bLoop", &loopInput))
	{
		atlasAnimationComponent->SetLooping(loopInput);
	}

	int32 frameRateInput = atlasAnimationComponent->GetFrameRate();
	if (ImGui::DragInt("FrameRate", &frameRateInput, 1.f, 1, 240, "%d", ImGuiSliderFlags_AlwaysClamp))
	{
		atlasAnimationComponent->SetFrameRate(frameRateInput);
	}
}

void FPropertyWindow::RenderStaticMeshComponent(UStaticMeshComponent* StaticMeshComponent)
{
	TSharedPtr<FStaticMeshAsset> CurrentStaticMesh = StaticMeshComponent->GetMesh();

	TArray<FString> StaticMeshAssetNames;
	TArray<FAssetMetaInfo> materialMetaInfos;
	mAssetManager->ForEachMetaInfo([&StaticMeshAssetNames, &materialMetaInfos](const FAssetMetaInfo& metaInfo) {
		if (metaInfo.AssetType == EAssetType::StaticMesh)
		{
			StaticMeshAssetNames.Add(metaInfo.AssetName.ToString());
		}
		else if (metaInfo.AssetType == EAssetType::Material)
		{
			materialMetaInfos.Add(metaInfo);
		}
		});

	const FString CurrentMeshPath = CurrentStaticMesh ? CurrentStaticMesh->GetAssetName().ToString() : "None";

	// Path에서 확장자 빼고 파일명만 parsing 하여 보여주기
	std::filesystem::path meshPath = std::filesystem::path(static_cast<std::string>(CurrentMeshPath)).stem();
	FString simpleMeshName = meshPath.stem().string();

	if (ImGui::BeginCombo("Static Mesh", simpleMeshName.CStr()))
	{
		for (const FString& assetName : StaticMeshAssetNames)
		{
			bool isSelected = (CurrentMeshPath == assetName);

			// Path에서 확장자 빼고 파일명만 parsing 하여 보여주기
			std::filesystem::path assetPath = std::filesystem::path(static_cast<std::string>(assetName)).stem();
			FString simpleAssetName = assetPath.stem().string();

			if (ImGui::Selectable((simpleAssetName.ToString() + "##" + assetName.ToString()).c_str(), isSelected))
			{
				StaticMeshComponent->SetMesh(mAssetManager->GetAssetAs<FStaticMeshAsset>(FName(assetName), true));
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", assetName.CStr());

			if (isSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}

		ImGui::EndCombo();
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
		{
			const FGuid& AssetGuid = *static_cast<const FGuid*>(Payload->Data);
			const FAssetMetaInfo& AssetMetaInfo = mAssetManager->GetMetaInfo(AssetGuid);

			if (AssetMetaInfo.AssetType == EAssetType::StaticMesh)
			{
				TSharedPtr<FStaticMeshAsset> MatchedMeshAsset = mAssetManager->GetAssetAs<FStaticMeshAsset>(AssetGuid, true);
				StaticMeshComponent->SetMesh(MatchedMeshAsset);
				UE_LOG("Success: StaticMesh applied: %s", AssetGuid.ToString().c_str());
			}
		}
		ImGui::EndDragDropTarget();
	}

	// 현재 가진 Material이 있으면 그것을, 없으면 None을 콤보박스 이름으로
	const auto& Materials = StaticMeshComponent->GetMaterials();
	for (int32 i = 0; i < Materials.Num(); i++)
	{
		ImGui::PushID(i);

		TSharedPtr<FMaterialAsset> currentMaterial = Materials[i];
		FString currentMaterialName = currentMaterial ? currentMaterial->GetAssetName().ToString() : "None";

		// Path에서 확장자 빼고 파일명만 parsing 하여 보여주기
		std::filesystem::path materialPath = std::filesystem::path(static_cast<std::string>(currentMaterialName)).stem();
		FString simpleMaterialName = materialPath.stem().string();

		if (ImGui::BeginCombo("Material", simpleMaterialName.CStr()))
		{
			for (const FAssetMetaInfo& metaInfo : materialMetaInfos)
			{
				// Path에서 확장자 빼고 파일명만 parsing 하여 보여주기
				std::filesystem::path metaPath = std::filesystem::path(metaInfo.AssetName.ToString().ToString()).stem();
				FString simpleMetaPath = metaPath.stem().string();

				bool isSelected = (currentMaterialName == metaInfo.AssetName.ToString());
				if (ImGui::Selectable((simpleMetaPath.ToString() + "##" + metaInfo.AssetID.ToString().ToString()).c_str(), isSelected))
				{
					TSharedPtr<FMaterialAsset> materialAsset = mAssetManager->GetAssetAs<FMaterialAsset>(metaInfo.AssetID, true);
					StaticMeshComponent->SetMaterial(i, materialAsset);
				}
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", metaInfo.AssetName.ToString().CStr()); if (isSelected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		FVector2 UVOffset = StaticMeshComponent->GetUVOffset(i);
		if (ImGui::DragFloat2("UV Offset", &UVOffset.X, 0.01f))
		{
			StaticMeshComponent->SetUVOffset(i, UVOffset);
		}
		ImGui::PopID();
	}
}
