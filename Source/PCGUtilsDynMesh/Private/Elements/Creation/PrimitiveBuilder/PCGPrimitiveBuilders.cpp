// Copyright Max Harris

#include "Elements/Creation/PrimitiveBuilder/PCGPrimitiveBuilders.h"

#include "PCGContext.h"

#define LOCTEXT_NAMESPACE "PCGPrimitiveBuilders"

namespace
{
	/**
	 * Each node builds the matching Geometry Script options object from its own reflected properties, which
	 * PCG has already resolved any overrides into. The options object is a short-lived carrier for the
	 * Geometry Script call - it exists so the 11 AppendPrimitive() implementations stay the single source of
	 * truth for how a primitive is generated, not to hold settings.
	 */
	template <typename PrimitiveSettingsType>
	PrimitiveSettingsType* MakePrimitiveSettings(
		FPCGContext* InContext, const FGeometryScriptPrimitiveOptions& PrimitiveOptions)
	{
		PrimitiveSettingsType* Result = FPCGContext::NewObject_AnyThread<PrimitiveSettingsType>(InContext);
		if (Result)
		{
			Result->PrimitiveOptions = PrimitiveOptions;
		}
		return Result;
	}
}

#if WITH_EDITOR
#define PCG_DEFINE_BUILDER_TEXT(ClassName, Title, Tooltip) \
	FText ClassName::GetDefaultNodeTitle() const { return LOCTEXT(#ClassName "_Title", Title); } \
	FText ClassName::GetNodeTooltipText() const { return LOCTEXT(#ClassName "_Tooltip", Tooltip); }

PCG_DEFINE_BUILDER_TEXT(UPCGBoxBuilderSettings, "Box Builder",
	"A deferred Box: every dimension and step count is an overridable pin. Feed it, with any other Builders, into Create Primitive's Builders pin.")
PCG_DEFINE_BUILDER_TEXT(UPCGSphereBuilderSettings, "Sphere Builder",
	"A deferred Sphere (LatLong or Box tessellation) with overridable radius and step counts.")
PCG_DEFINE_BUILDER_TEXT(UPCGCapsuleBuilderSettings, "Capsule Builder",
	"A deferred Capsule with overridable radius, line length, and step counts.")
PCG_DEFINE_BUILDER_TEXT(UPCGCylinderBuilderSettings, "Cylinder Builder",
	"A deferred Cylinder with overridable radius, height, step counts, and capping.")
PCG_DEFINE_BUILDER_TEXT(UPCGConeBuilderSettings, "Cone Builder",
	"A deferred Cone or truncated cone with overridable base/top radii, height, and step counts.")
PCG_DEFINE_BUILDER_TEXT(UPCGTorusBuilderSettings, "Torus Builder",
	"A deferred Torus with overridable major/minor radii, step counts, and revolve options for partial arcs.")
PCG_DEFINE_BUILDER_TEXT(UPCGRectangleBuilderSettings, "Rectangle Builder",
	"A deferred flat Rectangle with overridable dimensions and subdivision counts.")
PCG_DEFINE_BUILDER_TEXT(UPCGRoundedRectangleBuilderSettings, "Rounded Rectangle Builder",
	"A deferred flat Rounded Rectangle with overridable dimensions, corner radius, and subdivision counts.")
PCG_DEFINE_BUILDER_TEXT(UPCGDiscBuilderSettings, "Disc Builder",
	"A deferred Disc, pie wedge, or annulus with overridable radius, angle range, and hole radius.")
PCG_DEFINE_BUILDER_TEXT(UPCGLinearStairsBuilderSettings, "Linear Stairs Builder",
	"Deferred straight stairs with overridable step size and count.")
PCG_DEFINE_BUILDER_TEXT(UPCGCurvedStairsBuilderSettings, "Curved Stairs Builder",
	"Deferred curved stairs with overridable step size, inner radius, curve angle, and count.")

#undef PCG_DEFINE_BUILDER_TEXT
#endif // WITH_EDITOR

UPCGCreatePrimitiveSettingsBase* UPCGBoxBuilderSettings::CreatePrimitiveSettings(FPCGContext* InContext) const
{
	UPCGCreatePrimitiveBoxSettings* Result =
		MakePrimitiveSettings<UPCGCreatePrimitiveBoxSettings>(InContext, PrimitiveOptions);
	if (Result)
	{
		Result->DimensionX = DimensionX;
		Result->DimensionY = DimensionY;
		Result->DimensionZ = DimensionZ;
		Result->StepsX = StepsX;
		Result->StepsY = StepsY;
		Result->StepsZ = StepsZ;
		Result->Origin = Origin;
	}
	return Result;
}

UPCGCreatePrimitiveSettingsBase* UPCGSphereBuilderSettings::CreatePrimitiveSettings(FPCGContext* InContext) const
{
	UPCGCreatePrimitiveSphereSettings* Result =
		MakePrimitiveSettings<UPCGCreatePrimitiveSphereSettings>(InContext, PrimitiveOptions);
	if (Result)
	{
		Result->Topology = Topology;
		Result->Radius = Radius;
		Result->StepsPhi = StepsPhi;
		Result->StepsTheta = StepsTheta;
		Result->StepsX = StepsX;
		Result->StepsY = StepsY;
		Result->StepsZ = StepsZ;
		Result->Origin = Origin;
	}
	return Result;
}

UPCGCreatePrimitiveSettingsBase* UPCGCapsuleBuilderSettings::CreatePrimitiveSettings(FPCGContext* InContext) const
{
	UPCGCreatePrimitiveCapsuleSettings* Result =
		MakePrimitiveSettings<UPCGCreatePrimitiveCapsuleSettings>(InContext, PrimitiveOptions);
	if (Result)
	{
		Result->Radius = Radius;
		Result->LineLength = LineLength;
		Result->HemisphereSteps = HemisphereSteps;
		Result->CircleSteps = CircleSteps;
		Result->SegmentSteps = SegmentSteps;
		Result->Origin = Origin;
	}
	return Result;
}

UPCGCreatePrimitiveSettingsBase* UPCGCylinderBuilderSettings::CreatePrimitiveSettings(FPCGContext* InContext) const
{
	UPCGCreatePrimitiveCylinderSettings* Result =
		MakePrimitiveSettings<UPCGCreatePrimitiveCylinderSettings>(InContext, PrimitiveOptions);
	if (Result)
	{
		Result->Radius = Radius;
		Result->Height = Height;
		Result->RadialSteps = RadialSteps;
		Result->HeightSteps = HeightSteps;
		Result->bCapped = bCapped;
		Result->Origin = Origin;
	}
	return Result;
}

UPCGCreatePrimitiveSettingsBase* UPCGConeBuilderSettings::CreatePrimitiveSettings(FPCGContext* InContext) const
{
	UPCGCreatePrimitiveConeSettings* Result =
		MakePrimitiveSettings<UPCGCreatePrimitiveConeSettings>(InContext, PrimitiveOptions);
	if (Result)
	{
		Result->BaseRadius = BaseRadius;
		Result->TopRadius = TopRadius;
		Result->Height = Height;
		Result->RadialSteps = RadialSteps;
		Result->HeightSteps = HeightSteps;
		Result->bCapped = bCapped;
		Result->Origin = Origin;
	}
	return Result;
}

UPCGCreatePrimitiveSettingsBase* UPCGTorusBuilderSettings::CreatePrimitiveSettings(FPCGContext* InContext) const
{
	UPCGCreatePrimitiveTorusSettings* Result =
		MakePrimitiveSettings<UPCGCreatePrimitiveTorusSettings>(InContext, PrimitiveOptions);
	if (Result)
	{
		Result->MajorRadius = MajorRadius;
		Result->MinorRadius = MinorRadius;
		Result->MajorSteps = MajorSteps;
		Result->MinorSteps = MinorSteps;
		Result->Origin = Origin;
		Result->RevolveOptions = RevolveOptions;
	}
	return Result;
}

UPCGCreatePrimitiveSettingsBase* UPCGRectangleBuilderSettings::CreatePrimitiveSettings(FPCGContext* InContext) const
{
	UPCGCreatePrimitiveRectangleSettings* Result =
		MakePrimitiveSettings<UPCGCreatePrimitiveRectangleSettings>(InContext, PrimitiveOptions);
	if (Result)
	{
		Result->DimensionX = DimensionX;
		Result->DimensionY = DimensionY;
		Result->StepsWidth = StepsWidth;
		Result->StepsHeight = StepsHeight;
	}
	return Result;
}

UPCGCreatePrimitiveSettingsBase* UPCGRoundedRectangleBuilderSettings::CreatePrimitiveSettings(
	FPCGContext* InContext) const
{
	UPCGCreatePrimitiveRoundedRectangleSettings* Result =
		MakePrimitiveSettings<UPCGCreatePrimitiveRoundedRectangleSettings>(InContext, PrimitiveOptions);
	if (Result)
	{
		Result->DimensionX = DimensionX;
		Result->DimensionY = DimensionY;
		Result->CornerRadius = CornerRadius;
		Result->StepsWidth = StepsWidth;
		Result->StepsHeight = StepsHeight;
		Result->StepsRound = StepsRound;
	}
	return Result;
}

UPCGCreatePrimitiveSettingsBase* UPCGDiscBuilderSettings::CreatePrimitiveSettings(FPCGContext* InContext) const
{
	UPCGCreatePrimitiveDiscSettings* Result =
		MakePrimitiveSettings<UPCGCreatePrimitiveDiscSettings>(InContext, PrimitiveOptions);
	if (Result)
	{
		Result->Radius = Radius;
		Result->AngleSteps = AngleSteps;
		Result->SpokeSteps = SpokeSteps;
		Result->StartAngle = StartAngle;
		Result->EndAngle = EndAngle;
		Result->HoleRadius = HoleRadius;
	}
	return Result;
}

UPCGCreatePrimitiveSettingsBase* UPCGLinearStairsBuilderSettings::CreatePrimitiveSettings(
	FPCGContext* InContext) const
{
	UPCGCreatePrimitiveLinearStairsSettings* Result =
		MakePrimitiveSettings<UPCGCreatePrimitiveLinearStairsSettings>(InContext, PrimitiveOptions);
	if (Result)
	{
		Result->StepWidth = StepWidth;
		Result->StepHeight = StepHeight;
		Result->StepDepth = StepDepth;
		Result->NumSteps = NumSteps;
		Result->bFloating = bFloating;
	}
	return Result;
}

UPCGCreatePrimitiveSettingsBase* UPCGCurvedStairsBuilderSettings::CreatePrimitiveSettings(
	FPCGContext* InContext) const
{
	UPCGCreatePrimitiveCurvedStairsSettings* Result =
		MakePrimitiveSettings<UPCGCreatePrimitiveCurvedStairsSettings>(InContext, PrimitiveOptions);
	if (Result)
	{
		Result->StepWidth = StepWidth;
		Result->StepHeight = StepHeight;
		Result->InnerRadius = InnerRadius;
		Result->CurveAngle = CurveAngle;
		Result->NumSteps = NumSteps;
		Result->bFloating = bFloating;
	}
	return Result;
}

#undef LOCTEXT_NAMESPACE
