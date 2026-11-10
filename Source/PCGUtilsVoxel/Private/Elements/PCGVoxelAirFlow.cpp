// Copyright Max Harris

#include "Elements/PCGVoxelAirFlow.h"

#include "Async/ParallelFor.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGPointArrayData.h"
#include "Helpers/PCGHelpers.h"
#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "PCGContext.h"
#include "PCGModule.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"
#include "Voxel/PCGVoxelPointIO.h"

#define LOCTEXT_NAMESPACE "PCGVoxelAirFlow"

namespace PCGVoxelAirFlowPrivate
{
	enum EState : uint8 { Moving = 0, Solid = 1, Sealed = 2 };

	int32 FloorDiv(int32 A, int32 B)
	{
		const int32 Q = A / B;
		return (A % B != 0 && (A < 0) != (B < 0)) ? Q - 1 : Q;
	}

	FIntVector FloorDiv(const FIntVector& A, int32 B)
	{
		return FIntVector(FloorDiv(A.X, B), FloorDiv(A.Y, B), FloorDiv(A.Z, B));
	}

	const FIntVector Unit[3] = { FIntVector(1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, 0, 1) };

	/** Sum of Fn(Index) over [0, Num), in parallel, added up in a fixed order so the result does not depend on threads. */
	template <typename TFn>
	double ParallelSum(int32 Num, TFn&& Fn)
	{
		constexpr int32 Chunk = 8192;
		const int32 NumChunks = (Num + Chunk - 1) / Chunk;
		TArray<double> Parts;
		Parts.SetNumZeroed(NumChunks);
		ParallelFor(NumChunks, [&](int32 C)
		{
			double Sum = 0.0;
			const int32 End = FMath::Min(Num, (C + 1) * Chunk);
			for (int32 Index = C * Chunk; Index < End; ++Index)
			{
				Sum += Fn(Index);
			}
			Parts[C] = Sum;
		});
		double Total = 0.0;
		for (const double Part : Parts)
		{
			Total += Part;
		}
		return Total;
	}

	/** The air around one voxel set: a box of air cells, in air-cell index space. */
	struct FAir
	{
		FIntVector Origin = FIntVector::ZeroValue; // absolute air-cell index of local (0, 0, 0)
		FIntVector Dims = FIntVector::ZeroValue;
		bool bGround = false;

		int32 Num() const { return Dims.X * Dims.Y * Dims.Z; }
		bool Inside(const FIntVector& L) const
		{
			return static_cast<uint32>(L.X) < static_cast<uint32>(Dims.X) && static_cast<uint32>(L.Y) < static_cast<uint32>(Dims.Y)
				&& static_cast<uint32>(L.Z) < static_cast<uint32>(Dims.Z);
		}
		int32 Index(const FIntVector& L) const { return L.X + Dims.X * (L.Y + Dims.Y * L.Z); }
		FIntVector Local(int32 Index) const
		{
			return FIntVector(Index % Dims.X, (Index / Dims.X) % Dims.Y, Index / (Dims.X * Dims.Y));
		}
		FIntVector FaceDims(int32 Axis) const { return Dims + Unit[Axis]; }
		int32 FaceIndex(int32 Axis, const FIntVector& F) const
		{
			const FIntVector D = FaceDims(Axis);
			return F.X + D.X * (F.Y + D.Y * F.Z);
		}
		/** Whether the face on the -Axis or +Axis side of a cell, leaving the box, is the floor wall. */
		bool IsFloor(int32 Axis, int32 Sign) const { return bGround && Axis == 2 && Sign < 0; }
	};

	/** One flow source, in world space. Force is already in world axes (Point space applied) and scaled by Source Strength. */
	struct FSource
	{
		FVector Position = FVector::ZeroVector;
		FVector Force = FVector::ZeroVector;
		FVector Axis = FVector::UpVector;
		double Outflow = 0.0;
		double Swirl = 0.0;
		double Radius = 1.0;
	};

	double Falloff(EPCGVoxelFlowFalloff Mode, double T)
	{
		switch (Mode)
		{
		case EPCGVoxelFlowFalloff::Linear: return 1.0 - T;
		case EPCGVoxelFlowFalloff::Constant: return 1.0;
		default: { const double U = 1.0 - T * T; return U * U; }
		}
	}

	/** Curl of a three-channel Perlin field at Q (noise space): swirls with no sources or sinks. */
	FVector CurlNoise(const FVector& Q, const FVector Offsets[3])
	{
		constexpr double Eps = 0.05;
		auto Psi = [&](const FVector& P)
		{
			return FVector(FMath::PerlinNoise3D(P + Offsets[0]), FMath::PerlinNoise3D(P + Offsets[1]), FMath::PerlinNoise3D(P + Offsets[2]));
		};
		const FVector Dx = (Psi(Q + FVector(Eps, 0, 0)) - Psi(Q - FVector(Eps, 0, 0))) / (2.0 * Eps);
		const FVector Dy = (Psi(Q + FVector(0, Eps, 0)) - Psi(Q - FVector(0, Eps, 0))) / (2.0 * Eps);
		const FVector Dz = (Psi(Q + FVector(0, 0, Eps)) - Psi(Q - FVector(0, 0, Eps))) / (2.0 * Eps);
		return FVector(Dy.Z - Dz.Y, Dz.X - Dx.Z, Dx.Y - Dy.X);
	}
}

UPCGVoxelAirFlowSettings::UPCGVoxelAirFlowSettings()
	: Wind(FVector(500.0, 0.0, 0.0))
{
	// Update parses the @Data. prefix into the domain; the constructor's default name would keep it in the name.
	Wind.Attribute.Update(TEXT("@Data.Wind"));
}

#if WITH_EDITOR
FText UPCGVoxelAirFlowSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | Air Flow");
}

FText UPCGVoxelAirFlowSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Wind blowing past each voxel set: a smooth flow that goes around solid cells, speeds up over tops and corners and is "
		"weaker in wind shadows, with optional turbulence. Points on Sources add fans, vents, suction and vortices. Flow: one point per air cell (a voxel set of its own) with the flow "
		"vector, speed, shelter and distance to the solids. Cells: the input with how hard the wind hits each cell and the flow "
		"along its surface.");
}
#endif

