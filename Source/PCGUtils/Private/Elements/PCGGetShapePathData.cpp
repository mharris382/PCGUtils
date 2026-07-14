#include "Elements/PCGGetShapePathData.h"

#include "ShapePath/ShapePathComponent.h"
#include "Data/PathComponentData.h"
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
		if (!Comp || !IsValid(Comp))
		{
			continue;
		}

		const TArray<FVector>& LocalPoints = Comp->GetPathPoints();
		if (LocalPoints.IsEmpty())
		{
			continue;
		}

		const FLinearColor PathColor = Comp->PathData.GetPathColor(FLinearColor::White);
		const float PathDensity = Comp->PathData.GetPathDensity(1.0f);
		
		const FTransform CompTransform = Comp->GetComponentTransform();

		UPCGPointData* PointData = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
		TArray<FPCGPoint>& PCGPoints = PointData->GetMutablePoints();
		PCGPoints.Reserve(LocalPoints.Num());
		
		for (const FVector& LocalPos : LocalPoints)
		{
			FPCGPoint& Point = PCGPoints.Emplace_GetRef();
			Point.Transform = FTransform(CompTransform.TransformPosition(LocalPos));
			Point.SetExtents(FVector(50.f));
			Point.Density = PathDensity;
			Point.Color = PathColor;
		}

		// Stamp @Data domain attributes using explicit FPCGAttributeIdentifier with
		// PCGMetadataDomainID::Data. The @Data domain has no per-entry rows, so
		// DefaultValue in FindOrCreateAttribute IS the stored value.
		if (UPCGMetadata* Meta = PointData->MutableMetadata())
		{
			// isClosed mirrors $isClosed on spline data — always written so graphs can
			// filter open vs. closed without a missing-attribute fallback.
			
			if (FPCGMetadataAttribute<bool>* IsClosedAttribute =
								Meta->FindOrCreateAttribute<bool>(
									FPCGAttributeIdentifier(FName("IsClosed"), PCGMetadataDomainID::Data),
									Comp->IsClosedLoop(),
									/*bAllowsInterpolation=*/false,
									/*bOverrideParent=*/false,
									/*bOverwriteIfTypeMismatch=*/true))
			{
				IsClosedAttribute->SetValue(PCGInvalidEntryKey, Comp->IsClosedLoop());
			}
			
			if (GetSettings->bOutputActorReference)
			{
				Meta->FindOrCreateAttribute<FSoftObjectPath>(
					FPCGAttributeIdentifier(PCGPointDataConstants::ActorReferenceAttribute, PCGMetadataDomainID::Data),
					FSoftObjectPath(FoundActor),
					/*bAllowsInterpolation=*/false,
					/*bOverrideParent=*/false,
					/*bOverwriteIfTypeMismatch=*/false);
			}

			if (GetSettings->bOutputComponentReference)
			{
				Meta->FindOrCreateAttribute<FSoftObjectPath>(
					FPCGAttributeIdentifier(FName("ComponentReference"), PCGMetadataDomainID::Data),
					FSoftObjectPath(Comp),
					/*bAllowsInterpolation=*/false,
					/*bOverrideParent=*/false,
					/*bOverwriteIfTypeMismatch=*/false);
			}
			

			UPCGUtilPathDataLibrary::GetPathDataFromSettings(
				Meta, &GetSettings->PathSettings, &Comp->PathData);
		}

		FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = PointData;
		Algo::Transform(Comp->ComponentTags, TaggedData.Tags, NameToString);
		TaggedData.Tags.Append(ActorTags);
	}
}

#undef LOCTEXT_NAMESPACE
