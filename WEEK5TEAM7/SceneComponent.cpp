#include "SceneComponent.h"

#include <format>

#include "Transform.h"
#include "JsonUtil.h"

void USceneComponent::Initialize(FVector location, FRotator rotation, FVector scale3D)
{
	UActorComponent::Initialize();

	mRelativeTransform.SetLocation(location);
	mRelativeTransform.SetRotation(rotation);
	mRelativeTransform.SetScale(scale3D);
}

USceneComponent::~USceneComponent()
{
}

void USceneComponent::SerializeClass(json::JSON& outJson) const
{
	UActorComponent::SerializeClass(outJson);
	outJson["Properties"]["mRelativeLocation"] = JsonUtils::ToJson(mRelativeTransform.GetLocation());
	outJson["Properties"]["mRelativeRotation"] = JsonUtils::ToJson(mRelativeTransform.GetRotation());
	outJson["Properties"]["mRelativeScale3D"] = JsonUtils::ToJson(mRelativeTransform.GetScale());
}

void USceneComponent::DeserializeClass(const json::JSON& inJson)
{
	UActorComponent::DeserializeClass(inJson);

	const json::JSON& propertiesJson = inJson.at("Properties");

	if (!propertiesJson.hasKey("mRelativeLocation")
		|| propertiesJson.at("mRelativeLocation").JSONType() != json::JSON::Class::Array
		|| propertiesJson.at("mRelativeLocation").length() != 3)
	{
		throw std::runtime_error(std::format("{}: mRelativeLocation property requires an array of length 3", GetClass()->Name));
	}

	if (!propertiesJson.hasKey("mRelativeRotation")
		|| propertiesJson.at("mRelativeRotation").JSONType() != json::JSON::Class::Array
		|| propertiesJson.at("mRelativeRotation").length() != 3)
	{
		throw std::runtime_error(std::format("{}: mRelativeRotation property requires an array of length 3", GetClass()->Name));
	}

	if (!propertiesJson.hasKey("mRelativeScale3D")
		|| propertiesJson.at("mRelativeScale3D").JSONType() != json::JSON::Class::Array
		|| propertiesJson.at("mRelativeScale3D").length() != 3)
	{
		throw std::runtime_error(std::format("{}: mRelativeScale3D property requires an array of length 3", GetClass()->Name));
	}

	mRelativeTransform.SetLocation(JsonUtils::FromJson<FVector>(propertiesJson.at("mRelativeLocation")));
	mRelativeTransform.SetRotation(JsonUtils::FromJson<FRotator>(propertiesJson.at("mRelativeRotation")));
	mRelativeTransform.SetScale(JsonUtils::FromJson<FVector>(propertiesJson.at("mRelativeScale3D")));
}

FVector USceneComponent::GetRelativeLocation() const
{
	return mRelativeTransform.GetLocation();
}

void USceneComponent::SetRelativeLocation(FVector location)
{
	mRelativeTransform.SetLocation(location);
	OnTransformChanged();
}

FRotator USceneComponent::GetRelativeRotation() const
{
	return mRelativeTransform.GetRotation();
}

void USceneComponent::SetRelativeRotation(FRotator rotation)
{
	mRelativeTransform.SetRotation(rotation);
	OnTransformChanged();
}

FVector USceneComponent::GetRelativeScale3D() const
{
	return mRelativeTransform.GetScale();
}

void USceneComponent::SetRelativeScale3D(FVector scale)
{
	mRelativeTransform.SetScale(scale);
	OnTransformChanged();
}

const FTransform& USceneComponent::GetTransform() const
{
	return mRelativeTransform;
}
