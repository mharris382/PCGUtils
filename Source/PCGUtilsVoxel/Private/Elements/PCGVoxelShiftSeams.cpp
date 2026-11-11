// Copyright Max Harris

#include "Elements/PCGVoxelShiftSeams.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGPointArrayData.h"
#include "Helpers/PCGHelpers.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "Math/RandomStream.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"

#define LOCTEXT_NAMESPACE "PCGVoxelShiftSeams"

namespace PCGVoxelShiftSeamsPrivate
{
	struct FUnionFind
	{
		TArray<int32> Parent;
		explicit FUnionFind(int32 Num)
		{
			Parent.SetNumUninitialized(Num);
			for (int32 Index = 0; Index < Num; ++Index)
			{
				Parent[Index] = Index;
			}
		}
		int32 Find(int32 X)
		{
			while (Parent[X] != X)
			{
				Parent[X] = Parent[Parent[X]];
				X = Parent[X];
			}
			return X;
		}
		void Union(int32 A, int32 B)
		{
			A = Find(A);
			B = Find(B);
			if (A != B)
			{
				Parent[FMath::Max(A, B)] = FMath::Min(A, B);
			}
		}
	};

	FIntVector UnitStep(int32 Axis)
	{
		FIntVector Step(0);
		Step[Axis] = 1;
		return Step;
	}

	/**
	 * One seam: on one grid axis, a set of boxes whose high faces lie on one plane (Low) against the boxes whose low
	 * faces lie on it across from them (High), joined through every unit face they share.
	 */
	struct FSeam
	{
		int32 Axis = 0;
		int32 LowStart = 0;
		int32 LowNum = 0;
		int32 HighStart = 0;
		int32 HighNum = 0;
		FIntVector Key = FIntVector(TNumericLimits<int32>::Max());
		uint32 Order = 0;
	};
}

UPCGVoxelShiftSeamsSettings::UPCGVoxelShiftSeamsSettings()
{
	NoiseScale.Attribute.Update(TEXT("@Data.SeamNoiseScale"));
}

#if WITH_EDITOR
FText UPCGVoxelShiftSeamsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | Shift Seams");
}

FText UPCGVoxelShiftSeamsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Slides the interior seams between boxes so the boxes become uneven while filling exactly the same space: each seam "
		"grows the boxes on one side and shrinks those on the other by the same amount. Seams touching the outside stay put. "
		"Only point bounds change. Use after Voxel | Compress Random for staggered, independent seams.");
}
#endif

TArray<FPCGPinProperties> UPCGVoxelShiftSeamsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("InTooltip", "Voxel sets, plain or chunked. Each box's faces must be along its point's local axes (unrotated or rotated in 90 degree steps against the grid)."))
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelShiftSeamsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("OutTooltip", "Each input with its interior seams shifted: same points, cells and metadata; only bounds differ."));
	return Pins;
}

FPCGElementPtr UPCGVoxelShiftSeamsSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelShiftSeamsElement>();
}

bool FPCGVoxelShiftSeamsElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace PCGVoxelShiftSeamsPrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGVoxelShiftSeamsElement::ExecuteInternal);
	check(Context);

	const UPCGVoxelShiftSeamsSettings* Settings = Context->GetInputSettings<UPCGVoxelShiftSeamsSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | Shift Seams");
	const bool bNoise = Settings->Source == EPCGVoxelSeamShiftSource::Noise;
	if (bNoise && Settings->NoiseScale.IsConstant() && !(Settings->NoiseScale.Constant > 0.0))
	{
		PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, INDEX_NONE, FText::Format(
			LOCTEXT("NoiseScale", "Noise Scale is {0}; it must be greater than 0."), FText::AsNumber(Settings->NoiseScale.Constant))), Context);
		return true;
	}
	const bool bAxisOn[3] = { Settings->bShiftX, Settings->bShiftY, Settings->bShiftZ };
	const int32 Seed = Context->GetSeed();

	// A fixed random offset into the noise field per seed and axis, off the lattice where Perlin noise is always 0.
	FVector NoiseOffset[3];
	{
		FRandomStream Stream(PCGHelpers::ComputeSeed(Seed, 0x5EA45));
		for (FVector& Offset : NoiseOffset)
		{
			Offset = FVector(Stream.FRandRange(-1000.0, 1000.0), Stream.FRandRange(-1000.0, 1000.0), Stream.FRandRange(-1000.0, 1000.0));
		}
	}

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Inputs[Index].Data);
		FPCGVoxelGrid Grid;
		if (!FPCGVoxelGrid::Build(Context, Points, Settings->Grid, Title, Index, Grid))
		{
			continue;
		}

		// Strength per box (or one for the set) and the noise size for the set.
		TPCGUtilsPointValues<double> Strengths;
		FText ReadError;
		if (!Settings->Strength.TryRead(Points, Strengths, ReadError, LOCTEXT("StrengthLabel", "Strength")))
		{
			PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, Index, ReadError), Context);
			continue;
		}
		double NoiseScale = Settings->NoiseScale.Constant;
		if (bNoise)
		{
			if (!Settings->NoiseScale.TryReadDataValue(Points, NoiseScale, ReadError, LOCTEXT("NoiseScaleLabel", "Noise Scale")))
			{
				PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, Index, ReadError), Context);
				continue;
			}
			if (!(NoiseScale > 0.0))
			{
				PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, Index, FText::Format(
					LOCTEXT("NoiseScaleData", "Noise Scale ('{0}') is {1}; it must be greater than 0."),
					Settings->NoiseScale.Attribute.GetDisplayText(), FText::AsNumber(NoiseScale))), Context);
				continue;
			}
		}
		const bool bAnyStrength = Strengths.IsPerPoint()
			? Strengths.PerPoint.ContainsByPredicate([](double S) { return S > 0.0; })
			: Strengths.Single > 0.0;

		// The output is the input, copied point for point.
		const int32 NumPoints = Points->GetNumPoints();
		UPCGBasePointData* Out = PCGUtilsAsyncObjects::NewObject<UPCGPointArrayData>(Context);
		Out->InitializeFromDataWithParams(FPCGInitializeFromDataParams(Points));
		Out->SetNumPoints(NumPoints, /*bInitializeValues=*/false);
		Out->AllocateProperties(Points->GetAllocatedProperties() | EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax);
		{
			TArray<int32> Identity;
			Identity.SetNumUninitialized(NumPoints);
			for (int32 Point = 0; Point < NumPoints; ++Point)
			{
				Identity[Point] = Point;
			}
			Points->CopyPointsTo(Out, Identity, Identity);
		}

		// Every point's box in grid space (Lo, Hi), read from its bounds, and which local bounds axis (and sign) each
		// grid axis is for it.
		const TConstPCGValueRange<FTransform> Transforms = Points->GetConstTransformValueRange();
		TPCGValueRange<FVector> Mins = Out->GetBoundsMinValueRange();
		TPCGValueRange<FVector> Maxs = Out->GetBoundsMaxValueRange();
		const FQuat GridRotation = Grid.GridToWorld.GetRotation();
		TArray<FIntVector> LocalAxis, LocalSign;
		TArray<FVector> LocalScale, Centre, Lo, Hi;
		LocalAxis.SetNumUninitialized(NumPoints);
		LocalSign.SetNumUninitialized(NumPoints);
		LocalScale.SetNumUninitialized(NumPoints);
		Centre.SetNumUninitialized(NumPoints);
		Lo.SetNumUninitialized(NumPoints);
		Hi.SetNumUninitialized(NumPoints);
		for (int32 Point = 0; Point < NumPoints; ++Point)
		{
			const FQuat PointRotation = Transforms[Point].GetRotation();
			const FVector Scale = Transforms[Point].GetScale3D().GetAbs();
			Centre[Point] = Grid.GridToWorld.InverseTransformPositionNoScale(Transforms[Point].GetLocation());
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const FVector Local = PointRotation.UnrotateVector(GridRotation.RotateVector(FVector(UnitStep(Axis))));
				const int32 L = FMath::Abs(Local.X) >= FMath::Abs(Local.Y) && FMath::Abs(Local.X) >= FMath::Abs(Local.Z) ? 0
					: (FMath::Abs(Local.Y) >= FMath::Abs(Local.Z) ? 1 : 2);
				LocalAxis[Point][Axis] = L;
				LocalSign[Point][Axis] = Local[L] >= 0.0 ? 1 : -1;
				LocalScale[Point][Axis] = FMath::Max(Scale[L], UE_SMALL_NUMBER);
				const double LocalLo = Mins[Point][L] * Scale[L];
				const double LocalHi = Maxs[Point][L] * Scale[L];
				Lo[Point][Axis] = Centre[Point][Axis] + (LocalSign[Point][Axis] > 0 ? LocalLo : -LocalHi);
				Hi[Point][Axis] = Centre[Point][Axis] + (LocalSign[Point][Axis] > 0 ? LocalHi : -LocalLo);
			}
		}

		// Gather the seams of every enabled axis. A seam with any unit face on the outside never moves.
		TArray<FSeam> Seams;
		TArray<int32> LowMembers, HighMembers;
		const int32 NumCells = Grid.NumCells();
		for (int32 Axis = 0; Axis < 3 && bAnyStrength; ++Axis)
		{
			if (!bAxisOn[Axis])
			{
				continue;
			}
			const FIntVector Step = UnitStep(Axis);

			// Node 2P is point P's low face on this axis, 2P + 1 its high face.
			FUnionFind Faces(2 * NumPoints);
			TArray<bool> Outside;
			Outside.Init(false, 2 * NumPoints);
			TArray<FIntVector> FaceKey;
			FaceKey.Init(FIntVector(TNumericLimits<int32>::Max()), 2 * NumPoints);
			for (int32 Cell = 0; Cell < NumCells; ++Cell)
			{
				const int32 Point = Grid.CellPoint[Cell];
				const int32 Up = Grid.FindCellIndex(Grid.Cells[Cell] + Step);
				if (Up == INDEX_NONE)
				{
					Outside[2 * Point + 1] = true;
				}
				else if (Grid.CellPoint[Up] != Point)
				{
					Faces.Union(2 * Point + 1, 2 * Grid.CellPoint[Up]);
					if (FPCGVoxelGrid::CellLess(Grid.Cells[Up], FaceKey[2 * Point + 1]))
					{
						FaceKey[2 * Point + 1] = Grid.Cells[Up];
					}
				}
				if (Grid.FindCellIndex(Grid.Cells[Cell] - Step) == INDEX_NONE)
				{
					Outside[2 * Point] = true;
				}
			}

			// Per seam root: any face outside, how many Low and High boxes, and the lowest face cell (for the seed).
			TArray<bool> RootOutside;
			RootOutside.Init(false, 2 * NumPoints);
			TArray<int32> LowCount, HighCount, SeamOfRoot;
			LowCount.Init(0, 2 * NumPoints);
			HighCount.Init(0, 2 * NumPoints);
			SeamOfRoot.Init(INDEX_NONE, 2 * NumPoints);
			TArray<FIntVector> RootKey;
			RootKey.Init(FIntVector(TNumericLimits<int32>::Max()), 2 * NumPoints);
			for (int32 Node = 0; Node < 2 * NumPoints; ++Node)
			{
				if (!Grid.PointUsed[Node / 2])
				{
					continue;
				}
				const int32 Root = Faces.Find(Node);
				RootOutside[Root] |= Outside[Node];
				++((Node & 1) ? LowCount : HighCount)[Root];
				if (FPCGVoxelGrid::CellLess(FaceKey[Node], RootKey[Root]))
				{
					RootKey[Root] = FaceKey[Node];
				}
			}
			// Keep only seams with boxes on both sides and no face on the outside; members go in flat arrays.
			for (int32 Node = 0; Node < 2 * NumPoints; ++Node)
			{
				if (!Grid.PointUsed[Node / 2])
				{
					continue;
				}
				const int32 Root = Faces.Find(Node);
				if (RootOutside[Root] || LowCount[Root] == 0 || HighCount[Root] == 0)
				{
					continue;
				}
				if (SeamOfRoot[Root] == INDEX_NONE)
				{
					SeamOfRoot[Root] = Seams.Num();
					FSeam& Seam = Seams.AddDefaulted_GetRef();
					Seam.Axis = Axis;
					Seam.Key = RootKey[Root];
					Seam.LowStart = LowMembers.AddUninitialized(LowCount[Root]);
					Seam.HighStart = HighMembers.AddUninitialized(HighCount[Root]);
				}
				FSeam& Seam = Seams[SeamOfRoot[Root]];
				if (Node & 1)
				{
					LowMembers[Seam.LowStart + Seam.LowNum++] = Node / 2;
				}
				else
				{
					HighMembers[Seam.HighStart + Seam.HighNum++] = Node / 2;
				}
			}
		}

		// Seams move one at a time, in a seeded random order. A seam moves only when its two sides cover exactly the
		// same area of its plane at that moment: then the slab it sweeps belongs wholly to the boxes it shrinks, and no
		// other box is touched. An earlier move of a crossing seam can make that untrue; then the seam stays put.
		for (FSeam& Seam : Seams)
		{
			Seam.Order = static_cast<uint32>(PCGHelpers::ComputeSeed(PCGHelpers::ComputeSeed(Seed, Seam.Axis),
				PCGHelpers::ComputeSeed(Seam.Key.X, Seam.Key.Y, Seam.Key.Z)));
		}
		Seams.Sort([](const FSeam& A, const FSeam& B)
		{
			return A.Order != B.Order ? A.Order < B.Order : (A.Axis != B.Axis ? A.Axis < B.Axis : FPCGVoxelGrid::CellLess(A.Key, B.Key));
		});

		TArray<bool> Moved;
		Moved.Init(false, NumPoints);
		const double AreaTolerance = 1e-6 * Grid.VoxelSize.GetMax() * Grid.VoxelSize.GetMax();
		for (const FSeam& Seam : Seams)
		{
			const int32 Axis = Seam.Axis;
			const TConstArrayView<int32> Low(LowMembers.GetData() + Seam.LowStart, Seam.LowNum);
			const TConstArrayView<int32> High(HighMembers.GetData() + Seam.HighStart, Seam.HighNum);
			const int32 U = (Axis + 1) % 3;
			const int32 V = (Axis + 2) % 3;
			auto Area = [&](int32 P) { return (Hi[P][U] - Lo[P][U]) * (Hi[P][V] - Lo[P][V]); };
			auto Shared = [&](int32 A, int32 B)
			{
				const double DU = FMath::Min(Hi[A][U], Hi[B][U]) - FMath::Max(Lo[A][U], Lo[B][U]);
				const double DV = FMath::Min(Hi[A][V], Hi[B][V]) - FMath::Max(Lo[A][V], Lo[B][V]);
				return DU > 0.0 && DV > 0.0 ? DU * DV : 0.0;
			};

			// Boxes on each side are disjoint, so the two sides cover the same area exactly when
			// sum(Low areas) = sum(High areas) = sum of all Low-High overlaps.
			const double Plane = Hi[Low[0]][Axis];
			const double PlaneTolerance = 1e-6 * Grid.VoxelSize[Axis];
			bool bOnPlane = true;
			double LowArea = 0.0, HighArea = 0.0, SharedArea = 0.0;
			double LowGive = TNumericLimits<double>::Max(), HighGive = TNumericLimits<double>::Max();
			for (const int32 P : Low)
			{
				bOnPlane &= FMath::IsNearlyEqual(Hi[P][Axis], Plane, PlaneTolerance);
				LowArea += Area(P);
				LowGive = FMath::Min(LowGive, (Hi[P][Axis] - Lo[P][Axis]) * 0.5);
				for (const int32 Q : High)
				{
					SharedArea += Shared(P, Q);
				}
			}
			for (const int32 Q : High)
			{
				bOnPlane &= FMath::IsNearlyEqual(Lo[Q][Axis], Plane, PlaneTolerance);
				HighArea += Area(Q);
				HighGive = FMath::Min(HighGive, (Hi[Q][Axis] - Lo[Q][Axis]) * 0.5);
			}
			if (!bOnPlane || !FMath::IsNearlyEqual(LowArea, HighArea, AreaTolerance) || !FMath::IsNearlyEqual(LowArea, SharedArea, AreaTolerance))
			{
				continue;
			}

			double Value;
			if (bNoise)
			{
				FVector Sum = FVector::ZeroVector;
				for (const int32 P : Low)
				{
					FVector FaceCentre = (Lo[P] + Hi[P]) * 0.5;
					FaceCentre[Axis] = Plane;
					Sum += FaceCentre * Area(P);
				}
				const FVector World = Grid.GridToWorld.TransformPositionNoScale(Sum / FMath::Max(LowArea, UE_SMALL_NUMBER));
				Value = FMath::Clamp(FMath::PerlinNoise3D(World / NoiseScale + NoiseOffset[Axis]), -1.0f, 1.0f);
			}
			else
			{
				FRandomStream Stream(static_cast<int32>(Seam.Order));
				Value = Stream.FRandRange(-1.0, 1.0);
			}

			// + moves the seam towards the High boxes. LowGive and HighGive are half of each side's thinnest box.
			double Reach;
			if (Settings->Limit == EPCGVoxelSeamLimit::ThinnestEitherSide)
			{
				Reach = 2.0 * FMath::Min(LowGive, HighGive);
			}
			else
			{
				Reach = Value >= 0.0 ? HighGive : LowGive;
			}
			// The seam's strength: the weakest box on either side.
			double Strength = 1.0;
			for (const int32 P : Low)
			{
				Strength = FMath::Min(Strength, Strengths.Get(P));
			}
			for (const int32 Q : High)
			{
				Strength = FMath::Min(Strength, Strengths.Get(Q));
			}
			const double Shift = Value * Strength * Reach;
			if (Shift == 0.0)
			{
				continue;
			}
			for (const int32 P : Low)
			{
				Hi[P][Axis] += Shift;
				Moved[P] = true;
			}
			for (const int32 Q : High)
			{
				Lo[Q][Axis] += Shift;
				Moved[Q] = true;
			}
		}

		// Moved boxes back into local bounds; points stay where they are.
		for (int32 Point = 0; Point < NumPoints; ++Point)
		{
			if (!Moved[Point])
			{
				continue;
			}
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const int32 L = LocalAxis[Point][Axis];
				const double Scale = LocalScale[Point][Axis];
				const double C = Centre[Point][Axis];
				if (LocalSign[Point][Axis] > 0)
				{
					Mins[Point][L] = (Lo[Point][Axis] - C) / Scale;
					Maxs[Point][L] = (Hi[Point][Axis] - C) / Scale;
				}
				else
				{
					Mins[Point][L] = (C - Hi[Point][Axis]) / Scale;
					Maxs[Point][L] = (C - Lo[Point][Axis]) / Scale;
				}
			}
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[Index]);
		Output.Data = Out;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
