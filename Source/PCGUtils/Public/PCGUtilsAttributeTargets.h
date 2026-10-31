// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGSpatialData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"

/**
 * Output attribute names whose domain is chosen by the name itself, the way PCG selectors read them:
 * "Mesh" writes to the data's default domain (each point, on point data), "@Data.Mesh" writes one value for the
 * whole data set. Use this for an FName setting that names an attribute the node writes, so the user picks the
 * domain by renaming rather than through a second setting.
 */
namespace PCGUtilsAttributeTargets
{
	struct FResolvedTarget
	{
		FPCGMetadataDomain* Domain = nullptr;
		FPCGMetadataDomainID DomainID;
		FName AttributeName = NAME_None;

		bool IsDataDomain() const { return DomainID == PCGMetadataDomainID::Data; }
	};

	/**
	 * Resolves SettingValue against Data. Fails, with OutError naming SettingLabel and the offending value, when the
	 * text selects a point property ($Position), an extra accessor (.X), or a domain this data does not have.
	 */
	PCGUTILS_API bool Resolve(UPCGData& Data, FName SettingValue, const FString& SettingLabel,
		FResolvedTarget& OutTarget, FText& OutError);

	/** Writes one value under the resolved name, replacing any attribute of that name (whatever its type). */
	template <typename T>
	void WriteConstant(const FResolvedTarget& Target, const T& Value)
	{
		check(Target.Domain);
		Target.Domain->DeleteAttribute(Target.AttributeName);
		Target.Domain->CreateAttribute<T>(Target.AttributeName, Value, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/true);
	}
}
