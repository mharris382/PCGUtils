// Copyright Max Harris

#include "Elements/PCGVoxelPackPoints.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGPointArrayData.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGHelpers.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "Math/RandomStream.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelPointIO.h"

#define LOCTEXT_NAMESPACE "PCGVoxelPackPoints"

namespace PCGVoxelPackPointsPrivate
{
	struct FBoxRotation
	{
		int32 Perm[3];
		FQuat Quat;
	};

	/** The 24 rotations of a cube: local axis i goes to grid axis Perm[i], flipped or not, with determinant +1. */
	const TArray<FBoxRotation>& AllBoxRotations()
	{
		static const TArray<FBoxRotation> Rotations = []
		{
			TArray<FBoxRotation> Out;
			const int32 Perms[6][3] = { {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0} };
			const int32 Parity[6] = { 1, -1, -1, 1, 1, -1 };
			for (int32 P = 0; P < 6; ++P)
			{
				for (int32 Signs = 0; Signs < 8; ++Signs)
				{
					const int32 S[3] = { (Signs & 1) ? -1 : 1, (Signs & 2) ? -1 : 1, (Signs & 4) ? -1 : 1 };
					if (Parity[P] * S[0] * S[1] * S[2] != 1)
					{
						continue;
					}
					FVector Rows[3];
					for (int32 Axis = 0; Axis < 3; ++Axis)
					{
						Rows[Axis] = FVector::ZeroVector;
						Rows[Axis][Perms[P][Axis]] = S[Axis];
					}
					const FMatrix Matrix(FPlane(Rows[0], 0.0), FPlane(Rows[1], 0.0), FPlane(Rows[2], 0.0), FPlane(0.0, 0.0, 0.0, 1.0));
					FBoxRotation& Rotation = Out.AddDefaulted_GetRef();
					Rotation.Perm[0] = Perms[P][0];
					Rotation.Perm[1] = Perms[P][1];
					Rotation.Perm[2] = Perms[P][2];
					Rotation.Quat = FQuat(Matrix).GetNormalized();
				}
			}
			return Out;
		}();
		return Rotations;
	}

	/** One In point on its way to the output. */
	struct FPiece
	{
		int32 Input = 0;
		int32 Point = 0;
		bool bPacked = false;
		FIntVector First = FIntVector::ZeroValue;
		FIntVector Span = FIntVector(1);
		int32 TargetIndex = INDEX_NONE;
		FTransform Original;
		FVector TargetLocation = FVector::ZeroVector;
		FQuat TargetRotation = FQuat::Identity;
		FVector Half = FVector(50.0);
		double PositionSettle = 1.0;
		double RotationSettle = 1.0;

		// Result.
		bool bRemoved = false;
		bool bOverlapping = false;
		double UsedSettle = 1.0;
		FVector Location = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;

		void Pose(double Position, double Turn, FVector& OutLocation, FQuat& OutRotation) const
		{
			OutLocation = FMath::Lerp(Original.GetLocation(), TargetLocation, Position);
			OutRotation = FQuat::Slerp(Original.GetRotation(), TargetRotation, Turn).GetNormalized();
		}
	};

	/** Placed boxes, bucketed by centre for overlap queries. */
	struct FPlacedBoxes
	{
		double BucketSize = 100.0;
		double Shrink = 0.0;
		TMap<FIntVector, TArray<int32>> Buckets;
		TArray<const FPiece*> Pieces;

		FIntVector Bucket(const FVector& Location) const
		{
			return FIntVector(FMath::FloorToInt(Location.X / BucketSize), FMath::FloorToInt(Location.Y / BucketSize), FMath::FloorToInt(Location.Z / BucketSize));
		}

