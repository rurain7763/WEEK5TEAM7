#pragma once

#include <string_view>
#include <filesystem>
#include "SceneData.h"
#include "TArray.h"
#include "RenderInfo.h"
#include "enum.h"
#include "FAssetManager.h"
#include "FObjViewer.h"

inline constexpr std::string_view kSceneDataDir = "SceneData\\";
inline constexpr std::string_view kSceneDataSuffix = ".Scene";

class FFileManager;
class FFrameTimer;
class FEditorViewportClient;
class FGraphicsManager;
class UWorld;
class URenderer;
struct FViewport;
struct FEditorLayout;
struct FEditorViewport;
class UStaticMesh;

class FSceneManager
{
public:
	FSceneManager();
	~FSceneManager();

	void Tick(float deltaTime);
	void Render(float deltaTime, FRenderCollector& outCollector);

	// Clear world
	void NewScene();
	void DeleteScene();

	// 파일 탐색기용 오버로드
	void SaveScene(FCamera* Camera, const std::filesystem::path& scenePath, const FFileManager& fileManager);
	void LoadScene(FCamera* Camera, const std::filesystem::path& scenePath, const FFileManager& fileManager);

	UWorld* GetCurrentWorld() const { return mCurrentWorld; }

	AActor* GetSelectedActor() const { return mSelectedActor; }
	bool IsActorSelected() const { return mSelectedActor != nullptr; }
	void SetSelectedActor(AActor* actor);
	void ResetSelectedActor() { mSelectedActor = nullptr; }

private:
	UWorld* mCurrentWorld = nullptr;
	AActor* mSelectedActor = nullptr;
};
