// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Creation/CreatePrimitive/PCGCreatePrimitiveSettingsBase.h"
#include "Elements/Creation/PrimitiveBuilder/PCGPrimitiveBuilderFactory.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"

#include "PCGPrimitiveBuilders.generated.h"

/**
 * One Builder node per Geometry Script primitive type.
 *
 * Every primitive parameter below is a plain `PCG_Overridable` property on the node, which is the whole reason
 * these are separate nodes rather than one node with an inline primitive picker: PCG can only build override
 * pins from properties reflected directly on the Settings object (plus one struct level). Anything living
 * inside an inline instanced UObject is invisible to the override system, so a box's dimensions and step
 * counts could never be driven from the graph. Here they can.
 *
 * The shared Fitting block and the Builder output pin come from UPCGPrimitiveBuilderProviderSettingsBase; each
 * node only supplies its own parameters and the Geometry Script options object built from them.
 */

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Creation")
class PCGUTILSDYNMESH_API UPCGBoxBuilderSettings : public UPCGPrimitiveBuilderProviderSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("BoxBuilder"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box", meta = (PCG_Overridable, ClampMin = "0"))
	float DimensionX = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box", meta = (PCG_Overridable, ClampMin = "0"))
	float DimensionY = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box", meta = (PCG_Overridable, ClampMin = "0"))
	float DimensionZ = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box", meta = (PCG_Overridable, ClampMin = "0"))
	int32 StepsX = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box", meta = (PCG_Overridable, ClampMin = "0"))
	int32 StepsY = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box", meta = (PCG_Overridable, ClampMin = "0"))
	int32 StepsZ = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box", meta = (PCG_Overridable))
	EGeometryScriptPrimitiveOriginMode Origin = EGeometryScriptPrimitiveOriginMode::Base;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Options", meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptPrimitiveOptions PrimitiveOptions;

protected:
	virtual UPCGCreatePrimitiveSettingsBase* CreatePrimitiveSettings(FPCGContext* InContext) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Creation")
class PCGUTILSDYNMESH_API UPCGSphereBuilderSettings : public UPCGPrimitiveBuilderProviderSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SphereBuilder"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** LatLong is the classic latitude/longitude sphere; Box projects a subdivided cube. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sphere", meta = (PCG_Overridable))
	EPCGCreatePrimitiveSphereTopology Topology = EPCGCreatePrimitiveSphereTopology::LatLong;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sphere", meta = (PCG_Overridable, ClampMin = "0"))
	float Radius = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sphere", meta = (PCG_Overridable, ClampMin = "2",
		EditCondition = "Topology==EPCGCreatePrimitiveSphereTopology::LatLong", EditConditionHides))
	int32 StepsPhi = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sphere", meta = (PCG_Overridable, ClampMin = "3",
		EditCondition = "Topology==EPCGCreatePrimitiveSphereTopology::LatLong", EditConditionHides))
	int32 StepsTheta = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sphere", meta = (PCG_Overridable, ClampMin = "1",
		EditCondition = "Topology==EPCGCreatePrimitiveSphereTopology::Box", EditConditionHides))
	int32 StepsX = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sphere", meta = (PCG_Overridable, ClampMin = "1",
		EditCondition = "Topology==EPCGCreatePrimitiveSphereTopology::Box", EditConditionHides))
	int32 StepsY = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sphere", meta = (PCG_Overridable, ClampMin = "1",
		EditCondition = "Topology==EPCGCreatePrimitiveSphereTopology::Box", EditConditionHides))
	int32 StepsZ = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sphere", meta = (PCG_Overridable))
	EGeometryScriptPrimitiveOriginMode Origin = EGeometryScriptPrimitiveOriginMode::Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Options", meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptPrimitiveOptions PrimitiveOptions;

protected:
	virtual UPCGCreatePrimitiveSettingsBase* CreatePrimitiveSettings(FPCGContext* InContext) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Creation")