		bool Collides(const FVector& Location, const FQuat& Rotation, const FVector& Half) const
		{
			const FIntVector Center = Bucket(Location);
			const FVector Shrunk = (Half - FVector(Shrink)).ComponentMax(FVector(0.0));
			for (int32 Z = -1; Z <= 1; ++Z)
			{
				for (int32 Y = -1; Y <= 1; ++Y)
				{
					for (int32 X = -1; X <= 1; ++X)
					{
						const TArray<int32>* InBucket = Buckets.Find(Center + FIntVector(X, Y, Z));
						if (!InBucket)
						{
							continue;
						}
						for (const int32 Placed : *InBucket)
						{
							const FPiece& Other = *Pieces[Placed];
							if (PCGVoxelPackPoints::BoxesOverlap(Location, Rotation, Shrunk, Other.Location, Other.Rotation,
								(Other.Half - FVector(Shrink)).ComponentMax(FVector(0.0))))
							{
								return true;
							}
						}
					}
				}
			}
			return false;
		}

		void Add(const FPiece& Piece)
		{
			Buckets.FindOrAdd(Bucket(Piece.Location)).Add(Pieces.Add(&Piece));
		}
	};

	bool ReadSettle(FPCGContext* Context, const UPCGBasePointData* Points, const FPCGVoxelSettleAmount& Amount, int32 NodeSeed, int32 Salt,
		TArray<double>& OutValues, const FText& Title, int32 InputIndex, const FText& SettingName)
	{
		const int32 Num = Points->GetNumPoints();
		switch (Amount.Source)
		{
		case EPCGVoxelSettleSource::Constant:
			OutValues.Init(FMath::Clamp(Amount.Value, 0.0, 1.0), Num);
			return true;
		case EPCGVoxelSettleSource::Random:
		{
			if (Amount.Min > Amount.Max)
			{
				PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, INDEX_NONE, FText::Format(
					LOCTEXT("RandomRange", "{0}: Min is {1} and Max is {2}; Min must not be above Max."),
					SettingName, FText::AsNumber(Amount.Min), FText::AsNumber(Amount.Max))), Context);
				return false;
			}
			const TConstPCGValueRange<int32> Seeds = Points->GetConstSeedValueRange();
			OutValues.SetNumUninitialized(Num);
			for (int32 Point = 0; Point < Num; ++Point)
			{
				FRandomStream Stream(PCGHelpers::ComputeSeed(Seeds[Point], NodeSeed, Salt));
				OutValues[Point] = FMath::Clamp(Amount.Min + (Amount.Max - Amount.Min) * Stream.FRand(), 0.0, 1.0);
			}
			return true;
		}
		default:
		{
			if (!PCGVoxelPointIO::ReadPointValues<double>(Context, Points, Amount.Attribute, OutValues, Title, InputIndex,
				SettingName, LOCTEXT("SettleType", "a number from 0 to 1")))
			{
				return false;
			}
			for (int32 Point = 0; Point < OutValues.Num(); ++Point)
			{
				if (OutValues[Point] < 0.0 || OutValues[Point] > 1.0)
				{
					PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, InputIndex, FText::Format(
						LOCTEXT("SettleRange", "point {0}: {1} is {2}; a settle amount must be from 0 to 1 (it is not clamped)."),
						FText::AsNumber(Point), SettingName, FText::AsNumber(OutValues[Point]))), Context);
					return false;
				}
			}
			return true;
		}
		}
	}

	/** The free box of Span cells nearest to WorldLocation (by centre) within MaxRadius. */
	bool FindFreeBox(const FPCGVoxelGrid& Grid, const TSet<FIntVector>& Claimed, const FVector& WorldLocation, const FIntVector& Span,
		double MaxRadius, FIntVector& OutFirst)
	{
		const FVector Exact = Grid.GridToWorld.InverseTransformPositionNoScale(WorldLocation) / Grid.VoxelSize
			- (FVector(Span) - FVector(1.0)) * 0.5;
		const FIntVector Center(FMath::RoundToInt(Exact.X), FMath::RoundToInt(Exact.Y), FMath::RoundToInt(Exact.Z));
		const FIntVector Lo = Grid.MinCell;
		const FIntVector Hi = Grid.MaxCell - Span + FIntVector(1);
		if (Hi.X < Lo.X || Hi.Y < Lo.Y || Hi.Z < Lo.Z)
		{
			return false;
		}

		auto IsFree = [&](const FIntVector& First)
		{
			for (int32 Z = 0; Z < Span.Z; ++Z)
			{
				for (int32 Y = 0; Y < Span.Y; ++Y)
				{
					for (int32 X = 0; X < Span.X; ++X)
					{
						const FIntVector Cell = First + FIntVector(X, Y, Z);
						if (!Grid.IsOccupied(Cell) || Claimed.Contains(Cell))
						{
							return false;
						}
					}
				}
			}
			return true;
		};

		const double MinVoxel = Grid.VoxelSize.GetMin();
		const int32 MaxShell = FMath::CeilToInt(MaxRadius / MinVoxel) + 1;
		bool bFound = false;
		double Best = MaxRadius;
		auto Consider = [&](const FIntVector& First)
		{
			if (First.X < Lo.X || First.Y < Lo.Y || First.Z < Lo.Z || First.X > Hi.X || First.Y > Hi.Y || First.Z > Hi.Z)
			{
				return;
			}
			const double Distance = ((FVector(First) - Exact) * Grid.VoxelSize).Size();
			if (Distance > MaxRadius)
			{
				return;
			}
			const bool bBetter = !bFound || Distance < Best || (Distance == Best && FPCGVoxelGrid::CellLess(First, OutFirst));
			if (bBetter && IsFree(First))
			{
				bFound = true;
				Best = Distance;
				OutFirst = First;
			}
		};

		for (int32 Shell = 0; Shell <= MaxShell; ++Shell)
		{
			// Every box in this shell is at least (Shell - 0.5) cells away; stop once none can beat the best.
			if (Shell > 0 && (Shell - 0.5) * MinVoxel > Best)
			{
				break;
			}
			for (int32 DZ = -Shell; DZ <= Shell; ++DZ)
			{
				for (int32 DY = -Shell; DY <= Shell; ++DY)
				{
					const bool bOnFace = FMath::Abs(DZ) == Shell || FMath::Abs(DY) == Shell;
					if (bOnFace)
					{
						for (int32 DX = -Shell; DX <= Shell; ++DX)
						{
							Consider(Center + FIntVector(DX, DY, DZ));
						}
					}
					else
					{
						Consider(Center + FIntVector(-Shell, DY, DZ));
						if (Shell > 0)
						{
							Consider(Center + FIntVector(Shell, DY, DZ));
						}
					}
				}
			}
		}
		return bFound;
	}
}

