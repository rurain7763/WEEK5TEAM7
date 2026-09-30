#include "World.h"

#include <format>

#include "RenderInfo.h"
#include "JsonUtil.h"
#include "Console.h"
#include "ObjectFactory.h"
#include "PrimitiveComponent.h"
#include "FBVH.h"
#include "UTextComponent.h"
#include "UTextComponent.h"
#include "ShowFlags.h"
#include "UStaticMeshComponent.h"
#include "Camera.h"
#include "FInstrumentor.h"

UWorld::~UWorld()
{
	mPrimitiveComponents.Empty();

	for (AActor* removeActor : mActors)
	{
		removeActor->mWorld = nullptr;
		FObjectFactory::DestroyObject(removeActor);
	}
}

void UWorld::SerializeClass(json::JSON& outJson) const
{
	UObject::SerializeClass(outJson);
	json::JSON actorsJson = json::JSON::Make(json::JSON::Class::Array);

	for (const AActor* actor : mActors)
	{
		json::JSON actorJson;
		actor->SerializeClass(actorJson);
		actorsJson.append(std::move(actorJson));
	}

	outJson["Properties"]["mActors"] = actorsJson;
}

void UWorld::DeserializeClass(const json::JSON& inJson)
{
	UObject::DeserializeClass(inJson);

	const json::JSON& propertiesJson = inJson.at("Properties");

	if (!propertiesJson.hasKey("mActors") || propertiesJson.at("mActors").JSONType() != json::JSON::Class::Array)
	{
		throw std::runtime_error(std::format("{}: mActors requires an array", GetClass()->Name));
	}

	const json::JSON& actorsJson = propertiesJson.at("mActors");

	for (const auto& actorJson : actorsJson.ArrayRange())
	{
		if (!actorJson.hasKey("ClassName") || actorJson.at("ClassName").JSONType() != json::JSON::Class::String)
		{
			throw std::runtime_error(std::format("{}: ClassName requires a string", GetClass()->Name));
		}
		FString className(actorJson.at("ClassName").ToString());

		const FClassInfo* classInfo = FObjectFactory::GetClassInfoByName(className);
		if (!classInfo)
		{
			throw std::runtime_error(std::format("{}: Unknown class name: {}", GetClass()->Name, className));
		}
		AActor* actor = static_cast<AActor*>(FObjectFactory::LoadObject(classInfo, actorJson));
		AddActor(actor);
	}
}

void UWorld::RegisterActorComponents(AActor* actor)
{
	if (!actor) return;

	for (UActorComponent* component : actor->GetComponents())
	{
		RegisterComponent(component);
	}
}

void UWorld::UnregisterActorComponents(AActor* actor)
{
	if (!actor) return;

	for (UActorComponent* component : actor->GetComponents())
	{
		UnregisterComponent(component);
	}
}

void UWorld::AddActor(AActor* actor)
{
	assert(actor != nullptr);
	assert(getActorIndex(actor->UUID) == -1);

	actor->mWorld = this;
	for (UActorComponent* component : actor->GetComponents())
	{
		RegisterComponent(component);
	}

	mActors.Add(actor);

	// TODO: 전처리를 통해 에디터 모드가 아니면 아래 코드를 컴파일하지 않게 막아야함.
	actor->CreateEditorComponents();
}

bool UWorld::RemoveActor(uint32 uuid)
{
	int32 ActorIndex = getActorIndex(uuid);
	if (ActorIndex == -1)
	{
		return false;
	}

	AActor* ActorToRemove = mActors[ActorIndex];
	for (UActorComponent* component : ActorToRemove->GetComponents())
	{
		UnregisterComponent(component);
	}
	ActorToRemove->mWorld = nullptr;

	mActors.RemoveAtSwap(ActorIndex);

	return true;
}

void UWorld::RegisterComponent(UActorComponent* Component)
{
	RefreshComponentTick(Component);
	UPrimitiveComponent* PrimitiveComponent = Component->Cast<UPrimitiveComponent>();
	if (PrimitiveComponent)
	{
		mPrimitiveComponents.Add(PrimitiveComponent);
		mShouldRenderComponents.Add(Component);
		mbBVHDirty = true;
	}
	else if (Component->IsRenderable())
	{
		mNonPrimitiveRenderableComponents.Add(Component);
	}
}

