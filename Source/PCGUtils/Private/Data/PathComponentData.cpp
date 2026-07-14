// Fill out your copyright notice in the Description page of Project Settings.


#include "Data/PathComponentData.h"

namespace 
{
	bool AssignInt32Attribute(UPCGMetadata* Meta, FName AttributeName, int32 Value)
	{
		if (!Meta || AttributeName.IsNone()) return false;
		if (FPCGMetadataAttribute<int32>* Attribute =
						Meta->FindOrCreateAttribute<int32>(
							FPCGAttributeIdentifier(FName(AttributeName), PCGMetadataDomainID::Data),
							Value,false,false,true))
		{
			Attribute->SetValue(PCGInvalidEntryKey, Value);
			return true;
		}
		return false;
	}
	bool AssignFloatAttribute(UPCGMetadata* Meta, FName AttributeName, float Value)
	{
		if (!Meta || AttributeName.IsNone()) return false;
		if (FPCGMetadataAttribute<float>* Attribute =
						Meta->FindOrCreateAttribute<float>(
							FPCGAttributeIdentifier(FName(AttributeName), PCGMetadataDomainID::Data),
							Value,false,false,true))
		{
			Attribute->SetValue(PCGInvalidEntryKey, Value);
			return true;
		}
		return false;
	}
	bool AssignSoftObjectAttribute(UPCGMetadata* Meta, FName AttributeName, const FSoftObjectPath& Value)
	{
		if (!Meta || AttributeName.IsNone()) return false;
		if (FPCGMetadataAttribute<FSoftObjectPath>* Attribute =
						Meta->FindOrCreateAttribute<FSoftObjectPath>(
							FPCGAttributeIdentifier(FName(AttributeName), PCGMetadataDomainID::Data),
							Value,false,false,true))
		{
			Attribute->SetValue(PCGInvalidEntryKey, Value);
			return true;
		}
		return false;
	}
	bool AssignVector4Attribute(UPCGMetadata* Meta, FName AttributeName, const FVector4& Value)
	{
		if (!Meta || AttributeName.IsNone()) return false;
		if (FPCGMetadataAttribute<FVector4>* Attribute =
						Meta->FindOrCreateAttribute<FVector4>(
							FPCGAttributeIdentifier(FName(AttributeName), PCGMetadataDomainID::Data),
							Value,false,false,true))
		{
			Attribute->SetValue(PCGInvalidEntryKey, Value);
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
		AssignVector4Attribute(Meta, FName(Settings->ColorAttributeName), Data->GetPathColor(FLinearColor::White));
	}
	if (Settings->bExtractDensity && !Settings->PathDensityAttributeName.IsEmpty())
	{
		AssignFloatAttribute(Meta, FName(Settings->PathDensityAttributeName), Data->PathDensity);
	}
}