namespace PCGVoxelPackPoints
{
	bool BoxesOverlap(const FVector& CenterA, const FQuat& RotationA, const FVector& HalfA,
		const FVector& CenterB, const FQuat& RotationB, const FVector& HalfB)
	{
		// Separating axis test for two oriented boxes (Ericson, Real-Time Collision Detection 4.4.1).
		const FVector A[3] = { RotationA.GetAxisX(), RotationA.GetAxisY(), RotationA.GetAxisZ() };
		const FVector B[3] = { RotationB.GetAxisX(), RotationB.GetAxisY(), RotationB.GetAxisZ() };
		double R[3][3];
		double AbsR[3][3];
		for (int32 I = 0; I < 3; ++I)
		{
			for (int32 J = 0; J < 3; ++J)
			{
				R[I][J] = A[I] | B[J];
				AbsR[I][J] = FMath::Abs(R[I][J]) + 1e-9;
			}
		}
		const FVector D = CenterB - CenterA;
		const double T[3] = { D | A[0], D | A[1], D | A[2] };
		const double a0 = HalfA.X, a1 = HalfA.Y, a2 = HalfA.Z;
		const double b0 = HalfB.X, b1 = HalfB.Y, b2 = HalfB.Z;

		for (int32 I = 0; I < 3; ++I)
		{
			if (FMath::Abs(T[I]) >= HalfA[I] + (b0 * AbsR[I][0] + b1 * AbsR[I][1] + b2 * AbsR[I][2])) return false;
		}
		for (int32 J = 0; J < 3; ++J)
		{
			if (FMath::Abs(T[0] * R[0][J] + T[1] * R[1][J] + T[2] * R[2][J]) >= (a0 * AbsR[0][J] + a1 * AbsR[1][J] + a2 * AbsR[2][J]) + HalfB[J]) return false;
		}
		if (FMath::Abs(T[2] * R[1][0] - T[1] * R[2][0]) >= a1 * AbsR[2][0] + a2 * AbsR[1][0] + b1 * AbsR[0][2] + b2 * AbsR[0][1]) return false;
		if (FMath::Abs(T[2] * R[1][1] - T[1] * R[2][1]) >= a1 * AbsR[2][1] + a2 * AbsR[1][1] + b0 * AbsR[0][2] + b2 * AbsR[0][0]) return false;
		if (FMath::Abs(T[2] * R[1][2] - T[1] * R[2][2]) >= a1 * AbsR[2][2] + a2 * AbsR[1][2] + b0 * AbsR[0][1] + b1 * AbsR[0][0]) return false;
		if (FMath::Abs(T[0] * R[2][0] - T[2] * R[0][0]) >= a0 * AbsR[2][0] + a2 * AbsR[0][0] + b1 * AbsR[1][2] + b2 * AbsR[1][1]) return false;
		if (FMath::Abs(T[0] * R[2][1] - T[2] * R[0][1]) >= a0 * AbsR[2][1] + a2 * AbsR[0][1] + b0 * AbsR[1][2] + b2 * AbsR[1][0]) return false;
		if (FMath::Abs(T[0] * R[2][2] - T[2] * R[0][2]) >= a0 * AbsR[2][2] + a2 * AbsR[0][2] + b0 * AbsR[1][1] + b1 * AbsR[1][0]) return false;
		if (FMath::Abs(T[1] * R[0][0] - T[0] * R[1][0]) >= a0 * AbsR[1][0] + a1 * AbsR[0][0] + b1 * AbsR[2][2] + b2 * AbsR[2][1]) return false;
		if (FMath::Abs(T[1] * R[0][1] - T[0] * R[1][1]) >= a0 * AbsR[1][1] + a1 * AbsR[0][1] + b0 * AbsR[2][2] + b2 * AbsR[2][0]) return false;
		if (FMath::Abs(T[1] * R[0][2] - T[0] * R[1][2]) >= a0 * AbsR[1][2] + a1 * AbsR[0][2] + b0 * AbsR[2][1] + b1 * AbsR[2][0]) return false;
		return true;
	}

