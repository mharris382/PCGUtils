#include "MaterialCache/PCGUtilsComponentIdentity.h"

#include "Components/ActorComponent.h"

#define LOCTEXT_NAMESPACE "PCGUtilsComponentIdentity"

bool PCGUtilsMaterialCache::TryGetComponentOwnerKey(const UActorComponent* Component, FName& OutOwnerKey, FText* OutError)
{
	if (!Component)
	{
		if (OutError)
		{
			*OutError = LOCTEXT("NullComponent", "Cannot derive an owner key: component is null.");
		}
		return false;
	}

	OutOwnerKey = Component->GetFName();
	return true;
}

bool PCGUtilsMaterialCache::TryResolveComponent(const FSoftObjectPath& ComponentPath, UActorComponent*& OutComponent, FText* OutError)
{
	OutComponent = nullptr;

	if (ComponentPath.IsNull())
	{
		if (OutError)
		{
			*OutError = LOCTEXT("EmptyPath", "Cannot resolve component: soft object path is empty.");
		}
		return false;
	}

	UObject* Resolved = ComponentPath.ResolveObject();
	if (!Resolved)
	{
		if (OutError)
		{
			*OutError = FText::Format(
				LOCTEXT("UnresolvedPath", "Could not resolve component path '{0}' to a currently-instanced object."),
				FText::FromString(ComponentPath.ToString()));
		}
		return false;
	}

	UActorComponent* Component = Cast<UActorComponent>(Resolved);
	if (!Component)
	{
		if (OutError)
		{
			*OutError = FText::Format(
				LOCTEXT("NotAComponent", "Path '{0}' resolved to '{1}', which is not a UActorComponent."),
				FText::FromString(ComponentPath.ToString()),
				FText::FromString(Resolved->GetClass()->GetName()));
		}
		return false;
	}

	OutComponent = Component;
	return true;
}

bool PCGUtilsMaterialCache::TryGetComponentOwnerKey(const FSoftObjectPath& ComponentPath, FName& OutOwnerKey, UActorComponent** OutComponent, FText* OutError)
{
	UActorComponent* Component = nullptr;
	if (!TryResolveComponent(ComponentPath, Component, OutError))
	{
		return false;
	}

	if (OutComponent)
	{
		*OutComponent = Component;
	}

	return TryGetComponentOwnerKey(Component, OutOwnerKey, OutError);
}

bool PCGUtilsMaterialCache::TryGetComponentSoftObjectPath(const UActorComponent* Component, FSoftObjectPath& OutPath, FText* OutError)
{
	if (!Component)
	{
		if (OutError)
		{
			*OutError = LOCTEXT("NullComponentForPath", "Cannot build a soft object path: component is null.");
		}
		return false;
	}

	OutPath = FSoftObjectPath(Component);

	if (OutPath.IsNull() || OutPath.ResolveObject() != Component)
	{
		OutPath.Reset();
		if (OutError)
		{
			*OutError = FText::Format(
				LOCTEXT("UnstablePath", "Component '{0}' does not have a persistently resolvable soft object path (likely transient). Use the direct component-reference API instead."),
				FText::FromString(Component->GetName()));
		}
		return false;
	}

	return true;
}

bool PCGUtilsMaterialCache::TryGetComponentFromObject(UObject* Object, UActorComponent*& OutComponent, FText* OutError)
{
	OutComponent = Cast<UActorComponent>(Object);
	if (!OutComponent)
	{
		if (OutError)
		{
			*OutError = FText::Format(
				LOCTEXT("ObjectNotAComponent", "'{0}' is not a UActorComponent."),
				FText::FromString(Object ? Object->GetName() : TEXT("null")));
		}
		return false;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
