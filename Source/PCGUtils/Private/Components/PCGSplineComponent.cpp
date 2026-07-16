#include "Components/PCGSplineComponent.h"

#include "PCGUtilsHelpers.h"

UPCGSplineComponent::UPCGSplineComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	
}

void UPCGSplineComponent::PostLoad()
{
	Super::PostLoad();
	if (!bPathDataMigrated)
	{
		PathData.ProcessPathGraph = PreProcessSplineGraph;
		PathData.Height = Height;
		PathData.GroupID = GroupID;
		PathData.bSetPathDensity = bSetPathDensity;
		PathData.PathDensity = PathDensity;
		PathData.bSetPathColor = bSetPathColor;
		PathData.PathColor = PathColor;
		bPathDataMigrated = true;
	}
}

void UPCGSplineComponent::OnComponentCreated()
{
	Super::OnComponentCreated();
	bPathDataMigrated = true;
}

TArray<FPCGPoint> UPCGSplineComponent::GetPathPoints_Implementation() const
{
	TArray<FPCGPoint> Result;
	const int32 NumSplinePoints = GetNumberOfSplinePoints();
	if (NumSplinePoints == 0)
	{
		return Result;
	}

	const bool bPathIsClosedLoop = IsClosedLoop();
	const int32 NumSegments = bPathIsClosedLoop ? NumSplinePoints : NumSplinePoints - 1;
	const int32 InteriorSamples = FMath::Max(0, PathSubdivisionCount);
	const float Density = PathData.GetPathDensity();
	const FLinearColor Color = PathData.GetPathColor();
	Result.Reserve(NumSplinePoints + NumSegments * InteriorSamples);

	auto AddPoint = [&Result, Density, Color](const FTransform& LocalTransform)
	{
		FPCGPoint& Point = Result.Emplace_GetRef();
		Point.Transform = LocalTransform;
		Point.SetExtents(FVector(50.f));
		Point.Density = Density;
		Point.Color = Color;
	};

	for (int32 SegmentIndex = 0; SegmentIndex < NumSegments; ++SegmentIndex)
	{
		AddPoint(GetTransformAtSplinePoint(SegmentIndex, ESplineCoordinateSpace::Local, true));

		const int32 NextPointIndex = (SegmentIndex + 1) % NumSplinePoints;
		const bool bLinearSegment =
			GetSplinePointType(SegmentIndex) == ESplinePointType::Linear
			&& GetSplinePointType(NextPointIndex) == ESplinePointType::Linear;
		if (!bLinearSegment)
		{
			for (int32 SampleIndex = 1; SampleIndex <= InteriorSamples; ++SampleIndex)
			{
				const float Alpha = static_cast<float>(SampleIndex) / static_cast<float>(InteriorSamples + 1);
				AddPoint(GetTransformAtSplineInputKey(
					static_cast<float>(SegmentIndex) + Alpha,
					ESplineCoordinateSpace::Local,
					true));
			}
		}
	}

	if (!bPathIsClosedLoop)
	{
		AddPoint(GetTransformAtSplinePoint(NumSplinePoints - 1, ESplineCoordinateSpace::Local, true));
	}

	return Result;
}

#if WITH_EDITOR
void UPCGSplineComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	switch (GetSplineLoopMode())
	{
	case ESplineLoopMode::ClosedLoopOnly:
		SetClosedLoop(true, true);
		break;
	case ESplineLoopMode::OpenLoopOnly:
		SetClosedLoop(false, false);
		break;
	default:
		break;
	}

#if WITH_EDITORONLY_DATA
	bool bHasScriptedEditorColors = false;
	FLinearColor SelectedColor;
	FLinearColor UnselectedColor;
	FLinearColor TangentColor;

	GetSplineEditorColors(bHasScriptedEditorColors, UnselectedColor, SelectedColor, TangentColor);
	if (bHasScriptedEditorColors)
	{
		EditorUnselectedSplineSegmentColor = UnselectedColor;
		EditorSelectedSplineSegmentColor = SelectedColor;
		EditorTangentColor = TangentColor;
	}
#endif

	if (bRegeneratePCGOnSplineEdits)
	{
		UPCGUtilsHelpers::TryRefreshPCGGeneration(this, true);
	}
}

void UPCGSplineComponent::PostEditComponentMove(bool bFinished)
{
	Super::PostEditComponentMove(bFinished);
}

#endif

void UPCGSplineComponent::GetSplineEditorColors_Implementation(
	bool& bHasScriptedEditorColors,
	FLinearColor& UnselectedColor,
	FLinearColor& SelectedColor,
	FLinearColor& TangentColor) const
{
	bHasScriptedEditorColors = false;
	UnselectedColor = FLinearColor(0.1f, 0.8f, 0.1f, 1.f);
	SelectedColor = FLinearColor(1.0f, 0.9f, 0.1f, 1.f);
	TangentColor = FLinearColor(1.0f, 0.9f, 0.1f, 1.f);
}