	FQuat ClosestBoxRotation(const FQuat& Rotation, const FVector& Half)
	{
		const double Tolerance = 1e-6 * FMath::Max(1.0, Half.GetMax());
		FQuat Best = FQuat::Identity;
		double BestScore = -1.0;
		for (const PCGVoxelPackPointsPrivate::FBoxRotation& Candidate : PCGVoxelPackPointsPrivate::AllBoxRotations())
		{
			bool bKeepsShape = true;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				bKeepsShape &= FMath::Abs(Half[Axis] - Half[Candidate.Perm[Axis]]) <= Tolerance;
			}
			if (!bKeepsShape)
			{
				continue;
			}
			const double Score = FMath::Abs(Rotation | Candidate.Quat);
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = Candidate.Quat;
			}
		}
		return Best;
	}
}

UPCGVoxelPackPointsSettings::UPCGVoxelPackPointsSettings()
{
	PositionSettle.Attribute.SetAttributeName(TEXT("Settle"));
	RotationSettle.Attribute.SetAttributeName(TEXT("Settle"));
}

#if WITH_EDITOR
FText UPCGVoxelPackPointsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | Pack Points");
}

FText UPCGVoxelPackPointsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Packs loose points into free cells of a voxel grid, as if each were a voxel that fell out of it. In input order, each "
		"point takes the nearest free cell within Max Radius. Position and rotation settle amounts (constant, random or attribute) "
		"leave points part way, and Prevent Overlaps keeps the cell-sized output boxes from intersecting. Out matches In point "
		"for point; Unfilled Cells holds the grid cells nothing claimed.");
}
#endif

