// Copyright Max Harris

#include "Elements/PCGVoxelPattern.h"

#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"

#define LOCTEXT_NAMESPACE "PCGVoxelPattern"

PCG_DEFINE_TYPE_INFO(FPCGVoxelPatternDataTypeInfo, UPCGVoxelPatternData)

namespace PCGVoxelPatternPrivate
{
	// Saved in graphs: append, never reorder.
	enum EPatternPreset : int32
	{
		BlankPreset = 0,
		TopEdgePreset = 1,
		UprightCornerPreset = 2,
		InsideCornerFillPreset = 3,
		UprightCornerTwoPreset = 4,
		TopOutsideCornerPreset = 5,
		TopInsideCornerPreset = 6,
		SkirtEdgePreset = 7,
		SkirtOutsideCornerPreset = 8,
		SkirtInsideCornerPreset = 9,
	};

	/** Largest scale on one axis: the same limit as a box side. */
	const int32 MaxScale = 256;

	const FPCGMetadataAttributeBase* FindDataAttribute(const UPCGData* Data, FName Name, PCGMetadataEntryKey& OutKey)
	{
		const UPCGMetadata* Metadata = (Data && !Name.IsNone()) ? Data->ConstMetadata() : nullptr;
		const FPCGMetadataDomain* Domain = Metadata ? Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		if (!Domain)
		{
			return nullptr;
		}
		// No entry yet: the invalid key reads the attribute's default value.
		OutKey = Domain->GetItemCountForChild() > 0 ? PCGFirstEntryKey : PCGInvalidEntryKey;
		return Domain->GetConstAttribute(Name);
	}

	template <typename T>
	T ValueOf(const FPCGMetadataAttributeBase* Attribute, PCGMetadataEntryKey Key)
	{
		return static_cast<const FPCGMetadataAttribute<T>*>(Attribute)->GetValueFromItemKey(Key);
	}

	/** A single number of any numeric type. False when the attribute is not a number. */
	bool ReadNumber(const FPCGMetadataAttributeBase* Attribute, PCGMetadataEntryKey Key, double& OutValue)
	{
		const uint16 Type = Attribute->GetTypeId();
		if (Type == PCG::Private::MetadataTypes<int32>::Id)
		{
			OutValue = ValueOf<int32>(Attribute, Key);
		}
		else if (Type == PCG::Private::MetadataTypes<int64>::Id)
		{
			OutValue = static_cast<double>(ValueOf<int64>(Attribute, Key));
		}
		else if (Type == PCG::Private::MetadataTypes<float>::Id)
		{
			OutValue = ValueOf<float>(Attribute, Key);
		}
		else if (Type == PCG::Private::MetadataTypes<double>::Id)
		{
			OutValue = ValueOf<double>(Attribute, Key);
		}
		else
		{
			return false;
		}
		return true;
	}

	bool IsWhole(double Value)
	{
		return FMath::IsNearlyEqual(Value, FMath::RoundToDouble(Value), 1.e-4);
	}
}

void UPCGVoxelPatternData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);

	// Every authored value of the pattern, as text: any edit changes the Crc, so Apply Patterns reruns.
	FString Text;
	FPCGVoxelPattern::StaticStruct()->ExportText(Text, &Pattern, nullptr, nullptr, PPF_None, nullptr);
	Ar << Text;
	uint8 LocalScope = static_cast<uint8>(SelectorScope);
	Ar << LocalScope;
	FIntVector LocalScale = Scale;
	Ar << LocalScale;
	// Order (Priority), the selectors (data dependencies) and the attributes (metadata) are folded in by the bases.
}

