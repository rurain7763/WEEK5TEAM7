#pragma once

#include "Core.h"
#include "TArray.h"
#include "ImGui/imgui.h"

struct FGuiReference;
class FSceneManager;
class UObject;

class FOutlinerWindow
{
public:
	void Render(const FGuiReference& GuiReference);

private:
	uint64 mLastGUObjectRevision = -1;
	TArray<UObject*> mSortedObjectLists;
};
