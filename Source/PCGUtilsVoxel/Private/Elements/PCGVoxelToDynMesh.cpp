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

	/**
	 * Per Attribute Value polygroups: one group ID per point. Integer attributes give their value as the ID when
	 * bIntegerValuesAreGroupIds is on; anything else is read as a string and distinct strings are numbered in the order
	 * their first point appears. Logs the exact failure and returns false.
	 */
	bool ReadPolygroups(FPCGContext* Context, const UPCGVoxelToDynMeshSettings& Settings, const UPCGBasePointData* Points,
		const FPCGVoxelGrid& Grid, const FText& Title, int32 InputIndex, TArray<int32>& OutGroups)
	{
		const FText Label = LOCTEXT("PolygroupAttributeLabel", "Polygroup Attribute");
		const FPCGAttributePropertyInputSelector Fixed = Settings.PolygroupAttribute.CopyAndFixLast(Points);
		const TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Points, Fixed);
		if (!Accessor)
		{
			PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, InputIndex, FText::Format(
				LOCTEXT("PolygroupMissing", "{0} '{1}' does not exist on the points; Polygroups is Per Attribute Value, which needs it."),
				Label, Fixed.GetDisplayText())), Context);
			return false;
		}
		const int16 Type = Accessor->GetUnderlyingType();
		const bool bInteger = Type == static_cast<int16>(EPCGMetadataTypes::Integer32) || Type == static_cast<int16>(EPCGMetadataTypes::Integer64);
		const int32 NumPoints = Points->GetNumPoints();
		OutGroups.SetNumZeroed(NumPoints);

		if (bInteger && Settings.bIntegerValuesAreGroupIds)
		{
			TArray<int64> Values;
			if (!PCGVoxelPointIO::ReadPointValues<int64>(Context, Points, Settings.PolygroupAttribute, Values, Title, InputIndex,
				Label, LOCTEXT("PolygroupIntType", "an integer")))
			{
				return false;
			}
			for (int32 Point = 0; Point < NumPoints; ++Point)
			{
				if (!Grid.PointUsed[Point])
				{
					continue;
				}
				if (Values[Point] < 0 || Values[Point] > MAX_int32)
				{
					PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, InputIndex, FText::Format(
						LOCTEXT("PolygroupRange", "point {0}: {1} '{2}' is {3}; with Integer Values Are Group IDs on, a group ID must be from 0 to {4}. Turn it off to number the values instead."),
						FText::AsNumber(Point), Label, Fixed.GetDisplayText(), FText::AsNumber(Values[Point]), FText::AsNumber(MAX_int32))), Context);
					return false;
				}
				OutGroups[Point] = static_cast<int32>(Values[Point]);
			}
			return true;
		}

		TArray<FString> Values;
		if (!PCGVoxelPointIO::ReadPointValues<FString>(Context, Points, Settings.PolygroupAttribute, Values, Title, InputIndex,
			Label, LOCTEXT("PolygroupAnyType", "a type that converts to a string")))
		{
			return false;
		}
		TMap<FString, int32> GroupOfValue;
		for (int32 Point = 0; Point < NumPoints; ++Point)
		{
			if (Grid.PointUsed[Point])
			{
				OutGroups[Point] = GroupOfValue.FindOrAdd(Values[Point], GroupOfValue.Num());
			}
		}
		return true;
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
	 * Keeps every used point's surface the right way out. On each axis a point's two faces sit at Half + OffsetMax and
	 * -(Half + OffsetMin) from its centre; the point turns inside out only when they cross (thickness 0 or less), not
	 * when one face passes the centre. Only sides that show count: a hidden side has no face, so the opposite face is
	 * measured against the natural box side there (its offset, for example a seam moved by Voxel | Shift Seams,
	 * changes nothing).
	 *
	 * A point that crosses is repaired, not refused, as Repair says: its faces meet halfway between the crossed faces
	 * (or, with one side hidden, the shown face comes back to the hidden side), a thousandth of a cell apart for Thin
	 * Slab, together for Zero Thickness; One-Sided Plane also drops the opposite face and the side walls (OutDropped).
	 * One warning per input names how many points were repaired and the first few exactly.
	 */
	void RepairInvertedOffsets(FPCGContext* Context, const FPCGVoxelGrid& Grid, EPCGVoxelOffsetRepair Repair,
		TArray<FVector>& OffsetMin, TArray<FVector>& OffsetMax, TArray<uint8>& OutDropped, const FText& Title, int32 InputIndex)
	{
		constexpr int32 MaxListed = 5;
		const TCHAR* AxisNames[3] = { TEXT("X"), TEXT("Y"), TEXT("Z") };
		int32 NumRepaired = 0;
		int32 NumFellBack = 0;
		TArray<FString> Listed;
		for (int32 Point = 0; Point < Grid.NumPoints(); ++Point)
		{
			if (!Grid.PointUsed[Point])
			{
				continue;
			}
			const FVector Half = Grid.BoxHalfSize(Grid.PointSpan[Point]);
			int32 RepairedAxes = 0;
			int32 RepairedAxis = INDEX_NONE;
			bool bKeepMax = true;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const bool bMinShows = SideHasSurface(Grid, Point, Axis, false);
				const bool bMaxShows = SideHasSurface(Grid, Point, Axis, true);
				if (!bMinShows && !bMaxShows)
				{
					continue;
				}
				// Face positions from the point's centre along the axis (a hidden side stays at the natural box side).
				const double MaxFace = Half[Axis] + (bMaxShows ? OffsetMax[Point][Axis] : 0.0);
				const double MinFace = -Half[Axis] - (bMinShows ? OffsetMin[Point][Axis] : 0.0);
				const double Thickness = MaxFace - MinFace;
				if (Thickness > 0.0)
				{
					continue;
				}
				const double MinThickness = Repair == EPCGVoxelOffsetRepair::ThinSlab ? 1e-3 * Grid.VoxelSize[Axis] : 0.0;
				double NewMax = MaxFace;
				double NewMin = MinFace;
				if (bMinShows && bMaxShows)
				{
					const double Middle = 0.5 * (MaxFace + MinFace);
					NewMax = Middle + 0.5 * MinThickness;
					NewMin = Middle - 0.5 * MinThickness;
				}
				else if (bMaxShows)
				{
					NewMax = MinFace + MinThickness;
				}
				else
				{
					NewMin = MaxFace - MinThickness;
				}
				if (bMaxShows)
				{
					OffsetMax[Point][Axis] = NewMax - Half[Axis];
				}
				if (bMinShows)
				{
					OffsetMin[Point][Axis] = -NewMin - Half[Axis];
				}
				if (Listed.Num() < MaxListed)
				{
					Listed.Add(FString::Printf(TEXT("point %d on %s: its +%s face at %g and -%s face at %g from its centre (%s) leave it %g thick"),
						Point, AxisNames[Axis], AxisNames[Axis], MaxFace, AxisNames[Axis], MinFace,
						bMinShows && bMaxShows ? TEXT("both sides show") : (bMaxShows ? TEXT("only the + side shows") : TEXT("only the - side shows")),
						Thickness));
				}
				++RepairedAxes;
				RepairedAxis = Axis;
				bKeepMax = bMaxShows;
			}
			if (RepairedAxes == 0)
			{
				continue;
			}
			++NumRepaired;
			if (Repair == EPCGVoxelOffsetRepair::OneSidedPlane)
			{
				if (RepairedAxes > 1)
				{
					++NumFellBack;
					continue;
				}
				if (OutDropped.IsEmpty())
				{
					OutDropped.SetNumZeroed(Grid.NumPoints());
				}
				// Keep the one face; drop the opposite face and the four walls round it.
				const int32 KeptDir = RepairedAxis * 2 + (bKeepMax ? 0 : 1);   // the mesher numbers + sides even, - sides odd
				OutDropped[Point] = static_cast<uint8>(0x3F & ~(1 << KeptDir));
			}
		}
		if (NumRepaired > 0)
		{
			const FText RepairName = Repair == EPCGVoxelOffsetRepair::ThinSlab ? LOCTEXT("RepairSlab", "collapsed to a slab a thousandth of a cell thick")
				: (Repair == EPCGVoxelOffsetRepair::ZeroThickness ? LOCTEXT("RepairZero", "collapsed to zero thickness")
				: LOCTEXT("RepairPlane", "turned into a one-sided plane"));
			const FText FellBack = NumFellBack > 0
				? FText::Format(LOCTEXT("FellBack", " {0} of them cross on more than one axis, so they were collapsed to zero thickness instead."), FText::AsNumber(NumFellBack))
				: FText::GetEmpty();
			PCGLog::LogWarningOnGraph(PCGVoxel::FormatError(Title, InputIndex, FText::Format(
				LOCTEXT("OffsetsRepaired", "{0} of {1} points have a surface offset that crosses their own opposite face (a thickness of 0 or less on an axis), "
					"which would turn them inside out; each was {2} (Offset Repair).{3} {4}{5}"),
				FText::AsNumber(NumRepaired), FText::AsNumber(Grid.NumPoints()), RepairName, FellBack, FText::FromString(FString::Join(Listed, TEXT("; "))),
				NumRepaired > Listed.Num() ? FText::Format(LOCTEXT("AndMore", "; and {0} more."), FText::AsNumber(NumRepaired - Listed.Num())) : FText::FromString(TEXT(".")))),
				Context);
		}
	}
}

