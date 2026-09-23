#pragma once

#include "Core.h"

class FSceneManager;
struct FGuiReference;

class FControlWindow
{
public:
	void Render(const FGuiReference& GuiReference);

private:
	void RenderSpawnActorControl(const FGuiReference& GuiReference);
	void RenderSceneControl(const FGuiReference& GuiReference);
	void RenderCameraControl(const FGuiReference& GuiReference);
	void RenderGizmoControl(const FGuiReference& GuiReference);

private:
	int32 mSelectedTargetSpawnIndex = 1;
	int32 mSpawnCount = 1;
};