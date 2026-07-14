#include "Components/PCGSplineComponent.h"

#include "PCGActorBase.h"

UPCGSplineComponent::UPCGSplineComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	
}

void UPCGSplineComponent::CopyLegacyPathData()
{
	Modify();
	PathData.ProcessPathGraph = PreProcessSplineGraph;
	PathData.Height = Height;
	PathData.GroupID = GroupID;
	PathData.bSetPathDensity = bSetPathDensity;
	PathData.PathDensity = PathDensity;
	PathData.bSetPathColor = bSetPathColor;
	PathData.PathColor = PathColor;
	MarkPackageDirty();
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
		if (APCGActorBase* Actor = Cast<APCGActorBase>(GetOwner()))
		{
			Actor->TriggerRegeneratePCGOnComponentEdits(this);
		}
	}
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
