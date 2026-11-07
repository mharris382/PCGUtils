// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGBasePointData.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"

struct FPCGContext;
class UPCGPointArrayData;

/** One point to write: the source point it copies, and the box of cells it covers. */
struct FPCGVoxelCellOut
{
	int32 SourcePoint = INDEX_NONE;
	FIntVector First = FIntVector::ZeroValue;
	FIntVector Span = FIntVector(1);
};

namespace PCGVoxelPointIO
{
	/**
	 * Reads one value per point through Selector (a point attribute or property, broadcast and conversions allowed).
	 * Logs an error naming the setting, the selector and the input, and returns false, when the selector does not
	 * exist on the points or its type cannot be read as T.
	 */
	template <typename T>
	bool ReadPointValues(FPCGContext* Context, const UPCGBasePointData* Points, const FPCGAttributePropertyInputSelector& Selector,
		TArray<T>& OutValues, const FText& NodeTitle, int32 InputIndex, const FText& SettingName, const FText& ExpectedType)
	{
		const FPCGAttributePropertyInputSelector Fixed = Selector.CopyAndFixLast(Points);
		const TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Points, Fixed);
		const TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(Points, Fixed);
		if (!Accessor || !Keys)
		{
			PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(NodeTitle, InputIndex, FText::Format(
				NSLOCTEXT("PCGVoxelPointIO", "Missing", "{0} '{1}' does not exist on the points."),
				SettingName, Fixed.GetDisplayText())), Context);
			return false;
		}
		OutValues.SetNum(Points->GetNumPoints());
		if (!OutValues.IsEmpty() && !Accessor->GetRange<T>(MakeArrayView(OutValues), 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
		{
			PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(NodeTitle, InputIndex, FText::Format(
				NSLOCTEXT("PCGVoxelPointIO", "Type", "{0} '{1}' is of type {2}; it must be {3}."),
				SettingName, Fixed.GetDisplayText(),
				FText::FromString(StaticEnum<EPCGMetadataTypes>()->GetNameStringByValue(Accessor->GetUnderlyingType())), ExpectedType)), Context);
			return false;
		}
		return true;
	}

	/** True when the points carry a point attribute with this name. */
	PCGUTILSVOXEL_API bool HasPointAttribute(const UPCGBasePointData* Points, FName Name);

	/**
	 * Makes point data from Source (the voxel set Grid was read from): one point per entry of Cells, copied from its
	 * source point (metadata, tags and @Data carry over), centred on its box of cells, with bounds covering that box
	 * plus the source point's surface offsets. Seeds: a point that is its source unchanged keeps its seed; any other
	 * gets PCGHelpers::ComputeSeed of its first cell, as the Volume Sampler seeds a cell. With bWriteSpan the span is
	 * written to SpanAttribute; otherwise that attribute is removed.
	 */
	PCGUTILSVOXEL_API UPCGBasePointData* WriteCells(FPCGContext* Context, const UPCGBasePointData* Source, const FPCGVoxelGrid& Grid,
		TConstArrayView<FPCGVoxelCellOut> Cells, FName SpanAttribute, bool bWriteSpan);

	/**
	 * Gives each point of Points its own metadata entry (parented to the one it had), so per-point values can be written.
	 * Call it once, on points whose entries still belong to the parent metadata: WriteCells already does. Calling it
	 * again parents new entries to local ones, and inherited attribute values are lost.
	 */
	PCGUTILSVOXEL_API void MakeEntriesUnique(UPCGBasePointData* Points);

	/** Creates (or replaces) a point attribute and writes one value per point. Points must have unique entries. */
	template <typename T>
	void WritePointAttribute(UPCGBasePointData* Points, FName Name, const T& Default, TConstArrayView<T> Values)
	{
		if (Name.IsNone() || !Points)
		{
			return;
		}
		FPCGMetadataDomain* Domain = Points->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements);
		if (!Domain)
		{
			return;
		}
		Domain->DeleteAttribute(Name);
		FPCGMetadataAttribute<T>* Attribute = Domain->CreateAttribute<T>(Name, Default, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/true);
		if (!Attribute)
		{
			return;
		}
		const TConstPCGValueRange<int64> Entries = Points->GetConstMetadataEntryValueRange();
		for (int32 Index = 0; Index < Values.Num() && Index < Entries.Num(); ++Index)
		{
			Attribute->SetValue(Entries[Index], Values[Index]);
		}
	}
}