TArray<FPCGPinProperties> UPCGVoxelPackPointsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("InTooltip", "Loose points to pack, any number of data sets and points. Handled in order; sort upstream to change priority."))
		.SetRequiredPin();
	Pins.Emplace_GetRef(PCGVoxelPackPoints::GridLabel, EPCGDataType::Point, false, false,
		LOCTEXT("GridTooltip", "Exactly one voxel set: its cells are the targets to fill."))
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelPackPointsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("OutTooltip", "The In points, moved, with their metadata. Removed points (unpacked or unresolved) are left out."));
	Pins.Emplace(PCGVoxelPackPoints::UnfilledLabel, EPCGDataType::Point, false, false,
		LOCTEXT("UnfilledTooltip", "The Grid cells nothing claimed, as a voxel set."));
	return Pins;
}

FPCGElementPtr UPCGVoxelPackPointsSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelPackPointsElement>();
}

bool FPCGVoxelPackPointsElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace PCGVoxelPackPointsPrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGVoxelPackPointsElement::ExecuteInternal);
	check(Context);

	const UPCGVoxelPackPointsSettings* Settings = Context->GetInputSettings<UPCGVoxelPackPointsSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | Pack Points");

	if (!(Settings->MaxRadius > 0.0))
	{
		PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, INDEX_NONE, FText::Format(
			LOCTEXT("Radius", "Max Radius is {0}; it must be greater than 0."), FText::AsNumber(Settings->MaxRadius))), Context);
		return true;
	}

	const TArray<FPCGTaggedData> GridInputs = Context->InputData.GetInputsByPin(PCGVoxelPackPoints::GridLabel);
	if (GridInputs.Num() != 1)
	{
		PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, INDEX_NONE, FText::Format(
			LOCTEXT("GridCount", "the Grid pin has {0} inputs; it needs exactly one voxel set."), FText::AsNumber(GridInputs.Num()))), Context);
		return true;
	}
	const UPCGBasePointData* GridPoints = Cast<const UPCGBasePointData>(GridInputs[0].Data);
	FPCGVoxelGrid Grid;
	if (!FPCGVoxelGrid::Build(Context, GridPoints, Settings->Grid, FText::Format(LOCTEXT("GridTitle", "{0} (Grid)"), Title), 0, Grid))
	{
		return true;
	}

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	const int32 NodeSeed = Context->GetSeed();
	const FQuat GridRotation = Grid.GridToWorld.GetRotation();

	// ---- 1. Assignment, in input order.
	TArray<TArray<FPiece>> PiecesByInput;
	PiecesByInput.SetNum(Inputs.Num());
	TArray<bool> InputValid;
	InputValid.Init(false, Inputs.Num());
	TSet<FIntVector> Claimed;
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Inputs[Index].Data);
		if (!Points)
		{
			PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, Index, LOCTEXT("NotPoints", "the input is not point data.")), Context);
			continue;
		}
		TArray<double> PositionSettle, RotationSettle;
		if (!ReadSettle(Context, Points, Settings->PositionSettle, NodeSeed, 1, PositionSettle, Title, Index, LOCTEXT("PositionSettle", "Position Settle"))
			|| !ReadSettle(Context, Points, Settings->RotationSettle, NodeSeed, 2, RotationSettle, Title, Index, LOCTEXT("RotationSettle", "Rotation Settle")))
		{
			continue;
		}

		TArray<FVector> Spans;
		if (Settings->InChunks == EPCGVoxelInChunks::WholePiece && PCGVoxelPointIO::HasPointAttribute(Points, Settings->Grid.SpanAttribute))
		{
			FPCGAttributePropertyInputSelector Selector;
			Selector.SetAttributeName(Settings->Grid.SpanAttribute);
			if (!PCGVoxelPointIO::ReadPointValues<FVector>(Context, Points, Selector, Spans, Title, Index,
				LOCTEXT("SpanLabel", "Span Attribute"), LOCTEXT("SpanType", "a Vector of whole numbers")))
			{
				continue;
			}
		}

		const TConstPCGValueRange<FTransform> Transforms = Points->GetConstTransformValueRange();
		TArray<FPiece>& Pieces = PiecesByInput[Index];
		Pieces.SetNum(Points->GetNumPoints());
		bool bFailed = false;
		for (int32 Point = 0; Point < Pieces.Num() && !bFailed; ++Point)
		{
			FPiece& Piece = Pieces[Point];
			Piece.Input = Index;
			Piece.Point = Point;
			Piece.Original = Transforms[Point];
			Piece.PositionSettle = PositionSettle[Point];
			Piece.RotationSettle = RotationSettle[Point];
			if (!Spans.IsEmpty())
			{
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					if (!PCGVoxel::ToSpan(Spans[Point][Axis], Piece.Span[Axis]))
					{
						PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, Index, FText::Format(
							LOCTEXT("SpanValue", "point {0}: '{1}' is {2}; a span must be whole numbers of cells, each at least 1."),
							FText::AsNumber(Point), FText::FromName(Settings->Grid.SpanAttribute), FText::FromString(Spans[Point].ToString()))), Context);
						bFailed = true;
						break;
					}
				}
			}
			Piece.Half = Grid.BoxHalfSize(Piece.Span);

			if (FindFreeBox(Grid, Claimed, Piece.Original.GetLocation(), Piece.Span, Settings->MaxRadius, Piece.First))
			{
				Piece.bPacked = true;
				Piece.TargetIndex = Grid.FindPoint(Piece.First);
				for (int32 Z = 0; Z < Piece.Span.Z; ++Z)
				{
					for (int32 Y = 0; Y < Piece.Span.Y; ++Y)
					{
						for (int32 X = 0; X < Piece.Span.X; ++X)
						{
							Claimed.Add(Piece.First + FIntVector(X, Y, Z));
						}
					}
				}
				Piece.TargetLocation = Grid.GridToWorld.TransformPositionNoScale(Grid.BoxCenterGrid(Piece.First, Piece.Span));
				const FQuat InGrid = GridRotation.Inverse() * Piece.Original.GetRotation();
				Piece.TargetRotation = (GridRotation * PCGVoxelPackPoints::ClosestBoxRotation(InGrid, Piece.Half)).GetNormalized();
			}
			else
			{
				Piece.TargetLocation = Piece.Original.GetLocation();
				Piece.TargetRotation = Piece.Original.GetRotation();
				Piece.PositionSettle = 0.0;
				Piece.RotationSettle = 0.0;
				Piece.bRemoved = Settings->UnpackedPoints == EPCGVoxelUnpackedPoints::Remove;
			}
			Piece.UsedSettle = Piece.PositionSettle;
			Piece.Pose(Piece.PositionSettle, Piece.RotationSettle, Piece.Location, Piece.Rotation);
		}
		if (bFailed)
		{
			// Release what this input claimed, so a failed input leaves the grid as it found it.
			for (const FPiece& Piece : Pieces)
			{
				if (Piece.bPacked)
				{
					for (int32 Z = 0; Z < Piece.Span.Z; ++Z)
						for (int32 Y = 0; Y < Piece.Span.Y; ++Y)
							for (int32 X = 0; X < Piece.Span.X; ++X)
								Claimed.Remove(Piece.First + FIntVector(X, Y, Z));
				}
			}
			Pieces.Reset();
			continue;
		}
		InputValid[Index] = true;
	}

	// ---- 2. Overlaps: fully settled boxes first, then boxes that cannot move, then part-way boxes, most settled first.
	if (Settings->bPreventOverlaps)
	{
		TArray<FPiece*> Settled, Fixed, Partial;
		double BucketSize = 1.0;
		for (TArray<FPiece>& Pieces : PiecesByInput)
		{
			for (FPiece& Piece : Pieces)
			{
				if (Piece.bRemoved)
				{
					continue;
				}
				BucketSize = FMath::Max(BucketSize, 2.0 * Piece.Half.Size());
				if (!Piece.bPacked)
				{
					Fixed.Add(&Piece);
				}
				else if (Piece.PositionSettle >= 1.0 && Piece.RotationSettle >= 1.0)
				{
					Settled.Add(&Piece);
				}
				else
				{
					Partial.Add(&Piece);
				}
			}
		}
		Partial.StableSort([](const FPiece& A, const FPiece& B) { return A.PositionSettle > B.PositionSettle; });

		FPlacedBoxes Placed;
		Placed.BucketSize = BucketSize;
		Placed.Shrink = 1e-3 * Grid.VoxelSize.GetMin();
		for (FPiece* Piece : Settled)
		{
			Placed.Add(*Piece);
		}

		auto Unresolved = [&](FPiece& Piece, const FVector& Location, const FQuat& Rotation, double Settle)
		{
			switch (Settings->UnresolvedOverlap)
			{
			case EPCGVoxelUnresolvedOverlap::Keep:
				Piece.Location = Location;
				Piece.Rotation = Rotation;
				Piece.UsedSettle = Settle;
				Piece.bOverlapping = true;
				Placed.Add(Piece);
				break;
			case EPCGVoxelUnresolvedOverlap::Return:
				if (Piece.bPacked && !Placed.Collides(Piece.Original.GetLocation(), Piece.Original.GetRotation(), Piece.Half))
				{
					Piece.Location = Piece.Original.GetLocation();
					Piece.Rotation = Piece.Original.GetRotation();
					Piece.UsedSettle = 0.0;
					Placed.Add(Piece);
				}
				else
				{
					Piece.bRemoved = true;
				}
				break;
			default:
				Piece.bRemoved = true;
				break;
			}
		};

		for (FPiece* Piece : Fixed)
		{
			if (Placed.Collides(Piece->Location, Piece->Rotation, Piece->Half))
			{
				Unresolved(*Piece, Piece->Location, Piece->Rotation, 0.0);
			}
			else
			{
				Placed.Add(*Piece);
			}
		}

		constexpr int32 Steps = 8;
		for (FPiece* Piece : Partial)
		{
			bool bPlaced = false;
			for (int32 Step = 0; Step <= Steps && !bPlaced; ++Step)
			{
				const double Alpha = static_cast<double>(Step) / Steps;
				const double Position = FMath::Lerp(Piece->PositionSettle, 1.0, Alpha);
				const double Turn = FMath::Lerp(Piece->RotationSettle, 1.0, Alpha);
				FVector Location;
				FQuat Rotation;
				Piece->Pose(Position, Turn, Location, Rotation);
				if (!Placed.Collides(Location, Rotation, Piece->Half))
				{
					Piece->Location = Location;
					Piece->Rotation = Rotation;
					Piece->UsedSettle = Position;
					Placed.Add(*Piece);
					bPlaced = true;
				}
			}
			if (!bPlaced)
			{
				Unresolved(*Piece, Piece->Location, Piece->Rotation, Piece->PositionSettle);
			}
		}
	}

	// ---- 3. Out: the In points, moved.
	const bool bWriteOverlapping = Settings->bPreventOverlaps && Settings->UnresolvedOverlap == EPCGVoxelUnresolvedOverlap::Keep;
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		if (!InputValid[Index])
		{
			continue;
		}
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Inputs[Index].Data);
		const TArray<FPiece>& Pieces = PiecesByInput[Index];
		TArray<int32> Read, Write;
		for (const FPiece& Piece : Pieces)
		{
			if (!Piece.bRemoved)
			{
				Write.Add(Read.Num());
				Read.Add(Piece.Point);
			}
		}

		UPCGBasePointData* Out = PCGUtilsAsyncObjects::NewObject<UPCGPointArrayData>(Context);
		Out->InitializeFromDataWithParams(FPCGInitializeFromDataParams(Points));
		Out->SetNumPoints(Read.Num(), /*bInitializeValues=*/false);
		Out->AllocateProperties(Points->GetAllocatedProperties() | EPCGPointNativeProperties::Transform
			| EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax | EPCGPointNativeProperties::MetadataEntry);
		Points->CopyPointsTo(Out, Read, Write);

		TPCGValueRange<FTransform> Transforms = Out->GetTransformValueRange();
		TPCGValueRange<FVector> Mins = Out->GetBoundsMinValueRange();
		TPCGValueRange<FVector> Maxs = Out->GetBoundsMaxValueRange();
		TArray<bool> Packed, Overlapping;
		TArray<int32> Targets;
		TArray<float> Settles;
		for (int32 Out_ = 0; Out_ < Read.Num(); ++Out_)
		{
			const FPiece& Piece = Pieces[Read[Out_]];
			FTransform& Transform = Transforms[Out_];
			Transform.SetLocation(Piece.Location);
			Transform.SetRotation(Piece.Rotation);
			if (Settings->bSetBounds)
			{
				Transform.SetScale3D(FVector::OneVector);
				Mins[Out_] = -Piece.Half;
				Maxs[Out_] = Piece.Half;
			}
			Packed.Add(Piece.bPacked);
			Targets.Add(Piece.TargetIndex);
			Settles.Add(static_cast<float>(Piece.bPacked ? Piece.UsedSettle : 0.0));
			Overlapping.Add(Piece.bOverlapping);
		}

		PCGVoxelPointIO::MakeEntriesUnique(Out);
		PCGVoxelPointIO::WritePointAttribute<bool>(Out, Settings->PackedAttribute, false, Packed);
		PCGVoxelPointIO::WritePointAttribute<int32>(Out, Settings->TargetIndexAttribute, INDEX_NONE, Targets);
		PCGVoxelPointIO::WritePointAttribute<float>(Out, Settings->SettleAttribute, 0.0f, Settles);
		if (bWriteOverlapping)
		{
			PCGVoxelPointIO::WritePointAttribute<bool>(Out, Settings->OverlappingAttribute, false, Overlapping);
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[Index]);
		Output.Data = Out;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}

	// ---- 4. Unfilled Cells.
	TArray<FPCGVoxelCellOut> Unfilled;
	const bool bKeepChunks = Settings->UnfilledChunks == EPCGVoxelChunkOutput::Keep && Grid.bChunked;
	for (int32 Point = 0; Point < Grid.NumPoints(); ++Point)
	{
		if (!Grid.PointUsed[Point])
		{
			continue;
		}
		const FIntVector First = Grid.PointFirst[Point];
		const FIntVector Span = Grid.PointSpan[Point];
		TArray<FIntVector, TInlineAllocator<8>> Free;
		for (int32 Z = 0; Z < Span.Z; ++Z)
		{
			for (int32 Y = 0; Y < Span.Y; ++Y)
			{
				for (int32 X = 0; X < Span.X; ++X)
				{
					const FIntVector Cell = First + FIntVector(X, Y, Z);
					if (!Claimed.Contains(Cell))
					{
						Free.Add(Cell);
					}
				}
			}
		}
		if (bKeepChunks && Free.Num() == Span.X * Span.Y * Span.Z)
		{
			Unfilled.Add({ Point, First, Span });
			continue;
		}
		for (const FIntVector& Cell : Free)
		{
			Unfilled.Add({ Point, Cell, FIntVector(1) });
		}
	}
	FPCGTaggedData& UnfilledOutput = Context->OutputData.TaggedData.Emplace_GetRef(GridInputs[0]);
	UPCGBasePointData* UnfilledData = PCGVoxelPointIO::WriteCells(Context, GridPoints, Grid, Unfilled, Settings->Grid.SpanAttribute, bKeepChunks);
	// Surrounded by the whole grid, packed or not: packing fills slots, it does not change the grid.
	PCGVoxelPointIO::RefreshSurrounded(GridPoints, UnfilledData, Settings->Grid.SurroundedAttribute, Unfilled, [&Grid](const FIntVector& Cell) { return Grid.IsOccupied(Cell); });
	UnfilledOutput.Data = UnfilledData;
	UnfilledOutput.Pin = PCGVoxelPackPoints::UnfilledLabel;
	return true;
}

#undef LOCTEXT_NAMESPACE
