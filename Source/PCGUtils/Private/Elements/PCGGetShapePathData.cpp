#include "Elements/PCGGetShapePathData.h"

#include "Modifiers/PCGUtilsModifierStack.h"
#include "Data/PCGSplineData.h"
#include "Data/PCGPathPoint.h"
#include "ShapePath/ShapePathComponent.h"
#include "Data/PCGUtilsComponentData.h"
#include "Data/PCGPointData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/PCGMetadataCommon.h"
#include "Data/PCGBasePointData.h"

#include "GameFramework/Actor.h"
#include "Algo/Transform.h"

#define LOCTEXT_NAMESPACE "PCGGetShapePathElement"

UPCGGetShapePathSettings::UPCGGetShapePathSettings()
{
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;
	bAlwaysRequeryActors = true;
}

#if WITH_EDITOR
FText UPCGGetShapePathSettings::GetDefaultNodeTitle() const
{
	return NSLOCTEXT("PCGGetShapePathElement", "NodeTitle", "Get Shape Path Data");
}

FText UPCGGetShapePathSettings::GetNodeTooltipText() const
{
	return NSLOCTEXT("PCGGetShapePathElement", "NodeTooltip",
		"Collects UShapePathComponent path points from actors and outputs them as PCG PointData. "
		"One PointData collection per component — each path point becomes one PCG point in world space.");
}
#endif

TArray<FPCGPinProperties> UPCGGetShapePathSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point);
	return PinProperties;
}

FPCGElementPtr UPCGGetShapePathSettings::CreateElement() const
{
	return MakeShared<FPCGGetShapePathElement>();
}

void FPCGGetShapePathElement::ProcessActor(
	FPCGContext* Context,
	const UPCGDataFromActorSettings* Settings,
	AActor* FoundActor) const
{
	check(Context && Settings);
	if (!FoundActor || !IsValid(FoundActor))
	{
		return;
	}
	const UPCGGetShapePathSettings* GetSettings = CastChecked<UPCGGetShapePathSettings>(Settings);

	auto NameToString = [](const FName& N) { return N.ToString(); };
	TSet<FString> ActorTags;
	Algo::Transform(FoundActor->Tags, ActorTags, NameToString);

	TInlineComponentArray<UShapePathComponent*, 4> ShapeComps;
	FoundActor->GetComponents(ShapeComps);

	
	
	for (UShapePathComponent* Comp : ShapeComps)
	{
		if (!Comp || !IsValid(Comp))// || Settings->ComponentSelector.FilterComponent(Comp))
		{
			continue;
		}
		
		const TArray<FVector>& LocalPoints = Comp->GetGeneratedPathPoints();
		if (LocalPoints.IsEmpty())
		{
			continue;
		}

		const FLinearColor PathColor = Comp->PathData.GetPathColor();
		const float PathDensity = Comp->PathData.GetPathDensity();
		
		const FTransform CompTransform = Comp->GetComponentTransform();

		// One output per path the modifier stack produces; without a stack, just the component's own path.
		struct FShapeOutput
		{
			TArray<FVector> WorldPositions;
			bool bIsClosed = false;
			const UPCGData* ModifierData = nullptr;
		};
		TArray<FShapeOutput> Outputs;
		if (FPCGUtilsModifierHelpers::HasStack(Comp))
		{
			TArray<FPCGPathPoint> PathPoints;
			PathPoints.Reserve(LocalPoints.Num());
			for (const FVector& LocalPos : LocalPoints)
			{
				PathPoints.AddDefaulted_GetRef().Transform = FTransform(LocalPos);
			}
			UPCGSplineData* SourceSpline = FPCGUtilsModifierHelpers::MakeSpline(
				Context, PathPoints, /*bIsLocalSpace=*/true, CompTransform, /*bIsLinearPath=*/true, Comp->IsClosedLoop());
			for (const UPCGSplineData* Result : FPCGUtilsModifierHelpers::ApplyOwnerStackToPath(Context, Comp, SourceSpline))
			{
				FShapeOutput& Output = Outputs.AddDefaulted_GetRef();
				for (const FPCGPathPoint& Point : FPCGUtilsModifierHelpers::SplineToPathPoints(Result))
				{
					Output.WorldPositions.Add(Point.Transform.GetLocation());
				}
				Output.bIsClosed = Result->IsClosed();
				Output.ModifierData = Result;
			}
		}
		else
		{
			FShapeOutput& Output = Outputs.AddDefaulted_GetRef();
			for (const FVector& LocalPos : LocalPoints)
			{
				Output.WorldPositions.Add(CompTransform.TransformPosition(LocalPos));
			}
			Output.bIsClosed = Comp->IsClosedLoop();
		}

		for (const FShapeOutput& Output : Outputs)
		{
			if (Output.WorldPositions.IsEmpty())
			{
				continue;
			}

			UPCGPointData* PointData = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
			TArray<FPCGPoint>& PCGPoints = PointData->GetMutablePoints();
			PCGPoints.Reserve(Output.WorldPositions.Num());

			for (const FVector& WorldPos : Output.WorldPositions)
			{
				FPCGPoint& Point = PCGPoints.Emplace_GetRef();
				Point.Transform = FTransform(WorldPos);
				Point.SetExtents(FVector(50.f));
				Point.Density = PathDensity;
				Point.Color = PathColor;
			}

			// Stamp @Data domain attributes using explicit FPCGAttributeIdentifier with
			// PCGMetadataDomainID::Data. The @Data domain has no per-entry rows, so
			// DefaultValue in FindOrCreateAttribute IS the stored value.
			if (UPCGMetadata* Meta = PointData->MutableMetadata())
			{
				// Attributes a modifier wrote on its path (copy index, mirror side) come across first.
				FPCGUtilsModifierHelpers::CopyDataDomain(Output.ModifierData, PointData);

				// isClosed mirrors $isClosed on spline data - always written so graphs can
				// filter open vs. closed without a missing-attribute fallback.
				if (FPCGMetadataAttribute<bool>* IsClosedAttribute =
									Meta->FindOrCreateAttribute<bool>(
										FPCGAttributeIdentifier(FName("IsClosed"), PCGMetadataDomainID::Data),
										Output.bIsClosed,
										/*bAllowsInterpolation=*/false,
										/*bOverrideParent=*/false,
										/*bOverwriteIfTypeMismatch=*/true))
				{
					IsClosedAttribute->SetValue(PCGInvalidEntryKey, Output.bIsClosed);
				}

				UPCGUtilPathDataLibrary::GetComponentDataFromSettings(
					Meta, &GetSettings->ComponentSettings, Comp);

				UPCGUtilPathDataLibrary::GetPathDataFromSettings(
					Meta, &GetSettings->PathSettings, &Comp->PathData);
			}

			FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
			TaggedData.Data = PointData;
			Algo::Transform(Comp->ComponentTags, TaggedData.Tags, NameToString);
			TaggedData.Tags.Append(ActorTags);
		}
	}
}

#undef LOCTEXT_NAMESPACE