TArray<FPCGPinProperties> UPCGVoxelAirFlowSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("InTooltip", "Voxel sets: the solid cells the wind blows past. Each set gets its own air."))
		.SetRequiredPin();
	Pins.Emplace(PCGVoxelAirFlowPins::Sources, EPCGDataType::Point, true, true,
		LOCTEXT("SourcesTooltip", "Optional flow sources: any points (not a voxel set) carrying Force, Outflow and/or Swirl attributes, "
			"each reaching out to its Radius. Every source acts on every voxel set it overlaps, on top of the wind."));
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelAirFlowSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGVoxelAirFlowPins::Flow, EPCGDataType::Point, true, true,
		LOCTEXT("FlowTooltip", "One set of air cells per input: a voxel set (Voxel Size and grid transform in @Data) with the flow written per cell. Carries the input's tags."));
	Pins.Emplace(PCGVoxelAirFlowPins::Cells, EPCGDataType::Point, true, true,
		LOCTEXT("CellsTooltip", "Each input, unchanged but for Wind Exposure and Surface Flow. Only made when this pin is connected."));
	return Pins;
}

FPCGElementPtr UPCGVoxelAirFlowSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelAirFlowElement>();
}

bool FPCGVoxelAirFlowElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace PCGVoxelAirFlowPrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGVoxelAirFlowElement::ExecuteInternal);
	check(Context);

	const UPCGVoxelAirFlowSettings* Settings = Context->GetInputSettings<UPCGVoxelAirFlowSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | Air Flow");
	auto Fail = [&](const FText& Message, int32 InputIndex = INDEX_NONE)
	{
		PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, InputIndex, Message), Context);
	};

	// ---- Settings, each checked on its own ----
	auto Range = [&](const TCHAR* Name, double Value, const FText& Expected) -> bool
	{
		Fail(FText::Format(LOCTEXT("BadSetting", "{0} is {1}; it must be {2}."), FText::FromString(Name), FText::AsNumber(Value), Expected));
		return false;
	};
	if (Settings->AirCellVoxels < 1 || Settings->AirCellVoxels > 16)
	{
		Range(TEXT("Air Cell Voxels"), Settings->AirCellVoxels, LOCTEXT("R1to16", "from 1 to 16"));
		return true;
	}
	if (!(Settings->SolidFraction >= 0.0 && Settings->SolidFraction < 1.0))
	{
		Range(TEXT("Solid Fraction"), Settings->SolidFraction, LOCTEXT("R0to1Open", "at least 0 and below 1"));
		return true;
	}
	if (Settings->Padding < 0)
	{
		Range(TEXT("Padding"), Settings->Padding, LOCTEXT("R0", "0 or greater"));
		return true;
	}
	if (!(Settings->WakeLength >= 0.0))
	{
		Range(TEXT("Wake Length"), Settings->WakeLength, LOCTEXT("R0", "0 or greater"));
		return true;
	}
	if (!(Settings->WakeStrength >= 0.0 && Settings->WakeStrength <= 1.0))
	{
		Range(TEXT("Wake Strength"), Settings->WakeStrength, LOCTEXT("R0to1", "from 0 to 1"));
		return true;
	}
	if (!(Settings->TurbulenceStrength >= 0.0))
	{
		Range(TEXT("Turbulence Strength"), Settings->TurbulenceStrength, LOCTEXT("R0", "0 or greater"));
		return true;
	}
	if (Settings->TurbulenceStrength > 0.0 && !(Settings->TurbulenceSize > 0.0))
	{
		Range(TEXT("Turbulence Size"), Settings->TurbulenceSize, LOCTEXT("RPosTurb", "greater than 0 while Turbulence Strength is above 0"));
		return true;
	}
	if (Settings->SmoothPasses < 0)
	{
		Range(TEXT("Smooth Passes"), Settings->SmoothPasses, LOCTEXT("R0", "0 or greater"));
		return true;
	}
	if (Settings->MaxIterations < 0)
	{
		Range(TEXT("Max Iterations"), Settings->MaxIterations, LOCTEXT("R0", "0 or greater"));
		return true;
	}
	if (!(Settings->Tolerance >= 0.0))
	{
		Range(TEXT("Tolerance"), Settings->Tolerance, LOCTEXT("R0", "0 or greater"));
		return true;
	}
	if (Settings->MaxDistance < 0)
	{
		Range(TEXT("Max Distance"), Settings->MaxDistance, LOCTEXT("R0", "0 or greater"));
		return true;
	}
	if (Settings->MaxAirCells < 1)
	{
		Range(TEXT("Max Air Cells"), Settings->MaxAirCells, LOCTEXT("R1", "1 or greater"));
		return true;
	}

	// Every enabled output needs a name of its own (per output pin).
	struct FOutputName { bool bOn; FName Name; const TCHAR* Setting; };
	auto CheckNames = [&](TConstArrayView<FOutputName> Names) -> bool
	{
		for (int32 A = 0; A < Names.Num(); ++A)
		{
			if (!Names[A].bOn)
			{
				continue;
			}
			if (Names[A].Name.IsNone())
			{
				Fail(FText::Format(LOCTEXT("EmptyName", "{0} is empty while its output is on; give it a name or turn the output off."),
					FText::FromString(Names[A].Setting)));
				return false;
			}
			for (int32 B = A + 1; B < Names.Num(); ++B)
			{
				if (Names[B].bOn && Names[B].Name == Names[A].Name)
				{
					Fail(FText::Format(LOCTEXT("SameName", "{0} and {1} are both '{2}'; each enabled output needs its own attribute name."),
						FText::FromString(Names[A].Setting), FText::FromString(Names[B].Setting), FText::FromName(Names[A].Name)));
					return false;
				}
			}
		}
		return true;
	};
	const FOutputName FlowNames[] = {
		{ true, Settings->FlowAttribute, TEXT("Flow Attribute") },
		{ Settings->bWriteSpeed, Settings->SpeedAttribute, TEXT("Speed Attribute") },
		{ Settings->bWriteShelter, Settings->ShelterAttribute, TEXT("Shelter Attribute") },
		{ Settings->bWriteSolidDistance, Settings->SolidDistanceAttribute, TEXT("Solid Distance Attribute") },
	};
	const FOutputName CellNames[] = {
		{ Settings->bWriteExposure, Settings->ExposureAttribute, TEXT("Exposure Attribute") },
		{ Settings->bWriteSurfaceFlow, Settings->SurfaceFlowAttribute, TEXT("Surface Flow Attribute") },
	};
	if (!CheckNames(FlowNames) || !CheckNames(CellNames))
	{
		return true;
	}

	// ---- Sources, read once in world space ----
	TArray<FSource> Sources;
	const TArray<FPCGTaggedData> SourceInputs = Context->InputData.GetInputsByPin(PCGVoxelAirFlowPins::Sources);
	for (int32 SourceIndex = 0; SourceIndex < SourceInputs.Num(); ++SourceIndex)
	{
		const UPCGBasePointData* SourcePoints = Cast<const UPCGBasePointData>(SourceInputs[SourceIndex].Data);
		if (!SourcePoints || SourcePoints->GetNumPoints() == 0)
		{
			continue;
		}
		const FText Where = FText::Format(LOCTEXT("SourceWhere", "Sources input {0}"), FText::AsNumber(SourceIndex));
		auto FailSource = [&](const FText& Message)
		{
			Fail(FText::Format(LOCTEXT("SourceFail", "{0}: {1}"), Where, Message));
		};
		const bool bForce = PCGVoxelPointIO::HasPointAttribute(SourcePoints, Settings->ForceAttribute);
		const bool bOutflow = PCGVoxelPointIO::HasPointAttribute(SourcePoints, Settings->OutflowAttribute);
		const bool bSwirl = PCGVoxelPointIO::HasPointAttribute(SourcePoints, Settings->SwirlAttribute);
		if (!bForce && !bOutflow && !bSwirl)
		{
			FailSource(FText::Format(LOCTEXT("NoSourceAttribute",
				"none of Force Attribute '{0}', Outflow Attribute '{1}' and Swirl Attribute '{2}' exists on its points; a source needs at least one."),
				FText::FromName(Settings->ForceAttribute), FText::FromName(Settings->OutflowAttribute), FText::FromName(Settings->SwirlAttribute)));
			return true;
		}
		auto Selector = [](FName Name)
		{
			FPCGAttributePropertyInputSelector Out;
			Out.SetAttributeName(Name);
			return Out;
		};
		const int32 NumSourcePoints = SourcePoints->GetNumPoints();
		TArray<FVector> Forces;
		TArray<double> Outflows, Swirls, Radii;
		if ((bForce && !PCGVoxelPointIO::ReadPointValues<FVector>(Context, SourcePoints, Selector(Settings->ForceAttribute), Forces, Title, INDEX_NONE,
				LOCTEXT("ForceLabel", "Force Attribute"), LOCTEXT("ForceType", "a Vector")))
			|| (bOutflow && !PCGVoxelPointIO::ReadPointValues<double>(Context, SourcePoints, Selector(Settings->OutflowAttribute), Outflows, Title, INDEX_NONE,
				LOCTEXT("OutflowLabel", "Outflow Attribute"), LOCTEXT("OutflowType", "a number")))
			|| (bSwirl && !PCGVoxelPointIO::ReadPointValues<double>(Context, SourcePoints, Selector(Settings->SwirlAttribute), Swirls, Title, INDEX_NONE,
				LOCTEXT("SwirlLabel", "Swirl Attribute"), LOCTEXT("SwirlType", "a number")))
			|| (!Settings->bRadiusFromBounds && !PCGVoxelPointIO::ReadPointValues<double>(Context, SourcePoints, Selector(Settings->RadiusAttribute), Radii, Title, INDEX_NONE,
				LOCTEXT("RadiusLabel", "Radius Attribute"), LOCTEXT("RadiusType", "a number"))))
		{
			return true;
		}
		const TConstPCGValueRange<FTransform> SourceTransforms = SourcePoints->GetConstTransformValueRange();
		const TConstPCGValueRange<FVector> BoundsMin = SourcePoints->GetConstBoundsMinValueRange();
		const TConstPCGValueRange<FVector> BoundsMax = SourcePoints->GetConstBoundsMaxValueRange();
		for (int32 Point = 0; Point < NumSourcePoints; ++Point)
		{
			const FTransform& Transform = SourceTransforms[Point];
			FSource Source;
			Source.Position = Transform.GetLocation();
			Source.Axis = Transform.GetRotation().GetUpVector();
			if (bForce)
			{
				Source.Force = (Settings->ForceSpace == EPCGVoxelFlowSourceSpace::Point ? Transform.GetRotation().RotateVector(Forces[Point]) : Forces[Point])
					* Settings->SourceStrength;
			}
			Source.Outflow = bOutflow ? Outflows[Point] * Settings->SourceStrength : 0.0;
			Source.Swirl = bSwirl ? Swirls[Point] * Settings->SourceStrength : 0.0;
			if (Settings->bRadiusFromBounds)
			{
				Source.Radius = ((BoundsMax[Point] - BoundsMin[Point]) * 0.5 * Transform.GetScale3D().GetAbs()).GetMax();
				if (!(Source.Radius > 0.0))
				{
					FailSource(FText::Format(LOCTEXT("ZeroBoundsRadius",
						"point {0}: its largest scaled half-size is {1}, so it reaches nowhere; give the point bounds, or turn Radius From Bounds off and set Radius Attribute."),
						FText::AsNumber(Point), FText::AsNumber(Source.Radius)));
					return true;
				}
			}
			else
			{
				Source.Radius = Radii[Point];
				if (!(Source.Radius > 0.0))
				{
					FailSource(FText::Format(LOCTEXT("BadRadius", "point {0}: Radius Attribute '{1}' is {2}; it must be greater than 0."),
						FText::AsNumber(Point), FText::FromName(Settings->RadiusAttribute), FText::AsNumber(Source.Radius)));
					return true;
				}
			}
			if (!Source.Force.IsNearlyZero() || Source.Outflow != 0.0 || Source.Swirl != 0.0)
			{
				Sources.Add(Source);
			}
		}
	}
	const bool bHasSourceData = !SourceInputs.IsEmpty();
	double StrongestSource = 0.0;
	for (const FSource& Source : Sources)
	{
		StrongestSource = FMath::Max(StrongestSource, FMath::Max3(Source.Force.Size(), FMath::Abs(Source.Outflow), FMath::Abs(Source.Swirl)));
	}

	const bool bMakeCells = (Settings->bWriteExposure || Settings->bWriteSurfaceFlow)
		&& (!Context->Node || Context->Node->IsOutputPinConnected(PCGVoxelAirFlowPins::Cells));
	const int32 F = Settings->AirCellVoxels;
	const int32 Seed = Context->GetSeed();

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Inputs[InputIndex].Data);
		FPCGVoxelGrid Grid;
		if (!FPCGVoxelGrid::Build(Context, Points, Settings->Grid, Title, InputIndex, Grid) || Grid.NumCells() == 0)
		{
			continue;
		}

		// ---- Wind ----
		FVector WindValue = FVector::ZeroVector;
		FText Error;
		if (!Settings->Wind.TryReadDataValue(Points, WindValue, Error, LOCTEXT("WindLabel", "Wind"), InputIndex))
		{
			Fail(Error);
			continue;
		}
		if (WindValue.IsNearlyZero() && !bHasSourceData)
		{
			Fail(FText::Format(LOCTEXT("ZeroWind", "Wind is {0}; it must not be zero (its direction is the wind direction and its length the speed) unless the Sources pin has data."),
				FText::FromString(WindValue.ToString())), InputIndex);
			continue;
		}
		const FTransform& G = Grid.GridToWorld;
		const FVector WindGrid = Settings->WindSpace == EPCGVoxelDirectionSpace::World ? G.InverseTransformVectorNoScale(WindValue) : WindValue;
		const double WindSpeed = WindValue.IsNearlyZero() ? 0.0 : WindGrid.Size();
		const FVector WindDir = WindSpeed > 0.0 ? WindGrid / WindSpeed : FVector::ZeroVector;
		// What exposure 1 means: the wind speed, or the strongest source when sources alone drive the air.
		const double ReferenceSpeed = WindSpeed > 0.0 ? WindSpeed : (StrongestSource > 0.0 ? StrongestSource : 1.0);
		const FVector AirSize = Grid.VoxelSize * F;

		// ---- The air box ----
		FAir Air;
		Air.bGround = Settings->bGroundAtBottom;
		const FIntVector LowCell = FloorDiv(Grid.MinCell, F);
		const FIntVector HighCell = FloorDiv(Grid.MaxCell, F);
		Air.Origin = LowCell - FIntVector(Settings->Padding);
		if (Air.bGround)
		{
			Air.Origin.Z = LowCell.Z;
		}
		const FIntVector High = HighCell + FIntVector(Settings->Padding);
		const FIntVector Dims = High - Air.Origin + FIntVector(1);
		const int64 NumAir64 = static_cast<int64>(Dims.X) * Dims.Y * Dims.Z;
		if (NumAir64 > Settings->MaxAirCells)
		{
			Fail(FText::Format(LOCTEXT("TooMuchAir",
				"the air box is {0} x {1} x {2} = {3} air cells, more than Max Air Cells ({4}). Raise Air Cell Voxels, lower Padding, or raise Max Air Cells."),
				FText::AsNumber(Dims.X), FText::AsNumber(Dims.Y), FText::AsNumber(Dims.Z), FText::AsNumber(NumAir64),
				FText::AsNumber(Settings->MaxAirCells)), InputIndex);
			continue;
		}
		Air.Dims = Dims;
		const int32 NumAir = Air.Num();

		// ---- Which air cells are solid ----
		TArray<uint8> State;
		State.SetNumZeroed(NumAir);
		{
			TArray<uint16> Filled;
			Filled.SetNumZeroed(NumAir);
			for (const FIntVector& Cell : Grid.Cells)
			{
				uint16& Count = Filled[Air.Index(FloorDiv(Cell, F) - Air.Origin)];
				Count = static_cast<uint16>(FMath::Min<int32>(Count + 1, MAX_uint16));
			}
			const double Threshold = Settings->SolidFraction * F * F * F;
			for (int32 Index = 0; Index < NumAir; ++Index)
			{
				if (Filled[Index] > 0 && Filled[Index] > Threshold)
				{
					State[Index] = Solid;
				}
			}
		}

		// ---- Sealed air: not reachable from the open edge of the box ----
		{
			TArray<bool> Reached;
			Reached.SetNumZeroed(NumAir);
			TArray<int32> Queue;
			for (int32 Index = 0; Index < NumAir; ++Index)
			{
				if (State[Index] != Moving)
				{
					continue;
				}
				const FIntVector L = Air.Local(Index);
				const bool bEdge = L.X == 0 || L.X == Dims.X - 1 || L.Y == 0 || L.Y == Dims.Y - 1 || L.Z == Dims.Z - 1 || (L.Z == 0 && !Air.bGround);
				if (bEdge)
				{
					Reached[Index] = true;
					Queue.Add(Index);
				}
			}
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				const FIntVector L = Air.Local(Queue[Head]);
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					for (int32 Sign = -1; Sign <= 1; Sign += 2)
					{
						const FIntVector N = L + Unit[Axis] * Sign;
						if (!Air.Inside(N))
						{
							continue;
						}
						const int32 NI = Air.Index(N);
						if (State[NI] == Moving && !Reached[NI])
						{
							Reached[NI] = true;
							Queue.Add(NI);
						}
					}
				}
			}
			for (int32 Index = 0; Index < NumAir; ++Index)
			{
				if (State[Index] == Moving && !Reached[Index])
				{
					State[Index] = Sealed;
				}
			}
		}

		TArray<int32> Active;
		for (int32 Index = 0; Index < NumAir; ++Index)
		{
			if (State[Index] == Moving)
			{
				Active.Add(Index);
			}
		}
		const int32 NumActive = Active.Num();

		// ---- Distance to the solids (face steps), when asked for ----
		TArray<int32> Distance;
		if (Settings->MaxDistance > 0 || Settings->bWriteSolidDistance)
		{
			Distance.Init(MAX_int32, NumAir);
			TArray<int32> Queue;
			for (int32 Index = 0; Index < NumAir; ++Index)
			{
				if (State[Index] == Solid)
				{
					Distance[Index] = 0;
					Queue.Add(Index);
				}
			}
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				const int32 From = Queue[Head];
				const FIntVector L = Air.Local(From);
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					for (int32 Sign = -1; Sign <= 1; Sign += 2)
					{
						const FIntVector N = L + Unit[Axis] * Sign;
						if (Air.Inside(N) && Distance[Air.Index(N)] == MAX_int32)
						{
							Distance[Air.Index(N)] = Distance[From] + 1;
							Queue.Add(Air.Index(N));
						}
					}
				}
			}
		}

		// ---- Wind shadow: march upwind from each air cell to the first solid ----
		TArray<float> Shelter;
		Shelter.SetNumZeroed(NumAir);
		{
			FVector Step = -WindDir / AirSize; // in air cells
			if (WindSpeed > 0.0)
			{
				Step /= Step.GetAbsMax();      // one air cell along the main axis per step
			}
			const int32 Steps = WindSpeed > 0.0 ? FMath::CeilToInt32(Settings->WakeLength) : 0;
			const double Length = Settings->WakeLength;
			ParallelFor(NumAir, [&](int32 Index)
			{
				if (State[Index] == Sealed)
				{
					Shelter[Index] = 1.0f;
					return;
				}
				if (State[Index] != Moving || Steps == 0)
				{
					return;
				}
				const FVector From(Air.Local(Index));
				for (int32 S = 1; S <= Steps; ++S)
				{
					const FVector P = From + Step * S;
					const FIntVector N(FMath::RoundToInt32(P.X), FMath::RoundToInt32(P.Y), FMath::RoundToInt32(P.Z));
					if (!Air.Inside(N))
					{
						break;
					}
					if (State[Air.Index(N)] == Solid)
					{
						Shelter[Index] = static_cast<float>(FMath::Max(0.0, 1.0 - (S - 1) / Length));
						break;
					}
				}
			});
		}

		// ---- Starting wind per cell: free wind less the shadow, plus turbulence ----
		TArray<FVector3f> Velocity;
		Velocity.SetNumZeroed(NumAir);
		{
			FVector Offsets[3];
			FRandomStream Stream(PCGHelpers::ComputeSeed(Seed, InputIndex, 0xA1F));
			for (FVector& Offset : Offsets)
			{
				Offset = FVector(Stream.FRandRange(-1000.0, 1000.0), Stream.FRandRange(-1000.0, 1000.0), Stream.FRandRange(-1000.0, 1000.0));
			}
			const bool bTurbulence = Settings->TurbulenceStrength > 0.0;
			const double TurbulenceScale = Settings->TurbulenceStrength * ReferenceSpeed * 0.5;
			ParallelFor(NumActive, [&](int32 A)
			{
				const int32 Index = Active[A];
				FVector V = WindGrid * (1.0 - Settings->WakeStrength * Shelter[Index]);
				if (bTurbulence)
				{
					const FVector Position = FVector(Air.Local(Index) + Air.Origin) * AirSize;
					V += CurlNoise(Position / Settings->TurbulenceSize, Offsets) * TurbulenceScale;
				}
				Velocity[Index] = FVector3f(V);
			});
		}

		// ---- Sources: pushes and swirls go into the starting air; outflow goes into the solve as made (or lost) air ----
		TArray<float> Made; // per air cell, in the solve's units (face speed summed over the cell's faces)
		if (!Sources.IsEmpty())
		{
			Made.SetNumZeroed(NumAir);
			const double FaceArea = FMath::Pow(AirSize.X * AirSize.Y * AirSize.Z, 2.0 / 3.0);
			const FVector Half((F - 1) * 0.5);
			auto CellCentre = [&](const FIntVector& L) { return (FVector(L + Air.Origin) * F + Half) * Grid.VoxelSize; };
			TArray<TPair<int32, double>> Touched;
			for (const FSource& Source : Sources)
			{
				const FVector Centre = G.InverseTransformPositionNoScale(Source.Position);
				const FVector Force = G.InverseTransformVectorNoScale(Source.Force);
				const FVector Axis = G.InverseTransformVectorNoScale(Source.Axis).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
				const FVector CentreCells = (Centre / Grid.VoxelSize - Half) / F - FVector(Air.Origin);
				const FVector ReachCells = FVector(Source.Radius) / AirSize;
				const FIntVector Lo(
					FMath::Max(0, FMath::FloorToInt32(CentreCells.X - ReachCells.X)), FMath::Max(0, FMath::FloorToInt32(CentreCells.Y - ReachCells.Y)),
					FMath::Max(0, FMath::FloorToInt32(CentreCells.Z - ReachCells.Z)));
				const FIntVector Hi(
					FMath::Min(Dims.X - 1, FMath::CeilToInt32(CentreCells.X + ReachCells.X)), FMath::Min(Dims.Y - 1, FMath::CeilToInt32(CentreCells.Y + ReachCells.Y)),
					FMath::Min(Dims.Z - 1, FMath::CeilToInt32(CentreCells.Z + ReachCells.Z)));
				Touched.Reset();
				double TotalWeight = 0.0;
				for (int32 Z = Lo.Z; Z <= Hi.Z; ++Z)
				{
					for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
					{
						for (int32 X = Lo.X; X <= Hi.X; ++X)
						{
							const FIntVector L(X, Y, Z);
							const int32 Index = Air.Index(L);
							if (State[Index] != Moving)
							{
								continue;
							}
							const FVector Offset = CellCentre(L) - Centre;
							const double T = Offset.Size() / Source.Radius;
							if (T >= 1.0)
							{
								continue;
							}
							const double Weight = Falloff(Settings->SourceFalloff, T);
							FVector Add = Force * Weight;
							if (Source.Swirl != 0.0)
							{
								const FVector Across = Offset - Axis * (Offset | Axis);
								const double FromAxis = Across.Size();
								if (FromAxis > UE_KINDA_SMALL_NUMBER)
								{
									// Solid-body turning inside a small core, so the centre has no spike.
									const double Core = FMath::Min(1.0, FromAxis / (0.3 * Source.Radius));
									Add += (Axis ^ (Across / FromAxis)) * (Source.Swirl * Weight * Core);
								}
							}
							Velocity[Index] += FVector3f(Add);
							if (Source.Outflow != 0.0 && Weight > 0.0)
							{
								Touched.Emplace(Index, Weight);
								TotalWeight += Weight;
							}
						}
					}
				}
				if (Source.Outflow != 0.0)
				{
					// The air made per second, spread over the cells it reaches. A source smaller than a cell feeds the cell it is in.
					const double Total = Source.Outflow * 4.0 * UE_DOUBLE_PI * Source.Radius * Source.Radius / FaceArea;
					if (TotalWeight > 0.0)
					{
						for (const TPair<int32, double>& Cell : Touched)
						{
							Made[Cell.Key] += static_cast<float>(Total * Cell.Value / TotalWeight);
						}
					}
					else
					{
						const FIntVector Nearest(FMath::RoundToInt32(CentreCells.X), FMath::RoundToInt32(CentreCells.Y), FMath::RoundToInt32(CentreCells.Z));
						if (Air.Inside(Nearest) && State[Air.Index(Nearest)] == Moving)
						{
							Made[Air.Index(Nearest)] += static_cast<float>(Total);
						}
					}
				}
			}
		}

		// ---- Smoothing: 1-2-1 blur along each axis, over moving air only ----
		if (Settings->SmoothPasses > 0)
		{
			TArray<FVector3f> Temp;
			Temp.SetNumZeroed(NumAir);
			for (int32 Pass = 0; Pass < Settings->SmoothPasses; ++Pass)
			{
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					ParallelFor(NumActive, [&](int32 A)
					{
						const int32 Index = Active[A];
						const FIntVector L = Air.Local(Index);
						FVector3f Sum = Velocity[Index] * 2.0f;
						float Weight = 2.0f;
						for (int32 Sign = -1; Sign <= 1; Sign += 2)
						{
							const FIntVector N = L + Unit[Axis] * Sign;
							if (Air.Inside(N) && State[Air.Index(N)] == Moving)
							{
								Sum += Velocity[Air.Index(N)];
								Weight += 1.0f;
							}
						}
						Temp[Index] = Sum / Weight;
					});
					for (const int32 Index : Active)
					{
						Velocity[Index] = Temp[Index];
					}
				}
			}
		}

		TArray<FVector3f> Start;
		if (bMakeCells)
		{
			Start = Velocity;
		}

		// ---- Face velocities (staggered grid). A face is open when every side of it in the box is moving air ----
		TArray<float> Faces[3];
		TArray<bool> FaceOpen[3];
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const FIntVector FD = Air.FaceDims(Axis);
			const int32 NumFaces = FD.X * FD.Y * FD.Z;
			Faces[Axis].SetNumZeroed(NumFaces);
			FaceOpen[Axis].SetNumZeroed(NumFaces);
			ParallelFor(NumFaces, [&](int32 FaceIndex)
			{
				const FIntVector Hi(FaceIndex % FD.X, (FaceIndex / FD.X) % FD.Y, FaceIndex / (FD.X * FD.Y));
				const FIntVector Lo = Hi - Unit[Axis];
				const bool bInLo = Air.Inside(Lo), bInHi = Air.Inside(Hi);
				const bool bAirLo = bInLo && State[Air.Index(Lo)] == Moving;
				const bool bAirHi = bInHi && State[Air.Index(Hi)] == Moving;
				const bool bOpen = (bInLo ? bAirLo : !Air.IsFloor(Axis, -1)) && (bInHi ? bAirHi : true) && (bInLo || bInHi);
				if (!bOpen)
				{
					return;
				}
				FaceOpen[Axis][FaceIndex] = true;
				float Sum = 0.0f;
				int32 Count = 0;
				if (bAirLo) { Sum += Velocity[Air.Index(Lo)][Axis]; ++Count; }
				if (bAirHi) { Sum += Velocity[Air.Index(Hi)][Axis]; ++Count; }
				Faces[Axis][FaceIndex] = Count > 0 ? Sum / Count : 0.0f;
			});
		}

		// ---- Pressure solve: make the flow free of sources (conjugate gradient, Jacobi preconditioner) ----
		// For each moving air cell: sum over its open faces of (p - p_neighbour) = -divergence, with p = 0 outside the box.
		TArray<float> Pressure;
		Pressure.SetNumZeroed(NumAir);
		{
			TArray<uint8> Diagonal; // open faces per cell
			Diagonal.SetNumZeroed(NumAir);
			TArray<float> Residual, Direction, ADirection;
			Residual.SetNumZeroed(NumAir);
			Direction.SetNumZeroed(NumAir);
			ADirection.SetNumZeroed(NumAir);

			ParallelFor(NumActive, [&](int32 A)
			{
				const int32 Index = Active[A];
				const FIntVector L = Air.Local(Index);
				float Divergence = 0.0f;
				uint8 Open = 0;
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					const int32 LoFace = Air.FaceIndex(Axis, L), HiFace = Air.FaceIndex(Axis, L + Unit[Axis]);
					Divergence += Faces[Axis][HiFace] - Faces[Axis][LoFace];
					Open = static_cast<uint8>(Open + FaceOpen[Axis][LoFace] + FaceOpen[Axis][HiFace]);
				}
				Diagonal[Index] = Open;
				Residual[Index] = (Made.IsEmpty() ? 0.0f : Made[Index]) - Divergence;
			});

			auto ApplyA = [&](const TArray<float>& In, TArray<float>& Out)
			{
				ParallelFor(NumActive, [&](int32 A)
				{
					const int32 Index = Active[A];
					const FIntVector L = Air.Local(Index);
					float Sum = Diagonal[Index] * In[Index];
					for (int32 Axis = 0; Axis < 3; ++Axis)
					{
						for (int32 Sign = -1; Sign <= 1; Sign += 2)
						{
							const FIntVector N = L + Unit[Axis] * Sign;
							if (Air.Inside(N) && FaceOpen[Axis][Air.FaceIndex(Axis, Sign > 0 ? N : L)])
							{
								Sum -= In[Air.Index(N)];
							}
						}
					}
					Out[Index] = Sum;
				});
			};

			// z = r / diag; d = z.
			ParallelFor(NumActive, [&](int32 A)
			{
				const int32 Index = Active[A];
				Direction[Index] = Diagonal[Index] > 0 ? Residual[Index] / Diagonal[Index] : 0.0f;
			});
			double RZ = ParallelSum(NumActive, [&](int32 A) { const int32 I = Active[A]; return double(Residual[I]) * Direction[I]; });
			const double StartNorm = FMath::Sqrt(ParallelSum(NumActive, [&](int32 A) { const double R = Residual[Active[A]]; return R * R; }));
			const double Target = StartNorm * Settings->Tolerance;
			double Norm = StartNorm;
			int32 Iteration = 0;
			for (; Iteration < Settings->MaxIterations && StartNorm > 0.0 && RZ > 0.0; ++Iteration)
			{
				ApplyA(Direction, ADirection);
				const double DAD = ParallelSum(NumActive, [&](int32 A) { const int32 I = Active[A]; return double(Direction[I]) * ADirection[I]; });
				if (!(DAD > 0.0))
				{
					break;
				}
				const float Alpha = static_cast<float>(RZ / DAD);
				ParallelFor(NumActive, [&](int32 A)
				{
					const int32 I = Active[A];
					Pressure[I] += Alpha * Direction[I];
					Residual[I] -= Alpha * ADirection[I];
				});
				Norm = FMath::Sqrt(ParallelSum(NumActive, [&](int32 A) { const double R = Residual[Active[A]]; return R * R; }));
				if (Norm <= Target)
				{
					++Iteration;
					break;
				}
				const double NewRZ = ParallelSum(NumActive, [&](int32 A)
				{
					const int32 I = Active[A];
					return Diagonal[I] > 0 ? double(Residual[I]) * Residual[I] / Diagonal[I] : 0.0;
				});
				const float Beta = static_cast<float>(NewRZ / RZ);
				RZ = NewRZ;
				ParallelFor(NumActive, [&](int32 A)
				{
					const int32 I = Active[A];
					const float Z = Diagonal[I] > 0 ? Residual[I] / Diagonal[I] : 0.0f;
					Direction[I] = Z + Beta * Direction[I];
				});
			}
			UE_LOG(LogPCG, Verbose, TEXT("Voxel | Air Flow: input %d: %d moving air cells, solve took %d steps, error %.2g of the start."),
				InputIndex, NumActive, Iteration, StartNorm > 0.0 ? Norm / StartNorm : 0.0);
			if (StartNorm > 0.0 && Norm > Target)
			{
				PCGLog::LogWarningOnGraph(PCGVoxel::FormatError(Title, InputIndex, FText::Format(
					LOCTEXT("NotConverged", "the solve stopped at Max Iterations ({0}) with its error at {1} of the start, above Tolerance ({2}); some air may still push into walls. Raise Max Iterations or Air Cell Voxels."),
					FText::AsNumber(Settings->MaxIterations), FText::AsNumber(Norm / StartNorm), FText::AsNumber(Settings->Tolerance))), Context);
			}
		}

		// ---- Subtract the pressure gradient from open faces, then average faces back to cells ----
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const FIntVector FD = Air.FaceDims(Axis);
			ParallelFor(Faces[Axis].Num(), [&](int32 FaceIndex)
			{
				if (!FaceOpen[Axis][FaceIndex])
				{
					return;
				}
				const FIntVector Hi(FaceIndex % FD.X, (FaceIndex / FD.X) % FD.Y, FaceIndex / (FD.X * FD.Y));
				const FIntVector Lo = Hi - Unit[Axis];
				const float PLo = Air.Inside(Lo) ? Pressure[Air.Index(Lo)] : 0.0f;
				const float PHi = Air.Inside(Hi) ? Pressure[Air.Index(Hi)] : 0.0f;
				Faces[Axis][FaceIndex] -= PHi - PLo;
			});
		}
		ParallelFor(NumAir, [&](int32 Index)
		{
			if (State[Index] != Moving)
			{
				Velocity[Index] = FVector3f::ZeroVector;
				return;
			}
			const FIntVector L = Air.Local(Index);
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				Velocity[Index][Axis] = 0.5f * (Faces[Axis][Air.FaceIndex(Axis, L)] + Faces[Axis][Air.FaceIndex(Axis, L + Unit[Axis])]);
			}
		});
		for (TArray<float>& Face : Faces)
		{
			Face.Empty();
		}
		Pressure.Empty();

		const FQuat GridRotation = G.GetRotation();
		auto ToOutput = [&](const FVector& V)
		{
			return Settings->OutputSpace == EPCGVoxelDirectionSpace::World ? GridRotation.RotateVector(V) : V;
		};

		// ---- Flow output: one point per air cell (solid cells left out) ----
		{
			TArray<int32> Emit;
			for (int32 Index = 0; Index < NumAir; ++Index)
			{
				if (State[Index] != Solid && (Settings->MaxDistance == 0 || Distance[Index] <= Settings->MaxDistance))
				{
					Emit.Add(Index);
				}
			}
			const int32 NumOut = Emit.Num();
			UPCGBasePointData* Out = PCGUtilsAsyncObjects::NewObject<UPCGPointArrayData>(Context);
			Out->SetNumPoints(NumOut);
			Out->AllocateProperties(EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::BoundsMin
				| EPCGPointNativeProperties::BoundsMax | EPCGPointNativeProperties::Seed | EPCGPointNativeProperties::MetadataEntry);

			// The air cells form a grid of their own: cell c's centre sits at c * AirSize in it.
			const FTransform AirGrid(GridRotation, G.TransformPositionNoScale(FVector((F - 1) * 0.5) * Grid.VoxelSize));
			TPCGValueRange<FTransform> Transforms = Out->GetTransformValueRange();
			TPCGValueRange<FVector> Mins = Out->GetBoundsMinValueRange();
			TPCGValueRange<FVector> Maxs = Out->GetBoundsMaxValueRange();
			TPCGValueRange<int32> Seeds = Out->GetSeedValueRange();
			TPCGValueRange<int64> Entries = Out->GetMetadataEntryValueRange();
			ParallelFor(NumOut, [&](int32 P)
			{
				const FIntVector Cell = Air.Local(Emit[P]) + Air.Origin;
				Transforms[P] = FTransform(GridRotation, AirGrid.TransformPositionNoScale(FVector(Cell) * AirSize));
				Mins[P] = -AirSize * 0.5;
				Maxs[P] = AirSize * 0.5;
				Seeds[P] = PCGHelpers::ComputeSeed(Cell.X, Cell.Y, Cell.Z);
				Entries[P] = PCGInvalidEntryKey;
			});
			PCGVoxelPointIO::MakeEntriesUnique(Out);

			TArray<FVector> FlowValues;
			FlowValues.SetNumUninitialized(NumOut);
			for (int32 P = 0; P < NumOut; ++P)
			{
				FlowValues[P] = ToOutput(FVector(Velocity[Emit[P]]));
			}
			PCGVoxelPointIO::WritePointAttribute<FVector>(Out, Settings->FlowAttribute, FVector::ZeroVector, FlowValues);
			if (Settings->bWriteSpeed)
			{
				TArray<double> Values;
				Values.SetNumUninitialized(NumOut);
				for (int32 P = 0; P < NumOut; ++P)
				{
					Values[P] = FlowValues[P].Size();
				}
				PCGVoxelPointIO::WritePointAttribute<double>(Out, Settings->SpeedAttribute, 0.0, Values);
			}
			if (Settings->bWriteShelter)
			{
				TArray<double> Values;
				Values.SetNumUninitialized(NumOut);
				for (int32 P = 0; P < NumOut; ++P)
				{
					Values[P] = Shelter[Emit[P]];
				}
				PCGVoxelPointIO::WritePointAttribute<double>(Out, Settings->ShelterAttribute, 0.0, Values);
			}
			if (Settings->bWriteSolidDistance)
			{
				TArray<int32> Values;
				Values.SetNumUninitialized(NumOut);
				for (int32 P = 0; P < NumOut; ++P)
				{
					Values[P] = Distance[Emit[P]];
				}
				PCGVoxelPointIO::WritePointAttribute<int32>(Out, Settings->SolidDistanceAttribute, 0, Values);
			}
			if (FPCGMetadataDomain* Domain = PCGUtilsDataDomain::GetMutableDataDomain(Out))
			{
				const FName SizeName = Settings->Grid.VoxelSize.Attribute.GetAttributeName();
				PCGUtilsDataDomain::WriteValue(*Domain, SizeName.IsNone() ? FName(TEXT("VoxelSize")) : SizeName, AirSize);
				PCGUtilsDataDomain::WriteValue(*Domain, Settings->Grid.GridTransformAttribute, AirGrid);
			}

			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
			Output.Data = Out;
			Output.Pin = PCGVoxelAirFlowPins::Flow;
		}

		// ---- Cells output: the input with exposure and surface flow ----
		if (bMakeCells)
		{
			// Per cell: the air just outside each open side (stepping past air cells that are solid only because a
			// coarse air cell also holds other voxels).
			const int32 NumCells = Grid.NumCells();
			TArray<float> CellExposure;
			TArray<FVector3f> CellFlow;
			TArray<uint8> CellSides;
			CellExposure.SetNumZeroed(NumCells);
			CellFlow.SetNumZeroed(NumCells);
			CellSides.SetNumZeroed(NumCells);
			ParallelFor(NumCells, [&](int32 C)
			{
				const FIntVector Cell = Grid.Cells[C];
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					for (int32 Sign = -1; Sign <= 1; Sign += 2)
					{
						const FIntVector Step = Unit[Axis] * Sign;
						if (Grid.IsOccupied(Cell + Step))
						{
							continue;
						}
						FVector3f V = FVector3f(WindGrid);
						FVector3f VStart = FVector3f(WindGrid);
						FIntVector L = FloorDiv(Cell + Step, F) - Air.Origin;
						for (int32 K = 0; K < 3; ++K, L += Step)
						{
							if (!Air.Inside(L))
							{
								// Past the box: free wind, unless it is the floor.
								if (Air.IsFloor(Axis, Sign))
								{
									V = VStart = FVector3f::ZeroVector;
								}
								break;
							}
							const int32 Index = Air.Index(L);
							if (State[Index] != Solid)
							{
								V = Velocity[Index];
								VStart = Start[Index];
								break;
							}
							if (K == 2)
							{
								V = VStart = FVector3f::ZeroVector;
							}
						}
						const float Into = static_cast<float>(-VStart[Axis] * Sign / ReferenceSpeed);
						CellExposure[C] = FMath::Max(CellExposure[C], FMath::Clamp(Into, 0.0f, 1.0f));
						CellFlow[C] += V;
						++CellSides[C];
					}
				}
			});

			const int32 NumPoints = Points->GetNumPoints();
			TArray<double> PointExposure;
			TArray<FVector> PointFlowSum;
			TArray<int32> PointSides;
			PointExposure.SetNumZeroed(NumPoints);
			PointFlowSum.SetNumZeroed(NumPoints);
			PointSides.SetNumZeroed(NumPoints);
			for (int32 C = 0; C < NumCells; ++C)
			{
				const int32 Point = Grid.CellPoint[C];
				PointExposure[Point] = FMath::Max(PointExposure[Point], static_cast<double>(CellExposure[C]));
				PointFlowSum[Point] += FVector(CellFlow[C]);
				PointSides[Point] += CellSides[C];
			}

			UPCGBasePointData* Out = PCGUtilsAsyncObjects::NewObject<UPCGPointArrayData>(Context);
			Out->InitializeFromDataWithParams(FPCGInitializeFromDataParams(Points));
			Out->SetNumPoints(NumPoints, /*bInitializeValues=*/false);
			Out->AllocateProperties(Points->GetAllocatedProperties() | EPCGPointNativeProperties::MetadataEntry);
			{
				TArray<int32> Identity;
				Identity.SetNumUninitialized(NumPoints);
				for (int32 Point = 0; Point < NumPoints; ++Point)
				{
					Identity[Point] = Point;
				}
				Points->CopyPointsTo(Out, Identity, Identity);
			}
			PCGVoxelPointIO::MakeEntriesUnique(Out);
			if (Settings->bWriteExposure)
			{
				PCGVoxelPointIO::WritePointAttribute<double>(Out, Settings->ExposureAttribute, 0.0, PointExposure);
			}
			if (Settings->bWriteSurfaceFlow)
			{
				TArray<FVector> Values;
				Values.SetNumUninitialized(NumPoints);
				for (int32 Point = 0; Point < NumPoints; ++Point)
				{
					Values[Point] = PointSides[Point] > 0 ? ToOutput(PointFlowSum[Point] / PointSides[Point]) : FVector::ZeroVector;
				}
				PCGVoxelPointIO::WritePointAttribute<FVector>(Out, Settings->SurfaceFlowAttribute, FVector::ZeroVector, Values);
			}

			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[InputIndex]);
			Output.Data = Out;
			Output.Pin = PCGVoxelAirFlowPins::Cells;
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