void UWorld::UnregisterComponent(UActorComponent* Component)
{
	mTickableComponents.Remove(Component);
	mUUIDTickableComponents.Remove(Component);
	UPrimitiveComponent* PrimitiveComponent = Component->Cast<UPrimitiveComponent>();
	if (PrimitiveComponent)
	{
		int32 index = mPrimitiveComponents.Find(PrimitiveComponent);
		if (index != -1)
		{
			mPrimitiveComponents.RemoveAtSwap(index);
			mbBVHDirty = true;
		}

		index = mShouldRenderComponents.Find(Component);
		if (index != -1)
		{
			mShouldRenderComponents.RemoveAtSwap(index);
		}
	}
	else if (Component->IsRenderable())
	{
		int32 index = mNonPrimitiveRenderableComponents.Find(Component);
		if (index != -1)
		{
			mNonPrimitiveRenderableComponents.RemoveAtSwap(index);
		}

		index = mShouldRenderComponents.Find(Component);
		if (index != -1)
		{
			mShouldRenderComponents.RemoveAtSwap(index);
		}
	}
}

void UWorld::FComponentTickList::Add(UActorComponent* Component)
{
	if (!Indices.Contains(Component))
		Indices.Add(Component, Components.Add(Component));
}

void UWorld::FComponentTickList::Remove(UActorComponent* Component)
{
	const uint32* Found = Indices.Find(Component);
	if (!Found) return;
	const uint32 Index = *Found;
	Indices.Remove(Component);
	if (bTicking)
	{
		// Tick 내부에서 제거되어도 뒤의 컴포넌트를 건너뛰거나 해제된 포인터를 호출하지 않습니다.
		Components[Index] = nullptr;
		bNeedsCompaction = true;
		return;
	}
	Components.RemoveAtSwap(Index);
	if (Index < static_cast<uint32>(Components.Num()))
		*Indices.Find(Components[Index]) = Index;
}

void UWorld::FComponentTickList::Tick(float DeltaTime, int32 Count)
{
	bTicking = true;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (UActorComponent* Component = Components[Index])
			Component->Tick(DeltaTime);
	}
	bTicking = false;
	if (bNeedsCompaction)
	{
		// 순회 중 삭제가 발생한 프레임에만 빈 슬롯을 정리합니다.
		for (int32 Index = Components.Num() - 1; Index >= 0; --Index)
		{
			if (Components[Index]) continue;
			Components.RemoveAtSwap(Index);
			if (Index < Components.Num())
				*Indices.Find(Components[Index]) = static_cast<uint32>(Index);
		}
		bNeedsCompaction = false;
	}
}

void UWorld::RefreshComponentTick(UActorComponent* Component)
{
	if (!Component->IsTickable())
	{
		mTickableComponents.Remove(Component);
		mUUIDTickableComponents.Remove(Component);
		return;
	}
	if (Component->Cast<UText3DComponent>())
		mUUIDTickableComponents.Add(Component);
	else
		mTickableComponents.Add(Component);
}

void UWorld::MarkBoundsDirty(UActorComponent* Component)
{
	UPrimitiveComponent* PrimitiveComponent = Component->Cast<UPrimitiveComponent>();
	if (PrimitiveComponent)
	{
		mBVH.Refit(PrimitiveComponent, PrimitiveComponent->GetBoundingBox());
	}
}

void UWorld::RequestRenderUpdate(UActorComponent* Component)
{
	mShouldRenderComponents.Add(Component);
}

void UWorld::InvalidateMeshLOD(UPrimitiveComponent* Component)
{
	// 조회 중 직접 적용한 변경은 노드 캐시에 함께 기록하므로 다시 무효화할 필요가 없습니다.
	if (!mbSelectingBVHLOD) mBVH.Invalidate(Component);
}

void UWorld::Tick(float deltaTime)
{
	mbProcessedRenderThisTick = false;
	// 전체 Actor/Component 목록을 훑지 않고 실제 갱신 대상만 순회합니다.
	mTickableComponents.Tick(deltaTime, mTickableComponents.Components.Num());
	// 숨긴 UUID는 컴포넌트별 조건 검사도 하지 않고 목록 전체를 건너뜁니다.
	if (FShowFlags::Get().IsEnabled(EShowFlag::UUIDText))
		mUUIDTickableComponents.Tick(deltaTime, mUUIDTickableComponents.Components.Num());

	if (mbBVHDirty)
	{
		mBVH.Release();
		for (UPrimitiveComponent* primitiveComponent : mPrimitiveComponents)
		{
			mBVH.AddItem(primitiveComponent, primitiveComponent->GetBoundingBox());
		}
		mBVH.Build();
		mBVHLODStates.Empty();
		mBVHLODStates.SetNum(mBVH.GetNodeCount());
		mbBVHDirty = false;
	}
}

