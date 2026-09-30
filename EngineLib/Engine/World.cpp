#include "World.h"

#include <format>

#include "Rendering/RenderInfo.h"
#include "Core/IO/JsonUtil.h"
#include "Editor/Console.h"

IMPLEMENT_CLASS(UWorld, UObject);

UWorld::~UWorld()
{
	//for (AActor* removeActor : mActors)
	//{
	//	delete removeActor;
	//}
}

void UWorld::SerializeClass(json::JSON& outJson) const
{
	UObject::SerializeClass(outJson);
	json::JSON actorsJson = json::JSON::Make(json::JSON::Class::Array);

	for (const auto& actor : mActors)
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
		throw std::runtime_error(std::format("{}: mActors requires an array", GetRuntimeClass()->Name));
	}

	const json::JSON& actorsJson = propertiesJson.at("mActors");

	for (const auto& actorJson : actorsJson.ArrayRange())
	{
		if (!actorJson.hasKey("ClassName") || actorJson.at("ClassName").JSONType() != json::JSON::Class::String)
		{
			throw std::runtime_error(std::format("{}: ClassName requires a string", GetRuntimeClass()->Name));
		}
		FString className(actorJson.at("ClassName").ToString());

		const FClassInfo* classInfo = FObjectFactory::GetClassInfoByName(className);
		if (!classInfo)
		{
			throw std::runtime_error(std::format("{}: Unknown class name: {}", GetRuntimeClass()->Name, className));
		}
		AActor* actor = static_cast<AActor*>(FObjectFactory::LoadObject(classInfo, actorJson));
		AddActor(std::unique_ptr<AActor>(actor));
	}
}

void UWorld::Serialize(FStructuredArchive& archive)
{
	UObject::Serialize(archive);

	/* Construct Object */
	if (archive.IsLoading())
	{
		archive.BeginObject("mActors");
		int32 count = 0;
		archive << TNamedValue{ "Count", count };

		for (int32 i = 0; i < count && !archive.HasError(); ++i)
		{
			const FString key = FString(std::to_string(i));

			if (!archive.BeginObject(key.CStr()))
			{
				archive.SetError();
				archive.EndObject();
				break;
			}

			FString className;
			archive << TNamedValue<FString>{ "ClassName", className };

			const FClassInfo* classInfo = FObjectFactory::GetClassInfoByName(className);
			UObject* loadedObject = FObjectFactory::LoadObject(classInfo, archive);
			if (!loadedObject)
			{
				archive.SetError();
				archive.EndObject();
				break;
			}

			AActor* actor = loadedObject->Cast<AActor>();
			if (actor)
			{
				AddActor(std::unique_ptr<AActor>(actor));
			}

			archive.EndObject();
		}
		archive.EndObject();
	}
	else
	{
		archive << TNamedValue{ "mActors", mActors };
	}
}

void UWorld::AddActor(std::unique_ptr<AActor> actor)
{
	assert(actor != nullptr);
	assert(getActorIndex(actor->UUID) == -1);

	mActors.Add(std::move(actor));
	mbRenderInfosDirty = true;
}

bool UWorld::RemoveActor(uint32 componentUUID)
{
	int32 componentIndex = getActorIndex(componentUUID);
	if (componentIndex == -1)
	{
		return false;
	}

	//mActors.RemoveAt(componentIndex, 1);
	mActors.RemoveAtSwap(componentIndex);
	mbRenderInfosDirty = true;

	return true;
}

const TArray<const FRenderInfo*>& UWorld::GetRenderInfos()
{
	return mRenderInfoRefs;
}

void UWorld::Update(float deltaTime)
{
	if (mbRenderInfosDirty || mRenderInfoRefs.Num() != mActors.Num())
	{
		mRenderInfoRefs.Reset(mActors.Num());

		for (auto& actor : mActors)
		{
			actor->Update(deltaTime, mRenderInfoRefs);
		}
		mbRenderInfosDirty = false;
	}
}

/*
void UWorld::Render()
{
	for (AActor* actor : mActors)
	{
		actor->Render();
	}
}
*/

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
