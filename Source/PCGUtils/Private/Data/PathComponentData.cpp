// Fill out your copyright notice in the Description page of Project Settings.


#include "Data/PathComponentData.h"


void UPCGUtilPathDataLibrary::GetPathDataFromSettings(UPCGMetadata* Meta,
	const FGetPathElementSettingsConfiguration* Settings, const FPathComponentData* Data, bool bAlwaysCreateAttribute )
{
	if (!Meta || !Settings || !Data)
		return;
	if (Settings->bExtractProcessPathGraph && !Settings->ProcessPathGraphAttributeName.IsEmpty())
	{
		AssignProcessPathGraphAttribute(Meta,FName(Settings->ProcessPathGraphAttributeName), Data);
	}
	if (Settings->bExtractHeight && !Settings->HeightAttributeName.IsEmpty())
	{
		AssignPathHeightAttribute(Meta,FName(Settings->HeightAttributeName), Data);
	}
	
	if (Settings->bExtractGroup && !Settings->GroupAttributeName.IsEmpty())
	{
		AssignGroupIDAttribute(Meta,FName(Settings->GroupAttributeName), Data);
	}
	if (Settings->bExtractColor && !Settings->ColorAttributeName.IsEmpty())
	{
		AssignPathColorAttribute(Meta,FName(Settings->ColorAttributeName), Data, FLinearColor::White, bAlwaysCreateAttribute);
	}
	if (Settings->bExtractDensity && !Settings->PathDensityAttributeName.IsEmpty())
	{
		AssignPathDensityAttribute(Meta, FName(Settings->PathDensityAttributeName), Data, 1.0f, bAlwaysCreateAttribute);
	}
}

bool UPCGUtilPathDataLibrary::AssignGroupIDAttribute(UPCGMetadata* Meta, FName AttributeName,
                                                     const FPathComponentData* Data)
{
	if (!Data)return false;
	if (FPCGMetadataAttribute<int32>* GroupAttribute =
					Meta->FindOrCreateAttribute<int32>(
						FPCGAttributeIdentifier(FName(AttributeName), PCGMetadataDomainID::Data),
						Data->GroupID,
						/*bAllowsInterpolation=*/false,
						/*bOverrideParent=*/false,
						/*bOverwriteIfTypeMismatch=*/true))
	{
		GroupAttribute->SetValue(PCGInvalidEntryKey, Data->GroupID);
		return true;
	}
	return false;
}

bool UPCGUtilPathDataLibrary::AssignPathHeightAttribute(UPCGMetadata* Meta, FName AttributeName,
	const FPathComponentData* Data)
{
	if (!Data)return false;
	if (FPCGMetadataAttribute<float>* HeightAttribute =
					Meta->FindOrCreateAttribute<float>(
						FPCGAttributeIdentifier(AttributeName, PCGMetadataDomainID::Data),
						0.0f,
						/*bAllowsInterpolation=*/false,
						/*bOverrideParent=*/false,
						/*bOverwriteIfTypeMismatch=*/true))
	{
		HeightAttribute->SetValue(PCGInvalidEntryKey, Data->Height);
		return true;
	}
	return false;
}

bool UPCGUtilPathDataLibrary::AssignProcessPathGraphAttribute(UPCGMetadata* Meta, FName AttributeName,
	const FPathComponentData* Data)
{
	if (!Data)return false;
	if (FPCGMetadataAttribute<FSoftObjectPath>* GraphAttribute =
					Meta->FindOrCreateAttribute<FSoftObjectPath>(
						FPCGAttributeIdentifier(AttributeName, PCGMetadataDomainID::Data),
						Data->ProcessPathGraph.GetOverrideGraphSoft(),
						/*bAllowsInterpolation=*/false,
						/*bOverrideParent=*/false,
						/*bOverwriteIfTypeMismatch=*/true))
	{
		GraphAttribute->SetValue(PCGInvalidEntryKey, Data->ProcessPathGraph.GetOverrideGraphSoft());
		return true;
	}
	return false;
}

bool UPCGUtilPathDataLibrary::AssignPathColorAttribute(UPCGMetadata* Meta, FName AttributeName,
	const FPathComponentData* Data, FLinearColor DefaultColor, bool bAlwaysCreateAttribute)
{
	if (!Data)return false;
	if (!bAlwaysCreateAttribute && !Data->bSetPathColor)
		return false;
	FLinearColor color = Data->GetPathColor(DefaultColor);
	FVector4 colorV = FVector4(color.R, color.G, color.B, color.A);
	if (FPCGMetadataAttribute<FVector4>* ColorAttribute =
		Meta->FindOrCreateAttribute<FVector4>(FPCGAttributeIdentifier(AttributeName, PCGMetadataDomainID::Data), colorV, false, false, false))
	{
		ColorAttribute->SetValue(PCGInvalidEntryKey, colorV);
		return true;
	}
	return false;
}

bool UPCGUtilPathDataLibrary::AssignPathDensityAttribute(UPCGMetadata* Meta, FName AttributeName,
	const FPathComponentData* Data, float DefaultDensity, bool bAlwaysCreateAttribute)
{
	if (!Data)return false;
	if (!bAlwaysCreateAttribute && !Data->bSetPathDensity)
		return false;
	float density = Data->GetPathDensity(DefaultDensity);
	if (FPCGMetadataAttribute<float>* DensityAttribute =
					Meta->FindOrCreateAttribute<float>(
						FPCGAttributeIdentifier(AttributeName, PCGMetadataDomainID::Data),
						0.0f,
						/*bAllowsInterpolation=*/false,
						/*bOverrideParent=*/false,
						/*bOverwriteIfTypeMismatch=*/true))
	{
		DensityAttribute->SetValue(PCGInvalidEntryKey, Data->Height);
		return true;
	}
	return false;
}



