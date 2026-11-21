// Copyright Max Harris

#include "Elements/PCGVoxelPattern.h"

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
	};
}

void UPCGVoxelPatternData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);

	// Every authored value of the pattern, as text: any edit changes the Crc, so Apply Patterns reruns.
	FString Text;
	FPCGVoxelPattern::StaticStruct()->ExportText(Text, &Pattern, nullptr, nullptr, PPF_None, nullptr);
	Ar << Text;
	int32 LocalOrder = Order;
	Ar << LocalOrder;
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
	};
}

void UPCGVoxelPatternSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	using namespace PCGVoxelPatternPrivate;
	switch (PreconfiguredInfo.PreconfiguredIndex)
	{
	case TopEdgePreset: Pattern = PCGVoxelPatterns::Presets::TopEdge(); break;
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

	UPCGVoxelPatternData* Data = FPCGContext::NewObject_AnyThread<UPCGVoxelPatternData>(Context);
	Data->Pattern = *Pattern;
	Data->Order = Settings->Order;

	FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
	Output.Data = Data;
	Output.Pin = PCGVoxelPatternPins::PatternLabel;
	return true;
}

#undef LOCTEXT_NAMESPACE
