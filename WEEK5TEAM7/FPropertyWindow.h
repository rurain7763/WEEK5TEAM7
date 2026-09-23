#pragma once

#include "Core.h"
#include "ImGui/imgui.h"

struct FGuiReference;
class AActor;
class UText3DComponent;
class USpotLightComponent;
class UAtlasAnimationComponent;
class UStaticMeshComponent;
class FAssetManager;

class FPropertyWindow
{
public:
	void Render(const FGuiReference& GuiReference);

private:
	void RenderTransformProperties(AActor* TargetActor);
	void RenderText3DComponent(UText3DComponent* text3DComponent);
	void RenderSpotLightComponent(USpotLightComponent* spotLightComponent);
	void RenderAtlasAnimationComponent(UAtlasAnimationComponent* atlasAnimationComponent);
	void RenderStaticMeshComponent(UStaticMeshComponent* StaticMeshComponent);

private:
	FAssetManager* mAssetManager;
};