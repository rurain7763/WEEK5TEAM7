#pragma once

#include "Core.h"
#include "FControlWindow.h"
#include "FOutlinerWindow.h"
#include "FContentBrowser.h"
#include "FPropertyWindow.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGui/imgui_impl_win32.h"
#include "ImGui/imgui_impl_dx11.h"
#include "Assets.h"
#include <filesystem>

class FCamera;
class FGraphicsManager;
class URenderer;
class FFrameTimer;
class FSceneManager;
class FFileManager;
struct FEditorViewportClient;
struct FEditorLayout;
struct FEditorViewport;

struct FGuiReference
{
	FCamera* EditorCamera;
	FFrameTimer* FrameTimer;
	FSceneManager* SceneManager;
	FGraphicsManager* GraphicsManager;
	FEditorViewportClient* ViewportClient;
	FFileManager* FileManager;
	FAssetManager* AssetManager;
	FEditorLayout* EditorLayout;
	FEditorViewport* Viewports;
	int32 ViewportCount = 0;
};

class FEditorUIManager : public FContentBrowserEventHandler
{
public:
	FEditorUIManager(URenderer& InRenderer);

	void Render(FGuiReference& GuiReference);

	inline float GetViewportX() const { return mViewportX; }
	inline float GetViewportY() const { return mViewportY; }
	inline float GetViewportWidth() const { return mViewportWidth; }
	inline float GetViewportHeight() const { return mViewportHeight; }

private:
	void RenderBottomBar();

	void OnNewAssetFile(const FAssetFileHeader& Header, const std::filesystem::path& FilePath) override;
	void OnDeleteAssetFile(const std::filesystem::path& FilePath) override;
	void RefreshContentBrowser(const std::filesystem::path& TargetDirectory) override;

private:
	static constexpr float BottomBarHeight = 30.0f;
	
	URenderer& mRenderer;

	FContentBrowser mContentBrowser;
	FPropertyWindow mPropertyWindow;
	FControlWindow mControlWindow;
	FOutlinerWindow mOutlinerWindow;

	float mViewportX;
	float mViewportY;
	float mViewportWidth;
	float mViewportHeight;
};