void UWorld::RefreshBVHLODState(const FBVHNode* Node, uint64 SettingsRevision)
{
	auto ReadSelection = [&](int32 EntryIndex, FMeshLODSelection& Selection)
	{
		const auto* MeshComponent = mBVH.GetPayload(EntryIndex)->Cast<UStaticMeshComponent>();
		if (MeshComponent && MeshComponent->GetMesh())
		{
			Selection = MeshComponent->GetMesh()->GetLODSelection();
			return true;
		}
		return false;
	};
	FBVHLODTraversal::Refresh(Node, mBVHLODStates, SettingsRevision, ReadSelection);
}

void UWorld::Render(float deltaTime, FRenderCollector& outCollector)
{
	for (UActorComponent* Component : mNonPrimitiveRenderableComponents)
	{
		Component->Render(outCollector);
	}
	
	// 프록시는 컴포넌트당 하나이므로 첫 뷰가 공통 LOD를 결정합니다.
	// 이번 조회의 요청은 다음 Tick 이후 반영해 분할 화면 중간에 프록시가 바뀌지 않게 합니다.
	const bool bFirstView = !mbProcessedRenderThisTick;
	mbProcessedRenderThisTick = true;
	if (bFirstView)
	{
		PROFILE_SCOPE("Viewport/Collect/UpdateRenderProxies");
		const uint64 ResourceVersion = FStaticMeshAsset::GetLODResourceChangeVersion();
		if (mLODResourceVersion != ResourceVersion)
		{
			// 버퍼 재생성 시에만 순회합니다. 기존 raw 버퍼를 제출하기 전에 프록시를 갱신합니다.
			for (UPrimitiveComponent* Component : mPrimitiveComponents)
				if (auto* Mesh = Component->Cast<UStaticMeshComponent>())
					Mesh->SetMesh(Mesh->GetMesh(), Mesh->GetLODIndex());
			mLODResourceVersion = ResourceVersion;
		}
		for (UActorComponent* Component : mShouldRenderComponents)
			Component->Render(outCollector);
		mShouldRenderComponents.Empty();
		mLODQueryStats = {};
	}

	if (FShowFlags::Get().IsEnabled(EShowFlag::Primitive))
	{
		if (mBVH.IsValid())
		{
			PROFILE_SCOPE("Viewport/Collect/BVHVisibilityLOD");
			const bool bUpdateLOD = bFirstView && outCollector.Camera;
			if (bUpdateLOD)
				RefreshBVHLODState(mBVH.GetRootNode(), FStaticMeshAsset::GetLODChangeVersion());
			const FVector ViewOrigin = outCollector.Camera ? outCollector.Camera->Transform.GetLocation() : FVector();
			auto SubmitRange = [&](const FBVHItemRange& Range, int32 LOD)
			{
				for (int32 Index = Range.Offset; Index < Range.Offset + Range.Count; ++Index)
				{
					UPrimitiveComponent* Object = mBVH.GetPayload(Index);
					if (LOD >= 0)
					{
						if (auto* Mesh = Object->Cast<UStaticMeshComponent>(); Mesh && Mesh->GetMesh())
						{
							const uint32 ResolvedLOD = Mesh->GetMesh()->HasLOD(LOD) ? static_cast<uint32>(LOD) : 0;
							if (Mesh->GetLODIndex() != ResolvedLOD)
							{
								Mesh->SetMesh(Mesh->GetMesh(), ResolvedLOD);
								++mLODQueryStats.ChangedComponents;
							}
						}
					}
					Object->GetRenderProxy()->Submit();
				}
			};
			mbSelectingBVHLOD = true;
			FBVHLODTraversal::Query(mBVH.GetRootNode(), mBVHLODStates, outCollector.Frustum,
				ViewOrigin, mLODQueryStats, SubmitRange, bUpdateLOD);
			mbSelectingBVHLOD = false;
		}
	}

	outCollector.BVH = &mBVH;
}

int32 UWorld::getActorIndex(uint32 actorUUID) const
{
	for (uint32 i = 0; i < mActors.Num(); ++i)
	{
		if (mActors[i]->UUID == actorUUID)
		{
			return i;
		}
	}

	return -1;
}
