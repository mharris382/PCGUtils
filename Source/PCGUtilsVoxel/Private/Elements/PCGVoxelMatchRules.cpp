// Copyright Max Harris

#include "Elements/PCGVoxelMatchRules.h"

#include "Data/PCGBasePointData.h"
#include "Helpers/PCGHelpers.h"
#include "Math/RandomStream.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelPointIO.h"

#define LOCTEXT_NAMESPACE "PCGVoxelMatchRules"

namespace PCGVoxelMatchRulesPrivate
{
	const FName UnmatchedLabel = TEXT("Unmatched");

	/** Face steps in mask-bit order: +X, -X, +Y, -Y, +Z, -Z (as Cell Properties' Open Sides). */
	const FIntVector FaceStep[6] = {
		FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) };

	FText EntryLabel(int32 Index, const FPCGVoxelRuleEntry& Entry)
	{
		return Entry.Name.IsNone()
			? FText::Format(LOCTEXT("EntryUnnamed", "Entry {0}"), Index)
			: FText::Format(LOCTEXT("EntryNamed", "Entry {0} ('{1}')"), Index, FText::FromName(Entry.Name));
	}
}

UPCGVoxelMatchRulesSettings::UPCGVoxelMatchRulesSettings() = default;

#if WITH_EDITOR
FText UPCGVoxelMatchRulesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | Match Rules");
}

FText UPCGVoxelMatchRulesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Rule tiles for voxels. Each cell is tested against an ordered list of entries (which faces are open, thin...); "
		"each entry may turn (about up, or any cube rotation) so one mesh fits every matching cell. The first entry that "
		"fits turns the point to fit and writes its Mesh Index, for a mesh collection downstream to pick by. Rules are written "
		"in the mesh's frame: +Z up, side layouts start at +Y. Unmatched cells go to the Unmatched pin by default.");
}
#endif

TArray<FPCGPinProperties> UPCGVoxelMatchRulesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("InTooltip", "Voxel sets: point data whose points are occupied cells (or chunks) of the grid in @Data.VoxelSize."))
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelMatchRulesSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("OutTooltip", "Each input's matched cells, one point per cell, turned to fit their rule, with Mesh Index (and Rule Index) written."));
	Pins.Emplace(PCGVoxelMatchRulesPrivate::UnmatchedLabel, EPCGDataType::Point, true, true,
		LOCTEXT("UnmatchedTooltip", "Each input's cells no entry fits (when Unmatched is Pin), unturned, Mesh Index -1."));
	return Pins;
}

FPCGElementPtr UPCGVoxelMatchRulesSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelMatchRulesElement>();
}

bool FPCGVoxelMatchRulesElement::CanExecuteOnlyOnMainThread(FPCGContext* Context) const
{
	const UPCGVoxelMatchRulesSettings* Settings = Context ? Context->GetInputSettings<UPCGVoxelMatchRulesSettings>() : nullptr;
	return Settings && Settings->RulesSource == EPCGVoxelRulesSource::Asset;
}

bool FPCGVoxelMatchRulesElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace PCGVoxelMatchRulesPrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGVoxelMatchRulesElement::ExecuteInternal);
	check(Context);

	const UPCGVoxelMatchRulesSettings* Settings = Context->GetInputSettings<UPCGVoxelMatchRulesSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | Match Rules");
	auto Fail = [Context, &Title](const FText& Message)
	{
		PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, INDEX_NONE, Message), Context);
		return true;
	};

	// The mapping, from wherever Rules Source says.
	FPCGVoxelRuleMapping FromText;
	const FPCGVoxelRuleMapping* Mapping = &Settings->Rules;
	if (Settings->RulesSource == EPCGVoxelRulesSource::Asset)
	{
		if (Settings->RulesAsset.IsNull())
		{
			return Fail(LOCTEXT("NoAsset", "Rules Source is Asset, but Rules Asset is not set."));
		}
		const UPCGVoxelRuleMappingAsset* Asset = Settings->RulesAsset.LoadSynchronous();
		if (!Asset)
		{
			return Fail(FText::Format(LOCTEXT("AssetLoad", "Rules Asset '{0}' could not be loaded."),
				FText::FromString(Settings->RulesAsset.ToString())));
		}
		Mapping = &Asset->Mapping;
	}
	else if (Settings->RulesSource == EPCGVoxelRulesSource::Text)
	{
		FString Error;
		if (!PCGVoxelRules::ParseMapping(Settings->RulesText, FromText, Error))
		{
			return Fail(FText::Format(LOCTEXT("TextError", "Rules Text, {0}."), FText::FromString(Error)));
		}
		Mapping = &FromText;
	}

	// Each entry checked on its own.
	bool bAnyEnabled = false;
	for (int32 EntryIndex = 0; EntryIndex < Mapping->Entries.Num(); ++EntryIndex)
	{
		const FPCGVoxelRuleEntry& Entry = Mapping->Entries[EntryIndex];
		if (!Entry.bEnabled)
		{
			continue;
		}
		bAnyEnabled = true;
		if (Entry.MeshIndex < 0)
		{
			return Fail(FText::Format(LOCTEXT("NegativeIndex", "{0} has Mesh Index {1}; it must be 0 or more (-1 marks unmatched cells)."),
				EntryLabel(EntryIndex, Entry), Entry.MeshIndex));
		}
		for (int32 RuleIndex = 0; RuleIndex < Entry.Rule.Rules.Num(); ++RuleIndex)
		{
			if (!Entry.Rule.Rules[RuleIndex].GetPtr<FPCGVoxelRule>())
			{
				return Fail(FText::Format(LOCTEXT("NoType", "{0}, rule {1} has no type; pick one (Faces, Thin) or remove it."),
					EntryLabel(EntryIndex, Entry), RuleIndex));
			}
		}
	}
	if (!bAnyEnabled)
	{
		return Fail(FText::Format(LOCTEXT("NoEntries", "The rule mapping ({0}) has no enabled entries."),
			StaticEnum<EPCGVoxelRulesSource>()->GetDisplayNameTextByValue(static_cast<int64>(Settings->RulesSource))));
	}

	if (Settings->MeshIndexAttribute.IsNone())
	{
		return Fail(LOCTEXT("NoMeshName", "Mesh Index Attribute is empty; give it a name."));
	}
	if (Settings->bWriteRuleIndex && Settings->RuleIndexAttribute.IsNone())
	{
		return Fail(LOCTEXT("NoRuleName", "Rule Index Attribute is empty while it is on; give it a name or turn it off."));
	}
	if (Settings->bWriteRuleIndex && Settings->RuleIndexAttribute == Settings->MeshIndexAttribute)
	{
		return Fail(FText::Format(LOCTEXT("SameName", "Mesh Index Attribute and Rule Index Attribute are both '{0}'; each needs its own name."),
			FText::FromName(Settings->MeshIndexAttribute)));
	}

	PCGVoxelRules::FTable Table;
	PCGVoxelRules::BuildTable(*Mapping, Table);
	const TArray<PCGVoxelRules::FRotation>& Rotations = PCGVoxelRules::Rotations();
	const int32 Seed = Context->GetSeed();

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Inputs[InputIndex].Data);
		FPCGVoxelGrid Grid;
		if (!FPCGVoxelGrid::Build(Context, Points, Settings->Grid, Title, InputIndex, Grid))
		{
			continue;
		}

		// Sort every cell (in point order, a chunk's cells in Z, Y, X order) into Out or Unmatched.
		TArray<FPCGVoxelCellOut> OutCells, UnmatchedCells;
		TArray<int32> OutEntry;
		TArray<uint8> OutMask;
		for (int32 Point = 0; Point < Grid.NumPoints(); ++Point)
		{
			if (!Grid.PointUsed[Point])
			{
				continue;
			}
			const FIntVector First = Grid.PointFirst[Point];
			const FIntVector Span = Grid.PointSpan[Point];
			for (int32 Z = 0; Z < Span.Z; ++Z)
			{
				for (int32 Y = 0; Y < Span.Y; ++Y)
				{
					for (int32 X = 0; X < Span.X; ++X)
					{
						const FIntVector Cell = First + FIntVector(X, Y, Z);
						uint8 Mask = 0;
						for (int32 Dir = 0; Dir < 6; ++Dir)
						{
							if (!Grid.IsOccupied(Cell + FaceStep[Dir]))
							{
								Mask |= 1 << Dir;
							}
						}
						if (Mask == 0 && Settings->bDropInsideCells)
						{
							continue;
						}
						const int32 Entry = Table.Entry[Mask];
						if (Entry != INDEX_NONE || Settings->Unmatched == EPCGVoxelUnmatchedCells::Keep)
						{
							OutCells.Add({ Point, Cell, FIntVector(1) });
							OutEntry.Add(Entry);
							OutMask.Add(Mask);
						}
						else if (Settings->Unmatched == EPCGVoxelUnmatchedCells::Pin)
						{
							UnmatchedCells.Add({ Point, Cell, FIntVector(1) });
						}
					}
				}
			}
		}

		UPCGBasePointData* Out = PCGVoxelPointIO::WriteCells(Context, Points, Grid, OutCells, Settings->Grid.SpanAttribute, /*bWriteSpan=*/false);
		if (!Out)
		{
			continue;
		}

		// Turn each matched point to fit and give it bounds covering its cell in the turned frame.
		{
			const FQuat GridRotation = Grid.GridToWorld.GetRotation();
			TPCGValueRange<FTransform> Transforms = Out->GetTransformValueRange();
			TPCGValueRange<FVector> Mins = Out->GetBoundsMinValueRange();
			TPCGValueRange<FVector> Maxs = Out->GetBoundsMaxValueRange();
			const TConstPCGValueRange<int32> Seeds = Out->GetConstSeedValueRange();
			for (int32 I = 0; I < OutCells.Num(); ++I)
			{
				if (OutEntry[I] == INDEX_NONE)
				{
					continue;
				}
				const TArray<uint8>& Tied = Table.Tied[OutMask[I]];
				int32 Pick = 0;
				if (Mapping->Entries[OutEntry[I]].Rule.Spin == EPCGVoxelRuleSpin::Random && Tied.Num() > 1)
				{
					Pick = FRandomStream(PCGHelpers::ComputeSeed(Seed, Seeds[I])).RandHelper(Tied.Num());
				}
				const PCGVoxelRules::FRotation& Rotation = Rotations[Tied[Pick]];

				const FVector Scale = Transforms[I].GetScale3D();
				const FVector Half(
					FMath::Abs(FVector(Rotation.X).Dot(Grid.VoxelSize)) * 0.5,
					FMath::Abs(FVector(Rotation.Y).Dot(Grid.VoxelSize)) * 0.5,
					FMath::Abs(FVector(Rotation.Z).Dot(Grid.VoxelSize)) * 0.5);
				const FVector Local(
					Half.X / FMath::Max(FMath::Abs(Scale.X), UE_KINDA_SMALL_NUMBER),
					Half.Y / FMath::Max(FMath::Abs(Scale.Y), UE_KINDA_SMALL_NUMBER),
					Half.Z / FMath::Max(FMath::Abs(Scale.Z), UE_KINDA_SMALL_NUMBER));
				Transforms[I].SetRotation(GridRotation * Rotation.ToQuat());
				Mins[I] = -Local;
				Maxs[I] = Local;
			}
		}

		TArray<int32> MeshIndex, RuleIndex;
		MeshIndex.SetNumUninitialized(OutCells.Num());
		RuleIndex.SetNumUninitialized(OutCells.Num());
		for (int32 I = 0; I < OutCells.Num(); ++I)
		{
			MeshIndex[I] = OutEntry[I] == INDEX_NONE ? INDEX_NONE : Mapping->Entries[OutEntry[I]].MeshIndex;
			RuleIndex[I] = OutEntry[I];
		}
		PCGVoxelPointIO::WritePointAttribute<int32>(Out, Settings->MeshIndexAttribute, INDEX_NONE, MeshIndex);
		if (Settings->bWriteRuleIndex)
		{
			PCGVoxelPointIO::WritePointAttribute<int32>(Out, Settings->RuleIndexAttribute, INDEX_NONE, RuleIndex);
		}
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
		Output.Data = Out;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;

		if (Settings->Unmatched == EPCGVoxelUnmatchedCells::Pin)
		{
			if (UPCGBasePointData* Rest = PCGVoxelPointIO::WriteCells(Context, Points, Grid, UnmatchedCells, Settings->Grid.SpanAttribute, /*bWriteSpan=*/false))
			{
				TArray<int32> None;
				None.Init(INDEX_NONE, UnmatchedCells.Num());
				PCGVoxelPointIO::WritePointAttribute<int32>(Rest, Settings->MeshIndexAttribute, INDEX_NONE, None);
				if (Settings->bWriteRuleIndex)
				{
					PCGVoxelPointIO::WritePointAttribute<int32>(Rest, Settings->RuleIndexAttribute, INDEX_NONE, None);
				}
				FPCGTaggedData& RestOutput = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
				RestOutput.Data = Rest;
				RestOutput.Pin = UnmatchedLabel;
			}
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
