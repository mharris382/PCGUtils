// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/PCGMetadataDomain.h"

class UPCGData;

/**
 * Reading and writing a data's @Data domain value by value.
 *
 * The engine's domain copy (InitializeAsCopy, or Initialize with a parent) refuses a source domain whose parenting
 * it does not accept, and logs "Invalid parenting on the domain 'Data'". That is the usual state of data an upstream
 * node made from other data. Reading each value and writing it fresh has no such requirement, so these work on any
 * input.
 */
namespace PCGUtilsDataDomain
{
	/** The data's @Data domain, created if the data supports one and has none yet. Null when it cannot have one. */
	PCGUTILSCORE_API FPCGMetadataDomain* GetMutableDataDomain(UPCGData* Data);

	/**
	 * Writes one value to a single-entry domain such as @Data, creating the attribute or replacing one of another
	 * type. Works on a domain whose attributes come from a parent: the value is set on this domain only.
	 */
	template <typename T>
	void WriteValue(FPCGMetadataDomain& Domain, FName Name, const T& Value)
	{
		const PCGMetadataEntryKey Key = Domain.GetItemCountForChild() > 0 ? PCGFirstEntryKey : Domain.AddEntry();
		if (FPCGMetadataAttributeBase* Existing = Domain.GetMutableAttribute(Name))
		{
			if (Existing->GetTypeId() == PCG::Private::MetadataTypes<T>::Id)
			{
				static_cast<FPCGMetadataAttribute<T>*>(Existing)->SetValue(Key, Value);
				return;
			}
			Domain.DeleteAttribute(Name);
		}
		if (FPCGMetadataAttribute<T>* Attribute = Domain.CreateAttribute<T>(Name, Value,
			/*bAllowsInterpolation=*/false, /*bOverrideParent=*/true))
		{
			Attribute->SetValue(Key, Value);
		}
	}

	/** Copies every @Data attribute of Source onto OutDomain, except those named in Excluded. */
	PCGUTILSCORE_API void CopyDataAttributes(const UPCGData* Source, FPCGMetadataDomain& OutDomain, TConstArrayView<FName> Excluded = {});

	/**
	 * Copies the values of one element (a point, an attribute set entry) onto OutDomain: every attribute of
	 * Source's domain DomainName (NAME_None for its default domain) at element ElementIndex. Returns how many
	 * attributes were copied.
	 */
	PCGUTILSCORE_API int32 CopyElementAttributes(const UPCGData* Source, FName DomainName, int32 ElementIndex, FPCGMetadataDomain& OutDomain);
}