UPCGParamData* UPCGVoxelPatternData::DuplicateData(FPCGContext* Context, bool bInitializeMetadata) const
{
	UPCGVoxelPatternData* Copy = FPCGContext::NewObject_AnyThread<UPCGVoxelPatternData>(Context);
	if (bInitializeMetadata)
	{
		check(Copy->Metadata);
		Copy->Metadata->InitializeAsCopy(FPCGMetadataInitializeParams(Metadata));
	}
	Copy->Priority = Priority;
	Copy->Pattern = Pattern;
	Copy->Selectors = Selectors;
	Copy->SelectorScope = SelectorScope;
	Copy->Scale = Scale;
	Copy->DataDependencies = DataDependencies;
	return Copy;
}

void UPCGVoxelPatternData::WriteSettingsAttributes(const FPCGVoxelPatternAttributeNames& Names)
{
	FPCGMetadataDomain* Domain = PCGUtilsDataDomain::GetMutableDataDomain(this);
	if (!Domain)
	{
		return;
	}
	if (!Names.OrderAttribute.IsNone())
	{
		PCGUtilsDataDomain::WriteValue<int32>(*Domain, Names.OrderAttribute, Priority);
	}
	if (!Names.ScaleAttribute.IsNone())
	{
		PCGUtilsDataDomain::WriteValue<FVector>(*Domain, Names.ScaleAttribute, FVector(Scale));
	}
}

bool UPCGVoxelPatternData::ResolveSettings(const FPCGVoxelPatternAttributeNames& Names, int32& OutOrder, FIntVector& OutScale, FText& OutError) const
{
	using namespace PCGVoxelPatternPrivate;
	OutOrder = Priority;
	OutScale = Scale;

	PCGMetadataEntryKey Key = PCGInvalidEntryKey;
	if (const FPCGMetadataAttributeBase* Attribute = FindDataAttribute(this, Names.OrderAttribute, Key))
	{
		double Value = 0.0;
		if (!ReadNumber(Attribute, Key, Value))
		{
			OutError = FText::Format(LOCTEXT("OrderType", "its @Data attribute '{0}' (Order) is of type {1}; Order is read from Integer32, Integer64, Float or Double."),
				FText::FromName(Names.OrderAttribute), PCG::Private::GetTypeNameText(Attribute->GetTypeId()));
			return false;
		}
		if (!IsWhole(Value) || FMath::Abs(Value) > static_cast<double>(MAX_int32))
		{
			OutError = FText::Format(LOCTEXT("OrderWhole", "its @Data attribute '{0}' (Order) is {1}; Order must be a whole number that fits an Integer32."),
				FText::FromName(Names.OrderAttribute), FText::AsNumber(Value));
			return false;
		}
		OutOrder = FMath::RoundToInt32(Value);
	}

	FVector ScaleValue(OutScale);
	bool bFromAttribute = false;
	if (const FPCGMetadataAttributeBase* Attribute = FindDataAttribute(this, Names.ScaleAttribute, Key))
	{
		double Uniform = 0.0;
		if (Attribute->GetTypeId() == PCG::Private::MetadataTypes<FVector>::Id)
		{
			ScaleValue = ValueOf<FVector>(Attribute, Key);
		}
		else if (ReadNumber(Attribute, Key, Uniform))
		{
			ScaleValue = FVector(Uniform);
		}
		else
		{
			OutError = FText::Format(LOCTEXT("ScaleType", "its @Data attribute '{0}' (Scale) is of type {1}; Scale is read from Vector, or from one number (Integer32, Integer64, Float, Double) for the same scale on every axis."),
				FText::FromName(Names.ScaleAttribute), PCG::Private::GetTypeNameText(Attribute->GetTypeId()));
			return false;
		}
		bFromAttribute = true;
	}
	static const TCHAR* const AxisNames[3] = { TEXT("X"), TEXT("Y"), TEXT("Z") };
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double Value = ScaleValue[Axis];
		if (!IsWhole(Value) || Value < 0.5 || Value > MaxScale + 0.5)
		{
			const FText Where = bFromAttribute
				? FText::Format(LOCTEXT("ScaleFromAttribute", "its @Data attribute '{0}' (Scale)"), FText::FromName(Names.ScaleAttribute))
				: LOCTEXT("ScaleFromNode", "the Scale set on its Voxel | Pattern node");
			OutError = FText::Format(LOCTEXT("ScaleRange", "{0} is {1} on {2}; each axis must be a whole number from 1 to {3}."),
				Where, FText::AsNumber(Value), FText::FromString(AxisNames[Axis]), MaxScale);
			return false;
		}
		OutScale[Axis] = FMath::RoundToInt32(Value);
	}
	return true;
}

