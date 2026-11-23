// Copyright Max Harris

#include "Elements/PCGVoxelApplyPatterns.h"

#include "Data/PCGBasePointData.h"
#include "Elements/PCGVoxelPattern.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Selectors/PCGVoxelSelector.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelPatterns.h"
#include "Voxel/PCGVoxelPointIO.h"

#define LOCTEXT_NAMESPACE "PCGVoxelApplyPatterns"

namespace PCGVoxelApplyPatternsPrivate
{
	const FName UnmatchedPinLabel = TEXT("Unmatched");

	FText PatternLabel(int32 Index, const FPCGVoxelPattern& Pattern)
	{
		return Pattern.Name.IsNone()
			? FText::Format(LOCTEXT("PatternUnnamed", "Pattern {0}"), Index)
			: FText::Format(LOCTEXT("PatternNamed", "Pattern {0} ('{1}')"), Index, FText::FromName(Pattern.Name));
	}
}

#if WITH_EDITOR
FText UPCGVoxelApplyPatternsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | Apply Patterns");
}

FText UPCGVoxelApplyPatternsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Runs the connected voxel patterns over each voxel set, in their Order. Where a pattern fits it takes the cells of "
		"its boxes as chunks: one point per chunk, turned so the pattern's mesh fits, with bounds covering the chunk and "
		"the Mesh Index written, for a mesh collection downstream to pick by and fit. A cell is taken once: earlier "
		"patterns win. Cells no pattern took go to the Unmatched pin by default.");
}
#endif

TArray<FPCGPinProperties> UPCGVoxelApplyPatternsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("InTooltip", "Voxel sets: point data whose points are occupied cells (or chunks) of the grid in @Data.VoxelSize."))
		.SetRequiredPin();
	FPCGPinProperties& Patterns = Pins.Emplace_GetRef(PCGVoxelPatternPins::PatternsLabel, FPCGVoxelPatternDataTypeInfo::AsId(), true, true);
	Patterns.SetRequiredPin();
#if WITH_EDITOR
	Patterns.Tooltip = LOCTEXT("PatternsTooltip", "Voxel | Pattern nodes. They run by their Order (lower first), then in the order they are connected; every pattern runs on every voxel set.");
#endif
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelApplyPatternsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("OutTooltip", "Each input's chunks, one point per chunk, turned to fit their pattern, with Mesh Index written."));
	Pins.Emplace(PCGVoxelApplyPatternsPrivate::UnmatchedPinLabel, EPCGDataType::Point, true, true,
		LOCTEXT("UnmatchedTooltip", "Each input's cells no pattern took (when Unmatched is Pin), unturned, chunks kept, Mesh Index -1."));
	return Pins;
}

FPCGElementPtr UPCGVoxelApplyPatternsSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelApplyPatternsElement>();
}

bool FPCGVoxelApplyPatternsElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace PCGVoxelApplyPatternsPrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGVoxelApplyPatternsElement::ExecuteInternal);
	check(Context);

	const UPCGVoxelApplyPatternsSettings* Settings = Context->GetInputSettings<UPCGVoxelApplyPatternsSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | Apply Patterns");
	auto Fail = [Context, &Title](const FText& Message)
	{
		PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, INDEX_NONE, Message), Context);
		return true;
	};

	// The patterns: by Order, ties in connection order.
	TArray<const UPCGVoxelPatternData*> PatternData;
	for (const FPCGTaggedData& Tagged : Context->InputData.GetInputsByPin(PCGVoxelPatternPins::PatternsLabel))
	{
		if (const UPCGVoxelPatternData* Data = Cast<const UPCGVoxelPatternData>(Tagged.Data))
		{
			PatternData.AddUnique(Data);
		}
		else if (Tagged.Data)
		{
			return Fail(FText::Format(LOCTEXT("NotPattern", "the Patterns pin got data of type {0}; it takes only the output of Voxel | Pattern nodes."),
				FText::FromString(Tagged.Data->GetClass()->GetName())));
		}
	}
	if (PatternData.IsEmpty())
	{
		return Fail(LOCTEXT("NoPatterns", "nothing is connected to the Patterns pin; connect at least one Voxel | Pattern node."));
	}
	PatternData.StableSort([](const UPCGVoxelPatternData& A, const UPCGVoxelPatternData& B) { return A.Priority < B.Priority; });

	// A pattern with no selector is applied everywhere. Selectors are prepared per input, against its grid.
	bool bAnySelector = false;
	for (const UPCGVoxelPatternData* Data : PatternData)
	{
		bAnySelector |= !Data->Selectors.IsEmpty();
	}

	TArray<const FPCGVoxelPattern*> Patterns;
	for (int32 Index = 0; Index < PatternData.Num(); ++Index)
	{
		FText Error;
		if (!PCGVoxelPatterns::Validate(PatternData[Index]->Pattern, Error))
		{
			return Fail(FText::Format(LOCTEXT("PatternError", "{0}: {1}"), PatternLabel(Index, PatternData[Index]->Pattern), Error));
		}
		Patterns.Add(&PatternData[Index]->Pattern);
	}

	// Attribute names, each checked on its own.
	if (Settings->MeshIndexAttribute.IsNone())
	{
		return Fail(LOCTEXT("NoMeshName", "Mesh Index Attribute is empty; give it a name."));
	}
	struct FOptionalName
	{
		bool bOn;
		FName Name;
		FText Setting;
	};
	const FOptionalName Names[] = {
		{ true, Settings->MeshIndexAttribute, LOCTEXT("MeshIndexSetting", "Mesh Index Attribute") },
		{ Settings->bWritePatternIndex, Settings->PatternIndexAttribute, LOCTEXT("PatternIndexSetting", "Pattern Index Attribute") },
		{ Settings->bWritePatternName, Settings->PatternNameAttribute, LOCTEXT("PatternNameSetting", "Pattern Name Attribute") },
		{ Settings->bWritePatternColor, Settings->PatternColorAttribute, LOCTEXT("PatternColorSetting", "Pattern Color Attribute") },
		{ Settings->bWriteIsAdded, Settings->IsAddedAttribute, LOCTEXT("IsAddedSetting", "Is Added Attribute") },
	};
	for (int32 A = 0; A < UE_ARRAY_COUNT(Names); ++A)
	{
		if (!Names[A].bOn)
		{
			continue;
		}
		if (Names[A].Name.IsNone())
		{
			return Fail(FText::Format(LOCTEXT("NoName", "{0} is empty while it is on; give it a name or turn it off."), Names[A].Setting));
		}
		for (int32 B = 0; B < A; ++B)
		{
			if (Names[B].bOn && Names[B].Name == Names[A].Name)
			{
				return Fail(FText::Format(LOCTEXT("SameName", "{0} and {1} are both '{2}'; each needs its own name."),
					Names[B].Setting, Names[A].Setting, FText::FromName(Names[A].Name)));
			}
		}
	}

	const bool bKeepUnmatched = Settings->Unmatched == EPCGVoxelUnmatchedCells::Keep;

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Inputs[InputIndex].Data);
		FPCGVoxelGrid Grid;
		if (!FPCGVoxelGrid::Build(Context, Points, Settings->Grid, Title, InputIndex, Grid))
		{
			continue;
		}

		// Selectors become the limits of the run. They answer for any cell, filled or empty (an Add pattern asks
		// about the empty cells it would fill); a selector that is costly per cell keeps its own answers.
		PCGVoxelPatterns::FLimits Limits;
		TArray<PCGVoxelSelectors::FSet> SelectorSets;
		if (bAnySelector)
		{
			SelectorSets.SetNum(PatternData.Num());
			bool bPrepared = true;
			for (int32 Index = 0; Index < PatternData.Num() && bPrepared; ++Index)
			{
				const UPCGVoxelSelectorData* Failed = nullptr;
				if (!SelectorSets[Index].Prepare(Context, PatternData[Index]->Selectors, Grid, &Failed))
				{
					PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, InputIndex, FText::Format(
						LOCTEXT("SelectorNotPrepared", "{0}: its selector of type {1} could not be evaluated against this input's grid (see the selector's own error)."),
						PatternLabel(Index, PatternData[Index]->Pattern), FText::FromString(GetNameSafe(Failed ? Failed->GetClass() : nullptr)))), Context);
					bPrepared = false;
				}
			}
			if (!bPrepared)
			{
				continue;
			}
			auto IsSelected = [&SelectorSets](int32 Pattern, const FIntVector& Cell)
			{
				return SelectorSets[Pattern].Test(Cell);
			};
			Limits.CanStart = [&SelectorSets, &PatternData, IsSelected](int32 Pattern, const FIntVector& Cell)
			{
				return SelectorSets[Pattern].IsEmpty() || PatternData[Pattern]->SelectorScope == EPCGVoxelPatternSelectorScope::Cells || IsSelected(Pattern, Cell);
			};
			Limits.CanEdit = [&SelectorSets, &PatternData, IsSelected](int32 Pattern, const FIntVector& Cell)
			{
				return SelectorSets[Pattern].IsEmpty() || PatternData[Pattern]->SelectorScope == EPCGVoxelPatternSelectorScope::Origin || IsSelected(Pattern, Cell);
			};
		}

		PCGVoxelPatterns::FResult Result;
		PCGVoxelPatterns::Apply(Grid, Patterns, Limits, Result);
		const int32 NumChunks = Result.Chunks.Num();

		TArray<FPCGVoxelCellOut> Rest;
		if (Settings->Unmatched != EPCGVoxelUnmatchedCells::Drop)
		{
			PCGVoxelPatterns::Remainder(Grid, Result, Rest);
		}

		// Out: the chunks first, then (Keep) the cells no pattern took.
		TArray<FPCGVoxelCellOut> OutCells;
		OutCells.Reserve(NumChunks + (bKeepUnmatched ? Rest.Num() : 0));
		for (const PCGVoxelPatterns::FChunk& Chunk : Result.Chunks)
		{
			OutCells.Add({ Chunk.SourcePoint, Chunk.First, Chunk.Span });
		}
		if (bKeepUnmatched)
		{
			OutCells.Append(Rest);
		}

		auto IsFilled = [&Grid, &Result](const FIntVector& Cell) { return Grid.IsOccupied(Cell) || Result.Added.Contains(Cell); };

		UPCGBasePointData* Out = PCGVoxelPointIO::WriteCells(Context, Points, Grid, OutCells, Settings->Grid.SpanAttribute, /*bWriteSpan=*/true);
		if (!Out)
		{
			continue;
		}

		// Turn each chunk's point to fit its mesh, with bounds covering the chunk in the turned frame, at scale 1.
		{
			const FQuat GridRotation = Grid.GridToWorld.GetRotation();
			TPCGValueRange<FTransform> Transforms = Out->GetTransformValueRange();
			TPCGValueRange<FVector> Mins = Out->GetBoundsMinValueRange();
			TPCGValueRange<FVector> Maxs = Out->GetBoundsMaxValueRange();
			for (int32 I = 0; I < NumChunks; ++I)
			{
				const PCGVoxelPatterns::FChunk& Chunk = Result.Chunks[I];
				FVector GridMin, GridMax;
				if (Chunk.bAdded)
				{
					// A new cell has no surface offset of its own; it must not take its source point's.
					GridMax = Grid.BoxHalfSize(Chunk.Span);
					GridMin = -GridMax;
				}
				else
				{
					// As WriteCells left them: the chunk's box plus the source point's surface offsets, in grid axes.
					const FVector Scale = Transforms[I].GetScale3D().GetAbs();
					GridMin = Mins[I] * Scale;
					GridMax = Maxs[I] * Scale;
				}
				PCGVoxelPatterns::ToMeshFrameBounds(Chunk.MeshToGrid, GridMin, GridMax, Mins[I], Maxs[I]);
				Transforms[I].SetRotation(GridRotation * Chunk.MeshToGrid.ToQuat());
				Transforms[I].SetScale3D(FVector::OneVector);
			}
		}

		if (Settings->bSetPointColor && NumChunks > 0)
		{
			Out->AllocateProperties(Out->GetAllocatedProperties() | EPCGPointNativeProperties::Color);
			TPCGValueRange<FVector4> Colors = Out->GetColorValueRange();
			for (int32 I = 0; I < NumChunks; ++I)
			{
				Colors[I] = FVector4(Patterns[Result.Chunks[I].Pattern]->DebugColor);
			}
		}

		{
			const int32 Num = OutCells.Num();
			TArray<int32> MeshIndex, PatternIndex;
			MeshIndex.Init(INDEX_NONE, Num);
			PatternIndex.Init(INDEX_NONE, Num);
			for (int32 I = 0; I < NumChunks; ++I)
			{
				MeshIndex[I] = Result.Chunks[I].MeshIndex;
				PatternIndex[I] = Result.Chunks[I].Pattern;
			}
			PCGVoxelPointIO::WritePointAttribute<int32>(Out, Settings->MeshIndexAttribute, INDEX_NONE, MeshIndex);
			if (Settings->bWritePatternIndex)
			{
				PCGVoxelPointIO::WritePointAttribute<int32>(Out, Settings->PatternIndexAttribute, INDEX_NONE, PatternIndex);
			}
			if (Settings->bWritePatternName)
			{
				TArray<FName> Values;
				Values.Init(NAME_None, Num);
				for (int32 I = 0; I < NumChunks; ++I)
				{
					Values[I] = Patterns[Result.Chunks[I].Pattern]->Name;
				}
				PCGVoxelPointIO::WritePointAttribute<FName>(Out, Settings->PatternNameAttribute, NAME_None, Values);
			}
			if (Settings->bWritePatternColor)
			{
				TArray<FVector4> Values;
				Values.Init(FVector4(0.0, 0.0, 0.0, 0.0), Num);
				for (int32 I = 0; I < NumChunks; ++I)
				{
					Values[I] = FVector4(Patterns[Result.Chunks[I].Pattern]->DebugColor);
				}
				PCGVoxelPointIO::WritePointAttribute<FVector4>(Out, Settings->PatternColorAttribute, FVector4(0.0, 0.0, 0.0, 0.0), Values);
			}
			if (Settings->bWriteIsAdded)
			{
				TArray<bool> Values;
				Values.Init(false, Num);
				for (int32 I = 0; I < NumChunks; ++I)
				{
					Values[I] = Result.Chunks[I].bAdded;
				}
				PCGVoxelPointIO::WritePointAttribute<bool>(Out, Settings->IsAddedAttribute, false, Values);
			}
		}
		PCGVoxelPointIO::RefreshSurrounded(Points, Out, Settings->Grid.SurroundedAttribute, OutCells, IsFilled);

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
		Output.Data = Out;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;

		if (Settings->Unmatched == EPCGVoxelUnmatchedCells::Pin)
		{
			if (UPCGBasePointData* RestOut = PCGVoxelPointIO::WriteCells(Context, Points, Grid, Rest, Settings->Grid.SpanAttribute, /*bWriteSpan=*/Grid.bChunked))
			{
				TArray<int32> None;
				None.Init(INDEX_NONE, Rest.Num());
				PCGVoxelPointIO::WritePointAttribute<int32>(RestOut, Settings->MeshIndexAttribute, INDEX_NONE, None);
				if (Settings->bWritePatternIndex)
				{
					PCGVoxelPointIO::WritePointAttribute<int32>(RestOut, Settings->PatternIndexAttribute, INDEX_NONE, None);
				}
				PCGVoxelPointIO::RefreshSurrounded(Points, RestOut, Settings->Grid.SurroundedAttribute, Rest, IsFilled);
				FPCGTaggedData& RestOutput = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
				RestOutput.Data = RestOut;
				RestOutput.Pin = UnmatchedPinLabel;
			}
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
