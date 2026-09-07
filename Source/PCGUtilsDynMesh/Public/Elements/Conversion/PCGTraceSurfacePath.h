// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGTraceSurfacePath.generated.h"

namespace PCGTraceSurfacePathConstants
{
	inline const FName MeshInputPin = TEXT("Mesh");
	inline const FName SeedsInputPin = TEXT("Seeds");
	inline const FName PathsOutputPin = TEXT("Paths");
}

/** Which axis of a seed point's transform supplies the trace direction. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshTraceDirectionAxis : uint8
{
	Forward UMETA(DisplayName = "Forward (+X)"),
	Backward UMETA(DisplayName = "Backward (-X)"),
	Right UMETA(DisplayName = "Right (+Y)"),
	Left UMETA(DisplayName = "Left (-Y)"),
	Up UMETA(DisplayName = "Up (+Z)"),
	Down UMETA(DisplayName = "Down (-Z)")
};

/**
 * Traces a "straight" path across a DynMesh surface from each seed point.
 *
 * Each seed is projected onto the mesh, its direction is projected onto the surface there, and the path is
 * followed across triangles until it has covered Max Path Length or has run into a mesh boundary - whichever
 * comes first. Every seed produces its own independent output path.
 *
 * Intended for cracks, runoff streaks, vines, cables and other surface decoration that starts somewhere and
 * heads off in a direction, rather than joining two known endpoints (that is Route Path On DynMesh).
 *
 * A DynMesh Selection (or a connected Selector) restricts tracing to the selected triangle region, whose border
 * then acts as a boundary the trace terminates on.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh",
	meta=(Keywords="geodesic trace surface path crack vine streak straight"))
class PCGUTILSDYNMESH_API UPCGTraceSurfacePathSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
	/** Tracing runs across triangles, so any incoming vertex/edge selection converts to its incident faces. */
	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const override
	{
		OutElementType = UE::Geometry::EGeometryElementType::Face;
		return true;
	}

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("TraceSurfacePath"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/**
	 * Treat the incoming seed positions and directions as world space and emit the traced paths in world space,
	 * converting through the DynMesh's target actor transform. Disable when the seeds are already in the mesh's
	 * own coordinate space. Follows the PCGUtilsDynMesh world/local convention.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Space", meta=(PCG_Overridable))
	bool bWorldSpace = true;

	/**
	 * How far the trace may travel across the surface, in mesh-local units. The resulting path can be shorter
	 * when it reaches a mesh (or selection) boundary first.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trace", meta=(PCG_Overridable, ClampMin="0"))
	double MaxPathLength = 500.0;

	/**
	 * Farthest a seed may sit from the mesh and still be accepted, in mesh-local units. Zero means unlimited,
	 * matching the module's existing distance-limit convention. A seed beyond this distance produces no path and
	 * a graph warning, instead of snapping to a far-away part of the mesh.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trace", meta=(PCG_Overridable, ClampMin="0"))
	double MaxProjectionDistance = 0.0;

	/** Read the trace direction from a Vector attribute instead of the seed's transform axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Direction", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bUseDirectionAttribute = false;

	/**
	 * Vector-valued source for the trace direction, read per seed. It is interpreted in the same space as the
	 * seed positions (see World Space) and is projected onto the mesh surface at the seed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Direction",
		meta=(PCG_Overridable, EditCondition="bUseDirectionAttribute"))
	FPCGAttributePropertyInputSelector DirectionAttribute;

	/** Axis of the seed point's own transform used as the trace direction when no attribute is supplied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Direction",
		meta=(PCG_Overridable, EditCondition="!bUseDirectionAttribute", EditConditionHides))
	EPCGUtilsDynMeshTraceDirectionAxis DirectionAxis = EPCGUtilsDynMeshTraceDirectionAxis::Forward;

	/**
	 * Name of the @Data Bool closed-loop attribute written to every traced path. This is a hard contract with the
	 * rest of the PCGUtils path toolset - a downstream path consumer needs it to tell open from closed - so it is
	 * always written (as false; a surface trace never closes on itself). Nothing is written when this is None.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable))
	FName IsClosedAttributeName = TEXT("IsClosed");

	/** Record which seed produced each path, as a @Data Int32 attribute on the path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable))
	bool bOutputSeedIndex = false;

	/** Name of the @Data Int32 attribute holding the seed's index within its own input point data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes",
		meta=(PCG_Overridable, EditCondition="bOutputSeedIndex", EditConditionHides))
	FName SeedIndexAttributeName = TEXT("SeedIndex");

	/** Record whether the trace stopped at a mesh boundary rather than reaching Max Path Length, as a @Data Bool. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable))
	bool bOutputReachedBoundary = false;

	/** Name of the @Data Bool attribute set when the trace terminated on a mesh or selection boundary. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes",
		meta=(PCG_Overridable, EditCondition="bOutputReachedBoundary", EditConditionHides))
	FName ReachedBoundaryAttributeName = TEXT("ReachedBoundary");

	/** Steepness assigned to every generated path point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Points", meta=(PCG_Overridable, ClampMin="0", ClampMax="1"))
	float PointSteepness = 1.0f;

protected:
	virtual FName GetMainInputPinLabel() const override { return PCGTraceSurfacePathConstants::MeshInputPin; }
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGTraceSurfacePathElement : public FPCGUtilsDynMeshProcessBaseElement
{
public:
	/**
	 * The tracer itself only reads the mesh and is worker-thread safe, but resolving PCG data and the DynMesh
	 * target actor transform is not - matching every other PCGUtilsDynMesh element.
	 */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
