// Copyright Max Harris

#include "Elements/PCGVoxelTransformGrid.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGPointArrayData.h"
#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"
#include "Voxel/PCGVoxelGrid.h"

#define LOCTEXT_NAMESPACE "PCGVoxelTransformGrid"

UPCGVoxelTransformGridSettings::UPCGVoxelTransformGridSettings()
	: Translation(FVector::ZeroVector)
	, Rotation(FRotator::ZeroRotator)
	, Scale(FVector::OneVector)
	, PivotAlignment(FVector(0.5))
	, PivotLocation(FVector::ZeroVector)
{
	// Update parses the @Data. prefix into the domain; the constructor's default name would keep it in the name.
	Translation.Attribute.Update(TEXT("@Data.Translation"));
	Rotation.Attribute.Update(TEXT("@Data.Rotation"));
	Scale.Attribute.Update(TEXT("@Data.Scale"));
	PivotAlignment.Attribute.Update(TEXT("@Data.PivotAlignment"));
	PivotLocation.Attribute.Update(TEXT("@Data.Pivot"));
}

#if WITH_EDITOR
FText UPCGVoxelTransformGridSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Voxel | Transform Grid");
}

FText UPCGVoxelTransformGridSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Moves, turns and scales whole voxel sets about a pivot. The grid transform and Voxel Size are updated and every point "
		"moves with them, so cells, spans and per-cell values are unchanged. Each value can be read from each set's @Data, "
		"so parts split from one grid can each turn their own way about their own pivot.");
}
#endif

TArray<FPCGPinProperties> UPCGVoxelTransformGridSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("InTooltip", "Voxel sets. Each is transformed as a whole, with values from the node or from its own @Data."))
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGVoxelTransformGridSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("OutTooltip", "One voxel set per input: the same points, metadata and tags, with the new grid transform and Voxel Size in @Data."));
	return Pins;
}

FPCGElementPtr UPCGVoxelTransformGridSettings::CreateElement() const
{
	return MakeShared<FPCGVoxelTransformGridElement>();
}

bool FPCGVoxelTransformGridElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGVoxelTransformGridElement::ExecuteInternal);
	check(Context);

	const UPCGVoxelTransformGridSettings* Settings = Context->GetInputSettings<UPCGVoxelTransformGridSettings>();
	check(Settings);
	const FText Title = LOCTEXT("ErrorTitle", "Voxel | Transform Grid");
	static const TCHAR* AxisNames[3] = { TEXT("X"), TEXT("Y"), TEXT("Z") };

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Inputs[Index].Data);
		FPCGVoxelGrid Grid;
		if (!FPCGVoxelGrid::Build(Context, Points, Settings->Grid, Title, Index, Grid))
		{
			continue;
		}

		// This set's values.
		FVector Translation = FVector::ZeroVector, ScaleValue = FVector::OneVector, Alignment = FVector(0.5), PivotLocation = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FText Error;
		auto Fail = [&]()
		{
			PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, INDEX_NONE, Error), Context);
		};
		if (!Settings->Translation.TryReadDataValue(Points, Translation, Error, LOCTEXT("TranslationLabel", "Translation"), Index)
			|| !Settings->Rotation.TryReadDataValue(Points, Rotation, Error, LOCTEXT("RotationLabel", "Rotation"), Index)
			|| !Settings->Scale.TryReadDataValue(Points, ScaleValue, Error, LOCTEXT("ScaleLabel", "Scale"), Index))
		{
			Fail();
			continue;
		}
		if (Settings->Pivot == EPCGVoxelTransformPivot::Bounds
			? !Settings->PivotAlignment.TryReadDataValue(Points, Alignment, Error, LOCTEXT("AlignmentLabel", "Pivot Alignment"), Index)
			: !Settings->PivotLocation.TryReadDataValue(Points, PivotLocation, Error, LOCTEXT("PivotLocationLabel", "Pivot Location"), Index))
		{
			Fail();
			continue;
		}
		bool bScaleOk = true;
		for (int32 Axis = 0; Axis < 3 && bScaleOk; ++Axis)
		{
			if (!(ScaleValue[Axis] > 0.0))
			{
				PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, Index, FText::Format(
					LOCTEXT("ScaleAxis", "Scale {0} is {1}; every axis must be greater than 0."),
					FText::FromString(AxisNames[Axis]), FText::AsNumber(ScaleValue[Axis]))), Context);
				bScaleOk = false;
			}
		}
		if (!bScaleOk)
		{
			continue;
		}
		const bool bScaled = !ScaleValue.Equals(FVector::OneVector, UE_KINDA_SMALL_NUMBER);
		if (bScaled && Settings->Grid.VoxelSize.IsConstant())
		{
			PCGLog::LogErrorOnGraph(PCGVoxel::FormatError(Title, Index, FText::Format(
				LOCTEXT("ScaleNeedsDataSize", "Scale is {0}, which changes the cell size, but Voxel Size is a Constant; read Voxel Size from @Data so the new size can be written there."),
				FText::FromString(ScaleValue.ToString()))), Context);
			continue;
		}

		// Everything below is in the input grid's space: x -> P + R (S * (x - P)) + T.
		const FTransform& G = Grid.GridToWorld;
		const FQuat GridRotation = G.GetRotation();
		FQuat R = Rotation.Quaternion();
		FVector T = Translation;
		FVector P;
		if (Settings->Pivot == EPCGVoxelTransformPivot::Bounds)
		{
			const FVector Lo = (FVector(Grid.MinCell) - FVector(0.5)) * Grid.VoxelSize;
			const FVector Hi = (FVector(Grid.MaxCell) + FVector(0.5)) * Grid.VoxelSize;
			P = Grid.NumCells() > 0 ? Lo + Alignment * (Hi - Lo) : FVector::ZeroVector;
		}
		else
		{
			P = Settings->Space == EPCGVoxelTransformSpace::World ? G.InverseTransformPositionNoScale(PivotLocation) : PivotLocation;
		}
		if (Settings->Space == EPCGVoxelTransformSpace::World)
		{
			R = GridRotation.Inverse() * R * GridRotation;
			T = GridRotation.UnrotateVector(Translation);
		}

		// New grid: rotation G.R * R, origin at G(P - R (S * P) + T), cells ScaleValue times larger. A cell keeps its index.
		const FVector NewVoxelSize = Grid.VoxelSize * ScaleValue;
		const FQuat NewRotation = (GridRotation * R).GetNormalized();
		const FTransform NewGrid(NewRotation, G.TransformPositionNoScale(P - R.RotateVector(ScaleValue * P) + T));

		// The output is the input, copied point for point, then moved.
		const int32 NumPoints = Points->GetNumPoints();
		UPCGBasePointData* Out = PCGUtilsAsyncObjects::NewObject<UPCGPointArrayData>(Context);
		Out->InitializeFromDataWithParams(FPCGInitializeFromDataParams(Points));
		Out->SetNumPoints(NumPoints, /*bInitializeValues=*/false);
		Out->AllocateProperties(Points->GetAllocatedProperties() | EPCGPointNativeProperties::Transform);
		{
			TArray<int32> Identity;
			Identity.SetNumUninitialized(NumPoints);
			for (int32 Point = 0; Point < NumPoints; ++Point)
			{
				Identity[Point] = Point;
			}
			Points->CopyPointsTo(Out, Identity, Identity);
		}
		TPCGValueRange<FTransform> Transforms = Out->GetTransformValueRange();
		ParallelFor(NumPoints, [&](int32 Point)
		{
			FTransform& Transform = Transforms[Point];
			const FVector X = G.InverseTransformPositionNoScale(Transform.GetLocation());
			const FQuat Local = GridRotation.Inverse() * Transform.GetRotation();
			FVector PointScale = Transform.GetScale3D();
			if (bScaled)
			{
				// Each local axis of the point scales by the grid scale along the grid axis it lies on (exact for points
				// turned in 90 degree steps against the grid, blended otherwise).
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					FVector Unit = FVector::ZeroVector;
					Unit[Axis] = 1.0;
					const FVector Along = Local.RotateVector(Unit);
					PointScale[Axis] *= Along.X * Along.X * ScaleValue.X + Along.Y * Along.Y * ScaleValue.Y + Along.Z * Along.Z * ScaleValue.Z;
				}
			}
			Transform = FTransform((NewRotation * Local).GetNormalized(), NewGrid.TransformPositionNoScale(ScaleValue * X), PointScale);
		});

		if (FPCGMetadataDomain* Domain = PCGUtilsDataDomain::GetMutableDataDomain(Out))
		{
			PCGUtilsDataDomain::WriteValue(*Domain, Settings->Grid.GridTransformAttribute, NewGrid);
			if (bScaled)
			{
				PCGUtilsDataDomain::WriteValue(*Domain, Settings->Grid.VoxelSize.Attribute.GetAttributeName(), NewVoxelSize);
			}
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[Index]);
		Output.Data = Out;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