class PCGUTILSDYNMESH_API UPCGCapsuleBuilderSettings : public UPCGPrimitiveBuilderProviderSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CapsuleBuilder"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capsule", meta = (PCG_Overridable, ClampMin = "0"))
	float Radius = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capsule", meta = (PCG_Overridable, ClampMin = "0"))
	float LineLength = 75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capsule", meta = (PCG_Overridable, ClampMin = "2"))
	int32 HemisphereSteps = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capsule", meta = (PCG_Overridable, ClampMin = "3"))
	int32 CircleSteps = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capsule", meta = (PCG_Overridable, ClampMin = "0"))
	int32 SegmentSteps = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capsule", meta = (PCG_Overridable))
	EGeometryScriptPrimitiveOriginMode Origin = EGeometryScriptPrimitiveOriginMode::Base;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Options", meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptPrimitiveOptions PrimitiveOptions;

protected:
	virtual UPCGCreatePrimitiveSettingsBase* CreatePrimitiveSettings(FPCGContext* InContext) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Creation")
class PCGUTILSDYNMESH_API UPCGCylinderBuilderSettings : public UPCGPrimitiveBuilderProviderSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CylinderBuilder"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cylinder", meta = (PCG_Overridable, ClampMin = "0"))
	float Radius = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cylinder", meta = (PCG_Overridable, ClampMin = "0"))
	float Height = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cylinder", meta = (PCG_Overridable, ClampMin = "3"))
	int32 RadialSteps = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cylinder", meta = (PCG_Overridable, ClampMin = "0"))
	int32 HeightSteps = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cylinder", meta = (PCG_Overridable))
	bool bCapped = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cylinder", meta = (PCG_Overridable))
	EGeometryScriptPrimitiveOriginMode Origin = EGeometryScriptPrimitiveOriginMode::Base;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Options", meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptPrimitiveOptions PrimitiveOptions;

protected:
	virtual UPCGCreatePrimitiveSettingsBase* CreatePrimitiveSettings(FPCGContext* InContext) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Creation")
