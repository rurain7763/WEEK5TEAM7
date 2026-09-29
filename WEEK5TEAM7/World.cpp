#include "World.h"

#include <format>

#include "RenderInfo.h"
#include "JsonUtil.h"
#include "Console.h"
#include "ObjectFactory.h"
#include "PrimitiveComponent.h"
#include "FBVH.h"

UWorld::~UWorld()
{
	for (AActor* removeActor : mActors)
	{
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
	UPrimitiveComponent* PrimitiveComponent = Component->Cast<UPrimitiveComponent>();
	if (PrimitiveComponent)
	{
		mPrimitiveComponents.Add(PrimitiveComponent);
		mbBVHDirty = true;
	}
	else if (Component->IsRenderable())
	{
		mNonPrimitiveRenderableComponents.Add(Component);
	}
}

void UWorld::UnregisterComponent(UActorComponent* Component)
{
	UPrimitiveComponent* PrimitiveComponent = Component->Cast<UPrimitiveComponent>();
	if (PrimitiveComponent)
	{
		int32 index = mPrimitiveComponents.Find(PrimitiveComponent);
		if (index != -1)
		{
			mPrimitiveComponents.RemoveAtSwap(index);
			mbBVHDirty = true;
		}
	}
	else if (Component->IsRenderable())
	{
		int32 index = mNonPrimitiveRenderableComponents.Find(Component);
		if (index != -1)
		{
			mNonPrimitiveRenderableComponents.RemoveAtSwap(index);
		}
	}
}

void UWorld::MarkBoundsDirty(UActorComponent* Component)
{
	UPrimitiveComponent* PrimitiveComponent = Component->Cast<UPrimitiveComponent>();
	if (PrimitiveComponent)
	{
		mBVH.Refit(PrimitiveComponent, PrimitiveComponent->GetBoundingBox());
	}
}

void UWorld::Tick(float deltaTime)
{
	for (AActor* actor : mActors)
	{
		actor->Tick(deltaTime);
	}

	if (mbBVHDirty)
	{
		mBVH.Release();
		for (UPrimitiveComponent* primitiveComponent : mPrimitiveComponents)
		{
			mBVH.AddItem(primitiveComponent, primitiveComponent->GetBoundingBox());
		}
		mBVH.Build();
		mbBVHDirty = false;
	}
}

void UWorld::Render(float deltaTime, FRenderCollector& outCollector)
{
	for (UActorComponent* Component : mNonPrimitiveRenderableComponents)
	{
		Component->Render(outCollector);
	}

	if (mBVH.IsValid())
	{
		TArray<FBVHNode*> NodeStack;
		NodeStack.Add(mBVH.GetRootNode());
		while (NodeStack.Num() > 0)
		{
			FBVHNode* CurrentNode = NodeStack.Last();
			NodeStack.Pop();

			if (CurrentNode == nullptr)
			{
				continue;
			}

			int32 CollisionResult = outCollector.Frustum.Intersects(CurrentNode->BoundingBox);
			if (CollisionResult == -1)
			{
				continue;
			}

			if (CollisionResult == 1 || CurrentNode->IsLeaf())
			{
				for (int32 i = 0; i < CurrentNode->ItemRange.Count; ++i)
				{
					UPrimitiveComponent* Object = mBVH.GetPayload(CurrentNode->ItemRange.Offset + i);
					Object->Render(outCollector);
				}
			}
			else
			{
				NodeStack.Add(CurrentNode->Left);
				NodeStack.Add(CurrentNode->Right);
			}
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
