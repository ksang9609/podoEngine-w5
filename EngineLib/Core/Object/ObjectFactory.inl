template<typename TObject>
	requires std::derived_from<TObject, UObject>
TObject* FObjectFactory::ConstructUnInitializedObject()
{
	const FClassInfo* classInfo = TObject::GetClass();
	if (!classInfo || !classInfo->Constructor)
	{
		return nullptr;
	}

	TObject* instance = static_cast<TObject*>(classInfo->CreateInstance());
	if (instance)
	{
		instance->mClassInfo = classInfo;
		instance->mName = FName(classInfo->Name);
	}
	return instance;
}

template<typename TObject>
	requires std::derived_from<TObject, UObject>
TObject* FObjectFactory::ConstructUnInitializedObject(const FName& Name)
{
	const FClassInfo* classInfo = TObject::GetClass();

	if (!classInfo || !classInfo->Constructor)
	{
		return nullptr;
	}

	TObject* instance =static_cast<TObject*>(classInfo->CreateInstance());

	if (instance)
	{
		instance->mClassInfo = classInfo;
		instance->mName = Name;
	}

	return instance;
}

template<typename TObject, typename... Args>
	requires std::derived_from<TObject, UObject>
TObject* FObjectFactory::ConstructObjectWithName(const FName& Name, Args&&... args)
{
	static_assert(requires(TObject * obj)
	{
		obj->Initialize(std::forward<Args>(args)...);
	});

	TObject* instance = ConstructUnInitializedObject<TObject>(Name);

	if (instance)
	{
		instance->Initialize(std::forward<Args>(args)...);
	}

	return instance;
}

template<typename TObject, typename... Args>
	requires std::derived_from<TObject, UObject>
TObject* FObjectFactory::ConstructObject(Args&& ...args)
{
	static_assert(requires(TObject * obj)
	{
		obj->Initialize(std::forward<Args>(args)...);

	}, "TObject must have an Initialize method that accepts the provided arguments.");

	TObject* instance = ConstructUnInitializedObject<TObject>();

	if (instance)
	{
		instance->Initialize(std::forward<Args>(args)...);
	}
	return instance;
}

template<typename TObject>
	requires std::derived_from<TObject, UObject>
TObject* FObjectFactory::LoadObject(const json::JSON& inJson)
{
	TObject* instance = ConstructUnInitializedObject<TObject>();
	if (instance)
	{
		instance->DeserializeClass(inJson);
	}

	return instance;
}