namespace PCGVoxelToDynMeshPrivate
{
	/**
	 * The bevel amount of each edge kind (convex, concave) and facing (top, side, bottom) from the active Bevel
	 * Controls. Logs an error naming the setting and returns false when one is outside 0 to 1.
	 */
	bool ReadBevelAmounts(FPCGContext* Context, const UPCGVoxelToDynMeshSettings& Settings, const FText& Title, float (&OutAmounts)[2][3])
	{
		struct FSource { float Value; FText Name; int32 Kind; int32 Facing; };
		TArray<FSource, TInlineAllocator<6>> Sources;
		using namespace PCGVoxelBevel;
		switch (Settings.BevelControls)
		{
		case EPCGVoxelBevelControls::Single:
			for (int32 Kind = 0; Kind < 2; ++Kind)
			{
				for (int32 Facing = 0; Facing < 3; ++Facing)
				{
					Sources.Add({ Settings.BevelAmount, LOCTEXT("BevelAmountName", "Bevel Amount"), Kind, Facing });
				}
			}
			break;
		case EPCGVoxelBevelControls::ConvexConcave:
			for (int32 Facing = 0; Facing < 3; ++Facing)
			{
				Sources.Add({ Settings.ConvexBevel, LOCTEXT("ConvexBevelName", "Convex Bevel"), Convex, Facing });
				Sources.Add({ Settings.ConcaveBevel, LOCTEXT("ConcaveBevelName", "Concave Bevel"), Concave, Facing });
			}
			break;
		default:
			Sources.Add({ Settings.ConvexTopBevel, LOCTEXT("ConvexTopName", "Convex Top Bevel"), Convex, Top });
			Sources.Add({ Settings.ConvexSideBevel, LOCTEXT("ConvexSideName", "Convex Side Bevel"), Convex, Side });
			Sources.Add({ Settings.ConvexBottomBevel, LOCTEXT("ConvexBottomName", "Convex Bottom Bevel"), Convex, Bottom });
			Sources.Add({ Settings.ConcaveTopBevel, LOCTEXT("ConcaveTopName", "Concave Top Bevel"), Concave, Top });
			Sources.Add({ Settings.ConcaveSideBevel, LOCTEXT("ConcaveSideName", "Concave Side Bevel"), Concave, Side });
			Sources.Add({ Settings.ConcaveBottomBevel, LOCTEXT("ConcaveBottomName", "Concave Bottom Bevel"), Concave, Bottom });
			break;
		}
		for (const FSource& Source : Sources)
		{
			if (!(Source.Value >= 0.0f && Source.Value <= 1.0f))
			{
				PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, INDEX_NONE, FText::Format(
					LOCTEXT("BevelRange", "{0} is {1}; it must be from 0 to 1 (a fraction of a cell)."),
					Source.Name, FText::AsNumber(Source.Value))), Context);
				return false;
			}
			OutAmounts[Source.Kind][Source.Facing] = Source.Value;
		}
		return true;
	}
}

