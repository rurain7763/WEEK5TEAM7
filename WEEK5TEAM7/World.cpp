#include "World.h"

#include <format>

#include "RenderInfo.h"
#include "JsonUtil.h"
#include "Console.h"
#include "ObjectFactory.h"
#include "PrimitiveComponent.h"
#include "UTextComponent.h"
#include "ShowFlags.h"

UWorld::~UWorld()
{
	mPrimitiveComponents.Empty();
	mText3DComponents.Empty();

	for (AActor* removeActor : mActors)
	{
		removeActor->SetWorld(nullptr);
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

void UWorld::RegisterComponent(UActorComponent* component)
{
	if (!component) return;

	if (UPrimitiveComponent* PrimComp = component->Cast<UPrimitiveComponent>())
	{
		mPrimitiveComponents.Add(PrimComp);
	}
	else if (UText3DComponent* TextComp = component->Cast<UText3DComponent>())
	{
		mText3DComponents.Add(TextComp);
	}
}

void UWorld::UnregisterComponent(UActorComponent* component)
{
	if (!component) return;

	if (UPrimitiveComponent* PrimComp = component->Cast<UPrimitiveComponent>())
	{
		for (int32 i = 0; i < mPrimitiveComponents.Num(); ++i)
		{
			if (mPrimitiveComponents[i] == PrimComp)
			{
				mPrimitiveComponents.RemoveAtSwap(i);
				break;
			}
		}
	}
	else if (UText3DComponent* TextComp = component->Cast<UText3DComponent>())
	{
		for (int32 i = 0; i < mText3DComponents.Num(); ++i)
		{
			if (mText3DComponents[i] == TextComp)
			{
				mText3DComponents.RemoveAtSwap(i);
				break;
			}
		}
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

	actor->SetWorld(this);
	mActors.Add(actor);

	// TODO: 전처리를 통해 에디터 모드가 아니면 아래 코드를 컴파일하지 않게 막아야함.
	actor->CreateEditorComponents();

	RegisterActorComponents(actor);
}

bool UWorld::RemoveActor(uint32 actorUUID)
{
	int32 actorIndex = getActorIndex(actorUUID);
	if (actorIndex == -1)
	{
		return false;
	}

	AActor* actor = mActors[actorIndex];
	UnregisterActorComponents(actor);
	actor->SetWorld(nullptr);

	//mActors.RemoveAt(actorIndex, 1);
	mActors.RemoveAtSwap(actorIndex);

	return true;
}

void UWorld::Tick(float deltaTime)
{
	for (AActor* actor : mActors)
	{
		actor->Tick(deltaTime);
	}
}

void UWorld::Render(float deltaTime, FRenderCollector& outCollector)
{
	if (FShowFlags::Get().IsEnabled(EShowFlag::Primitive))
	{
		for (UPrimitiveComponent* PrimComp : mPrimitiveComponents)
		{
			PrimComp->Render(outCollector);
			if (outCollector.bNeedPickTargets)
			{
				PrimComp->RegisterPickTarget(outCollector);
			}
		}
	}

	if (FShowFlags::Get().IsEnabled(EShowFlag::UUIDText))
	{
		for (UText3DComponent* TextComp : mText3DComponents)
		{
			TextComp->Render(outCollector);
		}
	}
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