#if WITH_EDITOR
FText UPCGVoxelPatternSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | Pattern");
}

FText UPCGVoxelPatternSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"One voxel pattern for Voxel | Apply Patterns. Probes say what the grid must look like around a filled cell (Unclaimed: a cell no earlier pattern took); "
		"Boxes are the chunks made where it does, each with a mesh index and a mesh turn; Turns are the rotations the "
		"pattern may take; Join Along joins neighbouring fits into longer chunks. Patterns run in Order: an earlier "
		"pattern takes its cells first.");
}

TArray<FPCGPreConfiguredSettingsInfo> UPCGVoxelPatternSettings::GetPreconfiguredInfo() const
{
	using namespace PCGVoxelPatternPrivate;
	return {
		{ BlankPreset, LOCTEXT("BlankPreset", "Voxel | Pattern"),
			LOCTEXT("BlankPresetTooltip", "An empty pattern: one single-cell box and no probes, to author from scratch.") },
		{ TopEdgePreset, LOCTEXT("TopEdgePreset", "Voxel | Pattern Top Edge"),
			LOCTEXT("TopEdgePresetTooltip", "An outside edge along the top of a wall (+X and +Z open), joined into one long chunk per wall. Written for SM_Voxel_Edge.") },
		{ UprightCornerPreset, LOCTEXT("UprightCornerPreset", "Voxel | Pattern Upright Corner"),
			LOCTEXT("UprightCornerPresetTooltip", "An outside upright corner (+X and +Y open) with at least one plain cell of wall on both sides, joined bottom to top. Written for SM_Voxel_Edge stood on end.") },
		{ InsideCornerFillPreset, LOCTEXT("InsideCornerFillPreset", "Voxel | Pattern Inside Corner Fill"),
			LOCTEXT("InsideCornerFillPresetTooltip", "Adds a cell in an inside upright corner, between two walls that are whole on the side facing it (plain cells or the solid back of a corner chunk). Written for SM_Voxel_Edge stood on end.") },
		{ UprightCornerTwoPreset, LOCTEXT("UprightCornerTwoPreset", "Voxel | Pattern Upright Corner 2 x 2"),
			LOCTEXT("UprightCornerTwoPresetTooltip", "An outside upright corner with a full 2 x 2 of cells behind it and plain wall running on both ways, as one chunk. Run it before the single-cell Upright Corner.") },
		{ TopOutsideCornerPreset, LOCTEXT("TopOutsideCornerPreset", "Voxel | Pattern Top Outside Corner"),
			LOCTEXT("TopOutsideCornerPresetTooltip", "Slope on the shape: the top cell of an outside corner (+X, +Y and +Z open), mesh index 1 (SM_Voxel_Corner_Ex_3Sides, or Ex_2Sides for a hip). Order 0: runs before Top Edge.") },
		{ TopInsideCornerPreset, LOCTEXT("TopInsideCornerPreset", "Voxel | Pattern Top Inside Corner"),
			LOCTEXT("TopInsideCornerPresetTooltip", "Slope on the shape: the top cell in an inside corner, where two top edges meet, mesh index 2 (SM_Voxel_Corner_In_2Sides). Order 0: runs before Top Edge.") },
		{ SkirtEdgePreset, LOCTEXT("SkirtEdgePreset", "Voxel | Pattern Skirt Edge"),
			LOCTEXT("SkirtEdgePresetTooltip", "Slope around the shape: adds a sloped cell beside every wall cell, joined into one long chunk per wall, mesh index 0 (SM_Voxel_Edge). Order 1: after Skirt Inside Corner, before Skirt Outside Corner.") },
		{ SkirtOutsideCornerPreset, LOCTEXT("SkirtOutsideCornerPreset", "Voxel | Pattern Skirt Outside Corner"),
			LOCTEXT("SkirtOutsideCornerPresetTooltip", "Slope around the shape: adds the corner cell diagonal to an outside corner, between two skirt edges, mesh index 1 (SM_Voxel_Corner_Ex_3Sides, or Ex_2Sides for a hip). Order 2: runs after Skirt Edge.") },
		{ SkirtInsideCornerPreset, LOCTEXT("SkirtInsideCornerPreset", "Voxel | Pattern Skirt Inside Corner"),
			LOCTEXT("SkirtInsideCornerPresetTooltip", "Slope around the shape: adds the valley cell in an inside corner, between two walls, mesh index 2 (SM_Voxel_Corner_In_2Sides). Order 0: runs before Skirt Edge.") },
	};
}

void UPCGVoxelPatternSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	using namespace PCGVoxelPatternPrivate;
	switch (PreconfiguredInfo.PreconfiguredIndex)
	{
	// The chamfer sets carry their run order, so a set dropped on one Apply Patterns node works as placed.
	case TopEdgePreset: Pattern = PCGVoxelPatterns::Presets::TopEdge(); Order = 1; break;
	case TopOutsideCornerPreset: Pattern = PCGVoxelPatterns::Presets::TopOutsideCorner(); Order = 0; break;
	case TopInsideCornerPreset: Pattern = PCGVoxelPatterns::Presets::TopInsideCorner(); Order = 0; break;
	case SkirtInsideCornerPreset: Pattern = PCGVoxelPatterns::Presets::SkirtInsideCorner(); Order = 0; break;
	case SkirtEdgePreset: Pattern = PCGVoxelPatterns::Presets::SkirtEdge(); Order = 1; break;
	case SkirtOutsideCornerPreset: Pattern = PCGVoxelPatterns::Presets::SkirtOutsideCorner(); Order = 2; break;
	case UprightCornerPreset: Pattern = PCGVoxelPatterns::Presets::UprightCorner(); break;
	case InsideCornerFillPreset: Pattern = PCGVoxelPatterns::Presets::InsideCornerFill(); break;
	case UprightCornerTwoPreset:
		Pattern = PCGVoxelPatterns::Presets::UprightCorner(2);
		Pattern.Name = TEXT("UprightCorner2x2");
		Pattern.DebugColor = FLinearColor(1.0f, 0.85f, 0.0f);
		break;
	default: Pattern = FPCGVoxelPattern(); break;
	}
}
#endif

FString UPCGVoxelPatternSettings::GetAdditionalTitleInformation() const
{
	if (PatternSource == EPCGVoxelPatternSource::Asset)
	{
		return PatternAsset.IsNull() ? FString() : PatternAsset.GetAssetName();
	}
	return Pattern.Name.IsNone() ? FString() : Pattern.Name.ToString();
}

TArray<FPCGPinProperties> UPCGVoxelPatternSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	FPCGPinProperties& Pin = Pins.Emplace_GetRef(PCGVoxelSelectorPins::SelectorLabel, FPCGVoxelSelectorDataTypeInfo::AsId(), true, true);
#if WITH_EDITOR
	Pin.Tooltip = LOCTEXT("SelectorTooltip", "Optional. Voxel selectors (Selector | In Bounds (Voxel) and the like) limiting where this pattern is applied; a cell is selected when any of them selects it. Nothing connected: everywhere.");
#endif
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelPatternSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	FPCGPinProperties& Pin = Pins.Emplace_GetRef(PCGVoxelPatternPins::PatternLabel, FPCGVoxelPatternDataTypeInfo::AsId(), false, false);
#if WITH_EDITOR
	Pin.Tooltip = LOCTEXT("PatternTooltip", "The pattern, for the Patterns pin of Voxel | Apply Patterns.");
