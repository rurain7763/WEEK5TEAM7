#include "SceneManager.h"

#include <algorithm>
#include <format>

#include "FileManager.h"
#include "NativeFileDialog.h"
#include "EngineStatics.h"
#include "JsonUtil.h"
#include "ObjectFactory.h"
#include "PrimitiveComponent.h"
#include "TArray.h"
#include "World.h"
#include "FEditorViewportClient.h"
#include "Camera.h"
#include "Console.h"
#include "FLogManager.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGui/imgui_impl_dx11.h"
#include "imGui/imgui_impl_win32.h"

#include "FrameTimer.h"
#include "CubeComponent.h"
#include "UAtlasAnimationComponent.h"
#include "ActorComponent.h"
#include "WindowApplication.h"

#include "Cube.h"
#include "Assets.h"
#include "UTextComponent.h"
#include "ShowFlags.h"
#include "UStaticMeshComponent.h"
#include "LaunchEngineLoop.h"
#include "FAssetManager.h"
#include "FTextBuilder.h"

FSceneManager::FSceneManager()
{
}

FSceneManager::~FSceneManager()
{
	FObjectFactory::DestroyObject(mCurrentWorld);
}

void FSceneManager::Tick(float deltaTime)
{
	mCurrentWorld->Tick(deltaTime);
}

void FSceneManager::Render(float deltaTime, FRenderCollector& outCollector)
{
	mCurrentWorld->Render(deltaTime, outCollector);
}

void FSceneManager::NewScene()
{
	if (mCurrentWorld != nullptr)
	{
		FObjectFactory::DestroyObject(mCurrentWorld);
	}

	//UEngineStatics::SetNextUUID(0);
	ResetSelectedActor();
	mCurrentWorld = FObjectFactory::ConstructObject<UWorld>();
}

void FSceneManager::DeleteScene()
{
	if (mCurrentWorld != nullptr)
	{
		FObjectFactory::DestroyObject(mCurrentWorld);
		mCurrentWorld = nullptr;
	}
	ResetSelectedActor();
}


void FSceneManager::SaveScene(FCamera* Camera, const std::filesystem::path& scenePath, const FFileManager& fileManager)
{
	if (mCurrentWorld == nullptr)
	{
		throw std::runtime_error("Cannot save scene because current world is null.");
	}

	uint32 version = 0;

	// 기존 파일이 있으면 Version을 유지한다.
	try
	{
		const FString previousSceneString =
			fileManager.ReadFileToString(scenePath);

		const json::JSON previousSceneJson =
			json::JSON::Load(previousSceneString);

		if (previousSceneJson.hasKey("Version") &&
			previousSceneJson.at("Version").JSONType() ==
			json::JSON::Class::Integral)
		{
			version =
				previousSceneJson.at("Version").ToInt();
		}
	}
	catch (const std::exception&)
	{
		// 새로 저장하는 파일이면 Version 0부터 시작한다.
		version = 0;
	}

	json::JSON sceneJson =
		json::JSON::Make(json::JSON::Class::Object);

	json::JSON worldJson =
		json::JSON::Make(json::JSON::Class::Object);

	mCurrentWorld->SerializeClass(worldJson);

	sceneJson["Version"] = version;
	sceneJson["NextUUID"] = UEngineStatics::GetNextUUID();
	sceneJson["World"] = worldJson;

	json::JSON& PerspectiveCameraJson = sceneJson["PerspectiveCamera"];
	PerspectiveCameraJson["Location"] = JsonUtils::ToJson(Camera->Transform.GetLocation());
	PerspectiveCameraJson["Rotation"] = JsonUtils::ToJson(Camera->Transform.GetRotation());
	PerspectiveCameraJson["FOV"] = Camera->mFovDegree;
	PerspectiveCameraJson["Near"] = Camera->mNear;
	PerspectiveCameraJson["Far"] = Camera->mFar;

	const FString jsonString(sceneJson.dump(1, "  "));

	fileManager.WriteStringToFile(
		scenePath,
		jsonString);
}



