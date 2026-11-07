// Copyright Max Harris

#include "Elements/PCGVoxelToDynMesh.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelMesher.h"
#include "Voxel/PCGVoxelPointIO.h"

#define LOCTEXT_NAMESPACE "PCGVoxelToDynMesh"

namespace PCGVoxelToDynMeshPrivate
{
	const TCHAR* SideName(int32 Axis, bool bMax)
	{
		static const TCHAR* Names[6] = { TEXT("-X"), TEXT("+X"), TEXT("-Y"), TEXT("+Y"), TEXT("-Z"), TEXT("+Z") };
		return Names[Axis * 2 + (bMax ? 1 : 0)];
	}

	/** Whether any cell on that side of the point's box has an empty neighbour across it, so the side shows. */
	bool SideHasSurface(const FPCGVoxelGrid& Grid, int32 Point, int32 Axis, bool bMax)
	{
		const FIntVector First = Grid.PointFirst[Point];
		const FIntVector Span = Grid.PointSpan[Point];
		FIntVector Step(0);
		Step[Axis] = bMax ? 1 : -1;
		FIntVector Size = Span;
		Size[Axis] = 1;
		FIntVector Start = First;
		Start[Axis] = bMax ? First[Axis] + Span[Axis] - 1 : First[Axis];
		for (int32 Z = 0; Z < Size.Z; ++Z)
		{
			for (int32 Y = 0; Y < Size.Y; ++Y)
			{
				for (int32 X = 0; X < Size.X; ++X)
				{
					if (!Grid.IsOccupied(Start + FIntVector(X, Y, Z) + Step))
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	/**
	 * Every used point's offset must keep its surface the right way out: greater than minus half its extent. Only sides
	 * that show count; offsets on hidden sides (for example seams moved by Voxel | Shift Seams) change nothing.
	 */
	bool ValidateOffsets(FPCGContext* Context, const FPCGVoxelGrid& Grid, const TArray<FVector>& OffsetMin, const TArray<FVector>& OffsetMax,
		const FText& Title, int32 InputIndex)
	{
		for (int32 Point = 0; Point < Grid.NumPoints(); ++Point)
		{
			if (!Grid.PointUsed[Point])
			{
				continue;
			}
			const FVector Half = Grid.BoxHalfSize(Grid.PointSpan[Point]);
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				for (const bool bMax : { false, true })
				{
					const double Offset = bMax ? OffsetMax[Point][Axis] : OffsetMin[Point][Axis];
					if (Offset <= -Half[Axis] && SideHasSurface(Grid, Point, Axis, bMax))
					{
						PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, InputIndex, FText::Format(
							LOCTEXT("OffsetTooDeep", "point {0}: its surface offset on the {1} side is {2}; it must be greater than {3} (minus half its extent on that axis), or the surface turns inside out."),
							FText::AsNumber(Point), FText::FromString(SideName(Axis, bMax)), FText::AsNumber(Offset), FText::AsNumber(-Half[Axis]))), Context);
						return false;
					}
				}
			}
		}
		return true;
	}
}

UPCGVoxelToDynMeshSettings::UPCGVoxelToDynMeshSettings()
{
	ColorAttribute.SetPointProperty(EPCGPointProperties::Color);
}

#if WITH_EDITOR
FText UPCGVoxelToDynMeshSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | To DynMesh");
}

FText UPCGVoxelToDynMeshSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Builds one closed DynMesh per voxel set (point data with @Data.VoxelSize): a face for every cell side next to an "
		"empty cell. Materials, vertex colors (hard or smooth), UVs and a manifold-extrude style surface offset are optional. "
		"Collapse Coplanar Faces merges equal faces without leaving cracks. A chunked set gives the same mesh as its expanded form.");
}
#endif

TArray<FPCGPinProperties> UPCGVoxelToDynMeshSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("InTooltip", "Voxel sets: point data whose points are occupied cells (or chunks) of the grid in @Data.VoxelSize. One DynMesh per input."))
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelToDynMeshSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, true, true,
		LOCTEXT("OutTooltip", "One DynMesh per input voxel set, with that input's tags and (by default) its @Data attributes."));
	return Pins;
}

FPCGElementPtr UPCGVoxelToDynMeshSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelToDynMeshElement>();
}

bool FPCGVoxelToDynMeshElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace PCGVoxelToDynMeshPrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGVoxelToDynMeshElement::ExecuteInternal);
	check(Context);

	const UPCGVoxelToDynMeshSettings* Settings = Context->GetInputSettings<UPCGVoxelToDynMeshSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | To DynMesh");

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	if (Inputs.IsEmpty())
	{
		return true;
	}
	const FTransform OutputSpace = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(Context, nullptr, Settings->bConvertToLocalSpace);

	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Inputs[Index].Data);
		FPCGVoxelGrid Grid;
		if (!FPCGVoxelGrid::Build(Context, Points, Settings->Grid, Title, Index, Grid))
		{
			continue;
		}

		FPCGVoxelMeshInput MeshInput;
		MeshInput.Grid = &Grid;

		// Surface offset.
		if (Settings->SurfaceOffset == EPCGVoxelSurfaceOffset::FromPointBounds)
		{
			Grid.ComputeSurfaceOffsets(Points, MeshInput.OffsetMin, MeshInput.OffsetMax);
		}
		else if (Settings->SurfaceOffset == EPCGVoxelSurfaceOffset::Constant)
		{
			double Distance = 0.0;
			FText Error;
			if (!Settings->SurfaceOffsetDistance.TryReadDataValue(Points, Distance, Error, LOCTEXT("OffsetLabel", "Surface Offset Distance"), Index))
			{
				PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, INDEX_NONE, Error), Context);
				continue;
			}
			MeshInput.OffsetMin.Init(FVector(Distance), Grid.NumPoints());
			MeshInput.OffsetMax.Init(FVector(Distance), Grid.NumPoints());
		}
		if (!MeshInput.OffsetMin.IsEmpty() && !ValidateOffsets(Context, Grid, MeshInput.OffsetMin, MeshInput.OffsetMax, Title, Index))
		{
			continue;
		}

		// Materials.
		TArray<UMaterialInterface*> Materials;
		bool bMaterialsFailed = false;
		if (Settings->MaterialSource == EPCGVoxelMaterialSource::Constant)
		{
			UMaterialInterface* Loaded = Settings->Material.IsNull() ? nullptr : Settings->Material.LoadSynchronous();
			if (!Settings->Material.IsNull() && !Loaded)
			{
				PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, Index, FText::Format(
					LOCTEXT("MaterialNotLoaded", "Material '{0}' could not be loaded."), FText::FromString(Settings->Material.ToString()))), Context);
				continue;
			}
			Materials.Add(Loaded ? Loaded : UMaterial::GetDefaultMaterial(MD_Surface));
		}
		else if (Settings->MaterialSource == EPCGVoxelMaterialSource::Attribute)
		{
			FPCGAttributePropertyInputSelector Selector;
			Selector.SetAttributeName(Settings->MaterialAttribute);
			TArray<FSoftObjectPath> Paths;
			if (!PCGVoxelPointIO::ReadPointValues<FSoftObjectPath>(Context, Points, Selector, Paths, Title, Index,
				LOCTEXT("MaterialAttributeLabel", "Material Attribute"), LOCTEXT("MaterialAttributeType", "a soft object path or a string")))
			{
				continue;
			}
			TMap<FSoftObjectPath, int32> SlotOfPath;
			MeshInput.MaterialSlot.SetNumZeroed(Grid.NumPoints());
			for (int32 Point = 0; Point < Grid.NumPoints() && !bMaterialsFailed; ++Point)
			{
				if (!Grid.PointUsed[Point])
				{
					continue;
				}
				const FSoftObjectPath& Path = Paths[Point];
				if (const int32* Slot = SlotOfPath.Find(Path))
				{
					MeshInput.MaterialSlot[Point] = *Slot;
					continue;
				}
				UMaterialInterface* Loaded = Path.IsNull() ? nullptr : Cast<UMaterialInterface>(Path.TryLoad());
				if (!Loaded)
				{
					PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, Index, FText::Format(Path.IsNull()
						? LOCTEXT("MaterialEmpty", "point {0}: '{1}' is empty; every cell must name a material in Attribute mode.")
						: LOCTEXT("MaterialBad", "point {0}: '{2}' in '{1}' could not be loaded as a material."),
						FText::AsNumber(Point), FText::FromName(Settings->MaterialAttribute), FText::FromString(Path.ToString()))), Context);
					bMaterialsFailed = true;
					break;
				}
				const int32 Slot = Materials.Add(Loaded);
				SlotOfPath.Add(Path, Slot);
				MeshInput.MaterialSlot[Point] = Slot;
			}
		}
		if (bMaterialsFailed)
		{
			continue;
		}

		// Colors.
		if (Settings->VertexColorSource == EPCGVoxelColorSource::Attribute)
		{
			TArray<FVector4> Colors;
			if (!PCGVoxelPointIO::ReadPointValues<FVector4>(Context, Points, Settings->ColorAttribute, Colors, Title, Index,
				LOCTEXT("ColorLabel", "Color Attribute"), LOCTEXT("ColorType", "a Vector4 or convertible to one")))
			{
				continue;
			}
			MeshInput.Color.SetNumUninitialized(Colors.Num());
			for (int32 Point = 0; Point < Colors.Num(); ++Point)
			{
				MeshInput.Color[Point] = FVector4f(Colors[Point]);
			}
		}

		// Atlas tiles.
		if (Settings->UVMode == EPCGVoxelUVMode::AtlasFromAttribute)
		{
			FPCGAttributePropertyInputSelector Selector;
			Selector.SetAttributeName(Settings->AtlasAttribute);
			MeshInput.AtlasRect.SetNumUninitialized(Grid.NumPoints());
			bool bAtlasFailed = false;
			if (Settings->AtlasSource == EPCGVoxelAtlasSource::TileIndex)
			{
				TArray<int32> Tiles;
				if (!PCGVoxelPointIO::ReadPointValues<int32>(Context, Points, Selector, Tiles, Title, Index,
					LOCTEXT("AtlasLabel", "Atlas Attribute"), LOCTEXT("AtlasTileType", "an integer tile index")))
				{
					continue;
				}
				const int32 Columns = FMath::Max(1, Settings->AtlasColumns);
				const int32 Rows = FMath::Max(1, Settings->AtlasRows);
				for (int32 Point = 0; Point < Tiles.Num(); ++Point)
				{
					const int32 Tile = Tiles[Point];
					if (Grid.PointUsed[Point] && (Tile < 0 || Tile >= Columns * Rows))
					{
						PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, Index, FText::Format(
							LOCTEXT("TileRange", "point {0}: tile {1} in '{2}' is outside the atlas; it must be from 0 to {3} (Atlas Columns x Atlas Rows - 1)."),
							FText::AsNumber(Point), FText::AsNumber(Tile), FText::FromName(Settings->AtlasAttribute), FText::AsNumber(Columns * Rows - 1))), Context);
						bAtlasFailed = true;
						break;
					}
					const float Col = static_cast<float>(Tile % Columns);
					const float Row = static_cast<float>(Tile / Columns);
					MeshInput.AtlasRect[Point] = FVector4f(Col / Columns, Row / Rows, (Col + 1.0f) / Columns, (Row + 1.0f) / Rows);
				}
			}
			else
			{
				TArray<FVector4> Rects;
				if (!PCGVoxelPointIO::ReadPointValues<FVector4>(Context, Points, Selector, Rects, Title, Index,
					LOCTEXT("AtlasLabel", "Atlas Attribute"), LOCTEXT("AtlasRectType", "a Vector4 (MinU, MinV, MaxU, MaxV)")))
				{
					continue;
				}
				for (int32 Point = 0; Point < Rects.Num(); ++Point)
				{
					MeshInput.AtlasRect[Point] = FVector4f(Rects[Point]);
				}
			}
			if (bAtlasFailed)
			{
				continue;
			}
		}

		FPCGVoxelMeshOptions Options;
		Options.bCollapseCoplanarFaces = Settings->bCollapseCoplanarFaces;
		Options.ColorTransition = Settings->ColorTransition;
		Options.UVMode = Settings->UVMode;
		Options.UVScale = Settings->UVScale;
		Options.UVChannel = Settings->UVChannel;
		Options.Polygroups = Settings->Polygroups;
		Options.OutputSpace = OutputSpace;

		UE::Geometry::FDynamicMesh3 Mesh;
		PCGVoxelMesher::BuildMesh(MeshInput, Options, Mesh);

		UPCGDynamicMeshData* OutputData = PCGUtilsAsyncObjects::NewObject<UPCGDynamicMeshData>(Context);
		OutputData->Initialize(MoveTemp(Mesh), Materials);
		if (Settings->bCopyDataAttributes)
		{
			if (FPCGMetadataDomain* Domain = PCGUtilsDataDomain::GetMutableDataDomain(OutputData))
			{
				PCGUtilsDataDomain::CopyDataAttributes(Points, *Domain);
			}
		}
		OutputData->TargetActor = Points->TargetActor;

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[Index]);
		Output.Data = OutputData;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