UPCGVoxelToDynMeshSettings::UPCGVoxelToDynMeshSettings()
{
	ColorAttribute.SetPointProperty(EPCGPointProperties::Color);
	PolygroupAttribute.SetAttributeName(TEXT("Polygroup"));
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
		"empty cell. Shape Bevel cuts convex edges and fills concave ones, with separate amounts by edge kind and facing if wanted. "
		"Materials, vertex colors (hard or smooth), UVs and a manifold-extrude style surface offset are optional. "
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

	float BevelAmounts[2][3] = {};
	if (Settings->Shape == EPCGVoxelMeshShape::Bevel && !ReadBevelAmounts(Context, *Settings, Title, BevelAmounts))
	{
		return true;
	}

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
		const bool bBevel = Settings->Shape == EPCGVoxelMeshShape::Bevel;

		// Bevel scale.
		if (bBevel && Settings->bUseBevelScaleAttribute)
		{
			FPCGAttributePropertyInputSelector Selector;
			Selector.SetAttributeName(Settings->BevelScaleAttribute);
			TArray<double> Scales;
			if (!PCGVoxelPointIO::ReadPointValues<double>(Context, Points, Selector, Scales, Title, Index,
				LOCTEXT("BevelScaleLabel", "Bevel Scale Attribute"), LOCTEXT("BevelScaleType", "a float")))
			{
				continue;
			}
			bool bScaleFailed = false;
			MeshInput.BevelScale.SetNumUninitialized(Scales.Num());
			for (int32 Point = 0; Point < Scales.Num(); ++Point)
			{
				if (Grid.PointUsed[Point] && !(Scales[Point] >= 0.0))
				{
					PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, Index, FText::Format(
						LOCTEXT("BevelScaleNegative", "point {0}: '{1}' is {2}; a bevel scale must be 0 or greater."),
						FText::AsNumber(Point), FText::FromName(Settings->BevelScaleAttribute), FText::AsNumber(Scales[Point]))), Context);
					bScaleFailed = true;
					break;
				}
				MeshInput.BevelScale[Point] = static_cast<float>(Scales[Point]);
			}
			if (bScaleFailed)
			{
				continue;
			}
		}

		// Surface offset.
		if (bBevel)
		{
			// Not used with Bevel.
		}
		else if (Settings->SurfaceOffset == EPCGVoxelSurfaceOffset::FromPointBounds)
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
		if (!MeshInput.OffsetMin.IsEmpty())
		{
			RepairInvertedOffsets(Context, Grid, Settings->OffsetRepair, MeshInput.OffsetMin, MeshInput.OffsetMax, MeshInput.DroppedFaces, Title, Index);
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

		// Polygroups from an attribute.
		if (Settings->Polygroups == EPCGVoxelPolygroupMode::PerAttributeValue
			&& !ReadPolygroups(Context, *Settings, Points, Grid, Title, Index, MeshInput.Polygroup))
		{
			continue;
		}

		FPCGVoxelMeshOptions Options;
		Options.bCollapseCoplanarFaces = Settings->bCollapseCoplanarFaces && !bBevel;
		Options.bBevel = bBevel;
		Options.bExactOffsets = Settings->OffsetBlend == EPCGVoxelOffsetBlend::Exact;
		FMemory::Memcpy(Options.BevelAmount, BevelAmounts, sizeof(BevelAmounts));
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
