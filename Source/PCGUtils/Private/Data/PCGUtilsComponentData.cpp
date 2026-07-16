// Fill out your copyright notice in the Description page of Project Settings.


#include "Data/PCGUtilsComponentData.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

namespace 
{
	bool AssignInt32Attribute(UPCGMetadata* Meta, FName AttributeName, int32 Value, 
	const FPCGMetadataDomainID& DomainID = PCGMetadataDomainID::Data,
	const int64 Key = PCGInvalidEntryKey)
	{
		if (!Meta || AttributeName.IsNone()) return false;
		if (FPCGMetadataAttribute<int32>* Attribute =
						Meta->FindOrCreateAttribute<int32>(
							FPCGAttributeIdentifier(FName(AttributeName), DomainID),
							Value,false,false,true))
		{
			Attribute->SetValue(Key, Value);
			return true;
		}
		return false;
	}
	bool AssignFloatAttribute(UPCGMetadata* Meta, FName AttributeName, float Value, 
	const FPCGMetadataDomainID& DomainID = PCGMetadataDomainID::Data,
	const int64 Key = PCGInvalidEntryKey)
	{
		if (!Meta || AttributeName.IsNone()) return false;
		if (FPCGMetadataAttribute<float>* Attribute =
						Meta->FindOrCreateAttribute<float>(
							FPCGAttributeIdentifier(FName(AttributeName), DomainID),
							Value,false,false,true))
		{
			Attribute->SetValue(Key, Value);
			return true;
		}
		return false;
	}
	bool AssignSoftObjectAttribute(UPCGMetadata* Meta, FName AttributeName, const FSoftObjectPath& Value, 
	const FPCGMetadataDomainID& DomainID = PCGMetadataDomainID::Data,
	const int64 Key = PCGInvalidEntryKey)
	{
		if (!Meta || AttributeName.IsNone()) return false;
		if (FPCGMetadataAttribute<FSoftObjectPath>* Attribute =
						Meta->FindOrCreateAttribute<FSoftObjectPath>(
							FPCGAttributeIdentifier(FName(AttributeName), DomainID),
							Value,false,false,true))
		{
			Attribute->SetValue(Key, Value);
			return true;
		}
		return false;
	}
	
	bool AssignVector4Attribute(UPCGMetadata* Meta, FName AttributeName, const FVector4& Value, 
	const FPCGMetadataDomainID& DomainID = PCGMetadataDomainID::Data,
	const int64 Key = PCGInvalidEntryKey)
	{
		if (!Meta || AttributeName.IsNone()) return false;
		if (FPCGMetadataAttribute<FVector4>* Attribute =
						Meta->FindOrCreateAttribute<FVector4>(
							FPCGAttributeIdentifier(FName(AttributeName), DomainID),
							Value,false,false,true))
		{
			Attribute->SetValue(Key, Value);
			return true;
		}
		return false;
	}
	
	bool AssignTransformAttribute(UPCGMetadata* Meta, FName AttributeName, const FTransform& Value, 
	const FPCGMetadataDomainID& DomainID = PCGMetadataDomainID::Data,
	const int64 Key = PCGInvalidEntryKey)
	{
		if (!Meta || AttributeName.IsNone()) return false;
		if (FPCGMetadataAttribute<FTransform>* Attribute =
						Meta->FindOrCreateAttribute<FTransform>(
							FPCGAttributeIdentifier(FName(AttributeName), DomainID),
							Value,false,false,true))
		{
			Attribute->SetValue(Key, Value);
			return true;
		}
		return false;
	}
}

void UPCGUtilPathDataLibrary::GetPathDataFromSettings(UPCGMetadata* Meta,
	const FGetPathElementSettingsConfiguration* Settings, const FPathComponentData* Data)
{
	if (!Meta || !Settings || !Data)
		return;
	
	if (Settings->bExtractProcessPathGraph && !Settings->ProcessPathGraphAttributeName.IsEmpty())
	{
		AssignSoftObjectAttribute(Meta, FName(Settings->ProcessPathGraphAttributeName), Data->ProcessPathGraph.GetOverrideGraphSoft());
	}
	
	if (Settings->bExtractHeight && !Settings->HeightAttributeName.IsEmpty())
	{
		AssignFloatAttribute(Meta, FName(Settings->HeightAttributeName), Data->Height);
	}
	
	if (Settings->bExtractWidth && !Settings->WidthAttributeName.IsEmpty())
	{
		AssignFloatAttribute(Meta, FName(Settings->WidthAttributeName), Data->PathWidth);
	}
	
	if (Settings->bExtractGroup && !Settings->GroupAttributeName.IsEmpty())
	{
		AssignInt32Attribute(Meta, FName(Settings->GroupAttributeName), Data->GroupID);
	}
	if (Settings->bExtractColor && !Settings->ColorAttributeName.IsEmpty())
	{
		AssignVector4Attribute(Meta, FName(Settings->ColorAttributeName), Data->GetPathColor());
	}
	if (Settings->bExtractDensity && !Settings->PathDensityAttributeName.IsEmpty())
	{
		AssignFloatAttribute(Meta, FName(Settings->PathDensityAttributeName), Data->PathDensity);
	}
}

void UPCGUtilPathDataLibrary::GetComponentDataFromSettings(UPCGMetadata* Meta,
	const FGetComponentDataSettings* Settings, USceneComponent* Data)
{
	if (!Meta || !Settings || !Data)
		return;
	
	if (Settings->bOutputActorReference && !Settings->ActorReferenceAttributeName.IsEmpty())
	{
		FSoftObjectPath ActorReference;
		if (AActor* owner = Data->GetOwner())
		{
			ActorReference = TSoftObjectPtr<AActor>(owner).ToSoftObjectPath();	
		}
		AssignSoftObjectAttribute(Meta, FName(Settings->ActorReferenceAttributeName), ActorReference);
	}
	if (Settings->bOutputComponentReference && !Settings->ComponentReferenceAttributeName.IsEmpty())
	{
		AssignSoftObjectAttribute(Meta, FName(Settings->ComponentReferenceAttributeName), TSoftObjectPtr<UObject>(Data).ToSoftObjectPath());
	}
	if (Settings->bOutputComponentTransform && !Settings->RelativeTransformAttributeName.IsEmpty())
	{
		AssignTransformAttribute(Meta, FName(Settings->RelativeTransformAttributeName),Data->GetRelativeTransform());
	}
}

void UPCGUtilPathDataLibrary::GetPointDataFromSettings(UPCGMetadata* Meta,	const FGetPointElementSettingsConfiguration* Settings, const FPointComponentData* Data, 
                                                       const FPCGMetadataDomainID Domain, const int64 Key )
{
	if (!Meta || !Settings || !Data)
		return;
	
	if (Settings->bExtractProcessPointGraph && !Settings->ProcessPointGraphAttributeName.IsEmpty())
	{
		AssignSoftObjectAttribute(Meta, FName(Settings->ProcessPointGraphAttributeName), Data->ProcessPointGraph.GetOverrideGraphSoft());
	}
	if (Settings->bExtractGroup && !Settings->GroupAttributeName.IsEmpty())
	{
		AssignInt32Attribute(Meta, FName(Settings->GroupAttributeName), Data->GroupID, Domain, Key);
	}
	//if (Settings->bExtractFalloff && !Settings->FalloffAttributeName.IsEmpty())
	//{
	//	AssignInt32Attribute(Meta, FName(Settings->FalloffAttributeName), Data->GroupID, Domain, Key);
	//}
}