#endif
	return Pins;
}

FPCGElementPtr UPCGVoxelPatternSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelPatternElement>();
}

bool FPCGVoxelPatternElement::CanExecuteOnlyOnMainThread(FPCGContext* Context) const
{
	const UPCGVoxelPatternSettings* Settings = Context ? Context->GetInputSettings<UPCGVoxelPatternSettings>() : nullptr;
	return Settings && Settings->PatternSource == EPCGVoxelPatternSource::Asset;
}

bool FPCGVoxelPatternElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGVoxelPatternSettings* Settings = Context->GetInputSettings<UPCGVoxelPatternSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | Pattern");
	auto Fail = [Context, &Title](const FText& Message)
	{
		PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, INDEX_NONE, Message), Context);
		return true;
	};

	const FPCGVoxelPattern* Pattern = &Settings->Pattern;
	if (Settings->PatternSource == EPCGVoxelPatternSource::Asset)
	{
		if (Settings->PatternAsset.IsNull())
		{
			return Fail(LOCTEXT("NoAsset", "Pattern Source is Asset, but Pattern Asset is not set."));
		}
		const UPCGVoxelPatternAsset* Asset = Settings->PatternAsset.LoadSynchronous();
		if (!Asset)
		{
			return Fail(FText::Format(LOCTEXT("AssetLoad", "Pattern Asset '{0}' could not be loaded."),
				FText::FromString(Settings->PatternAsset.ToString())));
		}
		Pattern = &Asset->Pattern;
	}

	FText Error;
	if (!PCGVoxelPatterns::Validate(*Pattern, Error))
	{
		return Fail(Pattern->Name.IsNone() ? Error
			: FText::Format(LOCTEXT("NamedError", "pattern '{0}': {1}"), FText::FromName(Pattern->Name), Error));
	}

	// Each checked on its own: the scale per axis, then the two attribute names against each other.
	static const TCHAR* const AxisNames[3] = { TEXT("X"), TEXT("Y"), TEXT("Z") };
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Settings->Scale[Axis] < 1 || Settings->Scale[Axis] > PCGVoxelPatternPrivate::MaxScale)
		{
			return Fail(FText::Format(LOCTEXT("ScaleSetting", "Scale {0} is {1}; it must be from 1 to {2}."),
				FText::FromString(AxisNames[Axis]), Settings->Scale[Axis], PCGVoxelPatternPrivate::MaxScale));
		}
	}
	if (!Settings->Attributes.OrderAttribute.IsNone() && Settings->Attributes.OrderAttribute == Settings->Attributes.ScaleAttribute)
	{
		return Fail(FText::Format(LOCTEXT("SameAttributeName", "Order Attribute and Scale Attribute are both '{0}'; each needs its own name (or None to not write it)."),
			FText::FromName(Settings->Attributes.OrderAttribute)));
	}

	UPCGVoxelPatternData* Data = FPCGContext::NewObject_AnyThread<UPCGVoxelPatternData>(Context);
	Data->Pattern = *Pattern;
	Data->Priority = Settings->Order;
	Data->SelectorScope = Settings->SelectorScope;
	Data->Scale = Settings->Scale;
	Data->WriteSettingsAttributes(Settings->Attributes);
	// Optional: an empty pin is no selector, not an error.
	PCGUtilsFactories::GetInputFactories<UPCGVoxelSelectorData>(Context, PCGVoxelSelectorPins::SelectorLabel,
		Data->Selectors, PCGVoxelSelectors::GetSelectorTypes(), /*bRequired=*/false);
	for (const UPCGVoxelSelectorData* Selector : Data->Selectors)
	{
		Data->AddDataDependency(Selector);
	}

	FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
	Output.Data = Data;
	Output.Pin = PCGVoxelPatternPins::PatternLabel;
	return true;
}

#undef LOCTEXT_NAMESPACE