// "Data/apple_mid.obj" 같은 경로에서 "apple_mid" 에셋을 찾아오는 헬퍼 함수
static TSharedPtr<FStaticMeshAsset> FindMeshAssetByPathOrName(const std::string& pathStr)
{
	std::filesystem::path p(pathStr);
	std::string stemName = p.stem().string(); // "apple_mid" 추출

	// 1. "apple_mid" 이름으로 등록되어 있는지 직접 검색
	TSharedPtr<FStaticMeshAsset> meshAsset = FAssetManager::Get().GetAssetAs<FStaticMeshAsset>(FName(stemName.c_str()), true);
	if (meshAsset)
	{
		return meshAsset;
	}

	// 2. FAssetManager에 등록된 전체 메타정보를 순회하여 파일명(stem)이 같은 StaticMesh 검색
	FGuid foundGuid;
	FAssetManager::Get().ForEachMetaInfo([&](const FAssetMetaInfo& meta) {
		if (meta.AssetType == EAssetType::StaticMesh)
		{
			std::filesystem::path metaPath(meta.AssetName.ToString().CStr());
			if (metaPath.stem().string() == stemName)
			{
				foundGuid = meta.AssetID;
			}
		}
		});

	if (foundGuid.IsValid())
	{
		return FAssetManager::Get().GetAssetAs<FStaticMeshAsset>(foundGuid, true);
	}

	return nullptr;
}



void FSceneManager::LoadScene(FCamera* Camera, const std::filesystem::path& scenePath, const FFileManager& fileManager)
{
	const FString jsonString = fileManager.ReadFileToString(scenePath);
	const json::JSON sceneJson = json::JSON::Load(jsonString);

	// 1. NextUUID 설정
	if (sceneJson.hasKey("NextUUID"))
	{
		const uint32 nextUUID = sceneJson.at("NextUUID").ToInt();
		UEngineStatics::SetNextUUID(nextUUID);
	}

	// 2. 카메라 파싱 (단일 float과 [배열], Near/NearClip, Far/FarClip 모두 대응)
	if (sceneJson.hasKey("PerspectiveCamera"))
	{
		const json::JSON& camJson = sceneJson.at("PerspectiveCamera");

		if (camJson.hasKey("Location"))
		{
			Camera->Transform.SetLocation(JsonUtils::FromJson<FVector>(camJson.at("Location")));
		}

		if (camJson.hasKey("Rotation"))
		{
			// 파일의 XYZ 라디안을 Pitch(Y), Yaw(Z), Roll(X)의 도 단위로 변환합니다.
			const FVector XYZ = JsonUtils::FromJson<FVector>(camJson.at("Rotation"));
			const FRotator Rotation(
				FMath::RadiansToDegrees(-XYZ.y),
				FMath::RadiansToDegrees(XYZ.z),
				FMath::RadiansToDegrees(XYZ.x));
			Camera->Transform.SetRotation(Rotation);
		}

		// FOV (숫자 또는 [배열])
		if (camJson.hasKey("FOV"))
		{
			const auto& fov = camJson.at("FOV");
			Camera->mFovDegree = (fov.JSONType() == json::JSON::Class::Array) ? fov.at(0).ToFloat() : fov.ToFloat();
		}

		// Near / NearClip
		if (camJson.hasKey("Near"))
		{
			Camera->mNear = camJson.at("Near").ToFloat();
		}
		else if (camJson.hasKey("NearClip"))
		{
			const auto& nearClip = camJson.at("NearClip");
			Camera->mNear = (nearClip.JSONType() == json::JSON::Class::Array) ? nearClip.at(0).ToFloat() : nearClip.ToFloat();
		}

		// Far / FarClip
		if (camJson.hasKey("Far"))
		{
			Camera->mFar = camJson.at("Far").ToFloat();
		}
		else if (camJson.hasKey("FarClip"))
		{
			const auto& farClip = camJson.at("FarClip");
			Camera->mFar = (farClip.JSONType() == json::JSON::Class::Array) ? farClip.at(0).ToFloat() : farClip.ToFloat();
		}
	}

	// 3. 월드 및 액터 로드 (신규 World 포맷 vs 레거시 Primitives 포맷)
	UWorld* newWorld = nullptr;

	if (sceneJson.hasKey("World") && sceneJson.at("World").JSONType() == json::JSON::Class::Object)
	{
		// [포맷 1] 신규 UWorld 리플렉션 역직렬화
		newWorld = FObjectFactory::LoadObject<UWorld>(sceneJson.at("World"));
	}
	else if (sceneJson.hasKey("Primitives") && sceneJson.at("Primitives").JSONType() == json::JSON::Class::Object)
	{
		// [포맷 2] 레거시 Primitives 구조
		newWorld = FObjectFactory::ConstructObject<UWorld>();
		const json::JSON& primitivesJson = sceneJson.at("Primitives");

		for (const auto& [uuidStr, primJson] : primitivesJson.ObjectRange())
		{
			FVector location = JsonUtils::FromJson<FVector>(primJson.at("Location"));
			FRotator rotation = JsonUtils::FromJson<FRotator>(primJson.at("Rotation"));
			FVector scale = JsonUtils::FromJson<FVector>(primJson.at("Scale"));

			std::string typeStr = primJson.hasKey("Type") ? primJson.at("Type").ToString() : "StaticMeshComp";

			AActor* newActor = FObjectFactory::ConstructObject<AActor>();
			// 필요 시 UUID 지정: newActor->UUID = std::stoul(uuidStr);

			if (typeStr == "StaticMeshComp")
			{
				UStaticMeshComponent* meshComp = FObjectFactory::ConstructObject<UStaticMeshComponent>(
					location, rotation, scale
				);

				if (primJson.hasKey("ObjStaticMeshAsset"))
				{
					std::string meshPath = primJson.at("ObjStaticMeshAsset").ToString();
					TSharedPtr<FStaticMeshAsset> meshAsset = FindMeshAssetByPathOrName(meshPath);
					if (meshAsset)
					{
						meshComp->SetMesh(meshAsset);
					}
					else
					{
						UE_LOG_WARN("Could not find StaticMesh asset for: %s", meshPath.c_str());
					}
				}

				newActor->AddRootSceneComponent(meshComp);
			}

			newWorld->AddActor(newActor);
		}
	}
	else
	{
		throw std::runtime_error(std::format("Scene file '{}' does not contain valid World or Primitives data.", scenePath.string()));
	}

	if (newWorld == nullptr)
	{
		throw std::runtime_error(std::format("Failed to deserialize world from '{}'.", scenePath.string()));
	}

	// 4. 새 월드 적용
	FObjectFactory::DestroyObject(mCurrentWorld);
	mCurrentWorld = newWorld;

	ResetSelectedActor();
}