class PCGUTILSDYNMESH_API UPCGConeBuilderSettings : public UPCGPrimitiveBuilderProviderSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("ConeBuilder"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cone", meta = (PCG_Overridable, ClampMin = "0"))
	float BaseRadius = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cone", meta = (PCG_Overridable, ClampMin = "0"))
	float TopRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cone", meta = (PCG_Overridable, ClampMin = "0"))
	float Height = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cone", meta = (PCG_Overridable, ClampMin = "3"))
	int32 RadialSteps = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cone", meta = (PCG_Overridable, ClampMin = "0"))
	int32 HeightSteps = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cone", meta = (PCG_Overridable))
	bool bCapped = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cone", meta = (PCG_Overridable))
	EGeometryScriptPrimitiveOriginMode Origin = EGeometryScriptPrimitiveOriginMode::Base;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Options", meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptPrimitiveOptions PrimitiveOptions;

protected:
	virtual UPCGCreatePrimitiveSettingsBase* CreatePrimitiveSettings(FPCGContext* InContext) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Creation")
class PCGUTILSDYNMESH_API UPCGTorusBuilderSettings : public UPCGPrimitiveBuilderProviderSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("TorusBuilder"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torus", meta = (PCG_Overridable, ClampMin = "0"))
	float MajorRadius = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torus", meta = (PCG_Overridable, ClampMin = "0"))
	float MinorRadius = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torus", meta = (PCG_Overridable, ClampMin = "3"))
	int32 MajorSteps = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torus", meta = (PCG_Overridable, ClampMin = "3"))
	int32 MinorSteps = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torus", meta = (PCG_Overridable))
	EGeometryScriptPrimitiveOriginMode Origin = EGeometryScriptPrimitiveOriginMode::Base;

	/** Controls revolve angle/direction; RevolveDegrees < 360 produces a partial torus arc. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torus", meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptRevolveOptions RevolveOptions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Options", meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptPrimitiveOptions PrimitiveOptions;

protected:
	virtual UPCGCreatePrimitiveSettingsBase* CreatePrimitiveSettings(FPCGContext* InContext) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Creation")
class PCGUTILSDYNMESH_API UPCGRectangleBuilderSettings : public UPCGPrimitiveBuilderProviderSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("RectangleBuilder"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rectangle", meta = (PCG_Overridable, ClampMin = "0"))
	float DimensionX = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rectangle", meta = (PCG_Overridable, ClampMin = "0"))
	float DimensionY = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rectangle", meta = (PCG_Overridable, ClampMin = "0"))
	int32 StepsWidth = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rectangle", meta = (PCG_Overridable, ClampMin = "0"))
	int32 StepsHeight = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Options", meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptPrimitiveOptions PrimitiveOptions;

protected:
	virtual UPCGCreatePrimitiveSettingsBase* CreatePrimitiveSettings(FPCGContext* InContext) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Creation")
class PCGUTILSDYNMESH_API UPCGRoundedRectangleBuilderSettings : public UPCGPrimitiveBuilderProviderSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("RoundedRectangleBuilder"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rounded Rectangle", meta = (PCG_Overridable, ClampMin = "0"))
	float DimensionX = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rounded Rectangle", meta = (PCG_Overridable, ClampMin = "0"))
	float DimensionY = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rounded Rectangle", meta = (PCG_Overridable, ClampMin = "0"))
	float CornerRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rounded Rectangle", meta = (PCG_Overridable, ClampMin = "0"))
	int32 StepsWidth = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rounded Rectangle", meta = (PCG_Overridable, ClampMin = "0"))
	int32 StepsHeight = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rounded Rectangle", meta = (PCG_Overridable, ClampMin = "1"))
	int32 StepsRound = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Options", meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptPrimitiveOptions PrimitiveOptions;

protected:
	virtual UPCGCreatePrimitiveSettingsBase* CreatePrimitiveSettings(FPCGContext* InContext) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Creation")
class PCGUTILSDYNMESH_API UPCGDiscBuilderSettings : public UPCGPrimitiveBuilderProviderSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DiscBuilder"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Disc", meta = (PCG_Overridable, ClampMin = "0"))
	float Radius = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Disc", meta = (PCG_Overridable, ClampMin = "3"))
	int32 AngleSteps = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Disc", meta = (PCG_Overridable, ClampMin = "0"))
	int32 SpokeSteps = 0;

	/** Degrees; a StartAngle/EndAngle range under a full 360 produces a partial disc / pie wedge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Disc", meta = (PCG_Overridable))
	float StartAngle = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Disc", meta = (PCG_Overridable))
	float EndAngle = 360.0f;

	/** Radius of a central hole; produces a ring/annulus when greater than zero. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Disc", meta = (PCG_Overridable, ClampMin = "0"))
	float HoleRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Options", meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptPrimitiveOptions PrimitiveOptions;

protected:
	virtual UPCGCreatePrimitiveSettingsBase* CreatePrimitiveSettings(FPCGContext* InContext) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Creation")
class PCGUTILSDYNMESH_API UPCGLinearStairsBuilderSettings : public UPCGPrimitiveBuilderProviderSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("LinearStairsBuilder"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Linear Stairs", meta = (PCG_Overridable, ClampMin = "0"))
	float StepWidth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Linear Stairs", meta = (PCG_Overridable, ClampMin = "0"))
	float StepHeight = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Linear Stairs", meta = (PCG_Overridable, ClampMin = "0"))
	float StepDepth = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Linear Stairs", meta = (PCG_Overridable, ClampMin = "1"))
	int32 NumSteps = 8;

	/** If true, each step is a floating slab instead of a solid stacked staircase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Linear Stairs", meta = (PCG_Overridable))
	bool bFloating = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Options", meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptPrimitiveOptions PrimitiveOptions;

protected:
	virtual UPCGCreatePrimitiveSettingsBase* CreatePrimitiveSettings(FPCGContext* InContext) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Creation")
class PCGUTILSDYNMESH_API UPCGCurvedStairsBuilderSettings : public UPCGPrimitiveBuilderProviderSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CurvedStairsBuilder"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Stairs", meta = (PCG_Overridable, ClampMin = "0"))
	float StepWidth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Stairs", meta = (PCG_Overridable, ClampMin = "0"))
	float StepHeight = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Stairs", meta = (PCG_Overridable, ClampMin = "0"))
	float InnerRadius = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Stairs", meta = (PCG_Overridable))
	float CurveAngle = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Stairs", meta = (PCG_Overridable, ClampMin = "1"))
	int32 NumSteps = 8;

	/** If true, each step is a floating slab instead of a solid stacked staircase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curved Stairs", meta = (PCG_Overridable))
	bool bFloating = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Options", meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptPrimitiveOptions PrimitiveOptions;

protected:
	virtual UPCGCreatePrimitiveSettingsBase* CreatePrimitiveSettings(FPCGContext* InContext) const override;
};