void  FSceneManager::SetSelectedActor(AActor* actor)
{
	if (actor == nullptr)
	{
		UE_LOG_WARN("SetSelectedActor: Attempted to set selected actor to nullptr.");
		return;
	}

	if (actor == mSelectedActor)
	{
		UE_LOG_WARN("SetSelectedActor: Actor with UUID %d is already selected.", actor->UUID);
		return; // No change
	}

	UE_LOG_WARN("SetSelectedActor: Actor with UUID %d is now selected.", actor->UUID);
	mSelectedActor = actor;
}

//
//FSceneData FSceneManager::ReadSceneData(
//	std::string_view sceneName,
//	const FFileManager& fileManager)
//{
//	FString fileName = sceneName;
//	fileName += kSceneDataSuffix;
//
//	json::JSON jsonData = json::JSON::Load(fileManager.ReadFileToString(fileName));
//	FSceneData sceneData = FSceneData(jsonData);
//	return sceneData;
//}
//
//UWorld* FSceneManager::BuildWorldFromSceneData(const FSceneData& sceneData)
//{
//	//UWorld* newWorld = FObjectFactory::ConstructObject<UWorld>();
//
//	//for (const auto& [UUID, primitiveData] : sceneData.Primitives) 
//	//{
//	//	// TODO: Replace AActor creation logic later
//	//	AActor* newActor = FObjectFactory::ConstructObject<AActor>();
//	//	UPrimitiveComponent* newPrimitiveComponent =
//	//		FObjectFactory::ConstructObject<UPrimitiveComponent>(
//	//			);
//	//}
//
//	//UEngineStatics::SetNextUUID(sceneData.NextUUID);
//	throw std::logic_error("BuildWorldFromSceneData is not implemented yet.");
//	return nullptr;
//}
