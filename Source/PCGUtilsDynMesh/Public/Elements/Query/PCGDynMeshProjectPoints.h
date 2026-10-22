// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGUtilsDynMeshNodeColor.h"
#include "PCGDynMeshProjectPoints.generated.h"

namespace PCGDynMeshProjectPointsConstants
{
	inline const FName PointsInputPin = TEXT("In");
	inline const FName MeshInputPin = TEXT("Mesh");
	inline const FName PointsOutputPin = TEXT("Out");
}

/** Where each point's projection direction comes from. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshProjectDirectionSource : uint8
{
	/** Every point uses the Direction setting. */
	Constant,

	/** Each point reads its own direction from Direction Attribute. */
	Attribute
};

/** Which normal a hit reports, and which one Align Rotation turns the point towards. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshProjectNormalSource : uint8
{
	/**
	 * The mesh's shading normal at the hit, blended across the triangle's corners: the normal overlay when the mesh
	 * has one, otherwise per-vertex normals. Smooth across soft edges. Falls back to the face normal when the mesh
	 * has neither.
	 */
	Interpolated,

	/** The flat geometric normal of the triangle that was hit. Faceted; exact on hard-edged geometry. */
	Face
};

/** Point axes Project Position may overwrite. Axes are in output space: world space when World Space is on. */
UENUM(meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true"))
enum class EPCGUtilsDynMeshProjectAxes : uint8
{
	None = 0 UMETA(Hidden),
	X = 1 << 0,
	Y = 1 << 1,
	Z = 1 << 2,
};
ENUM_CLASS_FLAGS(EPCGUtilsDynMeshProjectAxes)

/** The point axis Align Rotation turns towards the hit normal. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshProjectAlignAxis : uint8
{
	X, Y, Z,
	NegativeX UMETA(DisplayName="-X"),
	NegativeY UMETA(DisplayName="-Y"),
	NegativeZ UMETA(DisplayName="-Z")
};

/**
 * Casts a ray from every point along a direction, constant or per-point, and moves the point onto the first DynMesh
 * surface it hits. Optionally turns the point to the surface and writes what the hit found: distance, location,
 * normal, triangle, barycentrics, UV, vertex color, PolyGroup, material and which mesh input was hit.
 *
 * The output is the input points, moved: every attribute, tag, point property and the target actor carry through,
 * and the hit information is added alongside. Points whose ray hits nothing are left where they were (or pruned).
 *
 * Several mesh inputs are treated as one scene: each ray takes the nearest hit across all of them. A Selection or a
 * connected Selector restricts the surface to the selected triangles.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh",
	meta=(Keywords="DynMesh mesh project projection raycast ray trace snap points surface sample hit selection"))
class PCGUTILSDYNMESH_API UPCGDynMeshProjectPointsSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
	UPCGDynMeshProjectPointsSettings();

	/** Rays hit triangles, so any incoming vertex/edge selection converts to its incident faces. */
	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const override
	{
		OutElementType = UE::Geometry::EGeometryElementType::Face;
		return true;
	}

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshProjectPoints"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsDynMeshNodeColor::Title; }
#endif

	// --- Space ---

	/**
	 * Treat the points, the direction and Max Distance as world space and convert through each DynMesh's target
	 * actor transform. Disable when the points are already in the mesh's own coordinate space.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Space", meta=(PCG_Overridable))
	bool bWorldSpace = true;

	// --- Projection ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Projection", meta=(PCG_Overridable))
	EPCGUtilsDynMeshProjectDirectionSource DirectionSource = EPCGUtilsDynMeshProjectDirectionSource::Constant;

	/** Direction every point is projected along. Need not be unit length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Projection",
		meta=(PCG_Overridable, EditCondition="DirectionSource == EPCGUtilsDynMeshProjectDirectionSource::Constant", EditConditionHides))
	FVector Direction = FVector(0.0, 0.0, -1.0);

	/**
	 * Required when Direction Source is Attribute: the per-point Vector to project along. Need not be unit length;
	 * a zero-length direction makes that point miss. Any attribute or point property readable as a Vector works,
	 * e.g. $Rotation.Up for each point's own up axis.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Projection",
		meta=(PCG_Overridable, EditCondition="DirectionSource == EPCGUtilsDynMeshProjectDirectionSource::Attribute", EditConditionHides))
	FPCGAttributePropertyInputSelector DirectionAttribute;

	/** Rotate the direction by each point's own rotation first, so (0,0,-1) means "down the point's own Z". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Projection", meta=(PCG_Overridable))
	bool bDirectionInPointSpace = false;

	/** Flip the direction. Handy with an attribute that points away from the surface, such as $Rotation.Up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Projection", meta=(PCG_Overridable))
	bool bInvertDirection = false;

	/** Farthest a point may travel to reach the surface. 0 means unlimited. Measured in the points' space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Projection", meta=(PCG_Overridable, ClampMin="0", UIMin="0"))
	double MaxDistance = 0.0;

	/**
	 * Also cast backwards, against the direction, and take whichever hit is nearer. Finds the surface for a point that
	 * starts below it. Backward hits report a negative distance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Projection", meta=(PCG_Overridable))
	bool bBidirectional = false;

	/** Ignore triangles the ray reaches from behind, so points only land on surfaces facing them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Projection", meta=(PCG_Overridable))
	bool bIgnoreBackFaces = false;

	/** Which normal the Hit Normal attribute reports and Align Rotation turns towards. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Projection", meta=(PCG_Overridable))
	EPCGUtilsDynMeshProjectNormalSource NormalSource = EPCGUtilsDynMeshProjectNormalSource::Interpolated;

	// --- Apply ---

	/** Move hit points onto the surface, on the axes Position Axes allows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Apply", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bProjectPosition = true;

	/** Which position components take the hit's. Clear X and Y to only drop points vertically onto the surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Apply",
		meta=(PCG_Overridable, EditCondition="bProjectPosition", Bitmask, BitmaskEnum="/Script/PCGUtilsDynMesh.EPCGUtilsDynMeshProjectAxes"))
	uint8 PositionAxes = static_cast<uint8>(EPCGUtilsDynMeshProjectAxes::X | EPCGUtilsDynMeshProjectAxes::Y | EPCGUtilsDynMeshProjectAxes::Z);

	/** Lift the projected position off the surface along the hit normal. Negative sinks it in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Apply", meta=(PCG_Overridable, EditCondition="bProjectPosition"))
	double SurfaceOffset = 0.0;

	/**
	 * Turn hit points so Align Axis follows the hit normal. The turn is the smallest one that does it, so the point
	 * keeps its heading around the normal.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Apply", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bProjectRotation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Apply", meta=(PCG_Overridable, EditCondition="bProjectRotation"))
	EPCGUtilsDynMeshProjectAlignAxis AlignAxis = EPCGUtilsDynMeshProjectAlignAxis::Z;

	/** Remove points whose ray hit nothing, instead of passing them through unmoved. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Apply", meta=(PCG_Overridable))
	bool bPruneMisses = false;

	// --- Attributes. Each is an optional point attribute behind its own flag; nothing is written for one that is off. ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable))
	bool bOutputSuccess = false;

	/** Optional output, Bool: true where the point's ray hit the mesh. Written for every point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputSuccess", EditConditionHides))
	FName SuccessAttributeName = TEXT("ProjectionHit");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable))
	bool bOutputDistance = false;

	/** Optional output, Double: how far the point travelled to the hit, negative for a backward hit. 0 on a miss. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputDistance", EditConditionHides))
	FName DistanceAttributeName = TEXT("ProjectionDistance");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable))
	bool bOutputHitLocation = false;

	/** Optional output, Vector: where the ray hit, without Surface Offset, in the points' space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputHitLocation", EditConditionHides))
	FName HitLocationAttributeName = TEXT("HitLocation");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable))
	bool bOutputHitNormal = false;

	/** Optional output, Vector: the unit surface normal at the hit, per Normal Source, in the points' space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputHitNormal", EditConditionHides))
	FName HitNormalAttributeName = TEXT("HitNormal");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable))
	bool bOutputBackFace = false;

	/** Optional output, Bool: true where the ray reached the hit triangle from behind, e.g. a point inside a closed mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputBackFace", EditConditionHides))
	FName BackFaceAttributeName = TEXT("HitBackFace");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable))
	bool bOutputTriangleId = false;

	/** Optional output, Int32: the source DynMesh's triangle ID that was hit (the source mesh's, even through a Selection). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputTriangleId", EditConditionHides))
	FName TriangleIdAttributeName = TEXT("TriangleId");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable))
	bool bOutputBarycentric = false;

	/** Optional output, Vector: the hit's barycentric weights on the triangle's three corners, in corner order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputBarycentric", EditConditionHides))
	FName BarycentricAttributeName = TEXT("Barycentric");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable))
	bool bOutputUV = false;

	/** Optional output, Vector2: the UV at the hit in UV Channel. Not written where the mesh has no such channel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputUV", EditConditionHides))
	FName UVAttributeName = TEXT("UV");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputUV", EditConditionHides, ClampMin="0", ClampMax="7"))
	int32 UVChannel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable))
	bool bOutputColor = false;

	/** Optional output, Vector4: the mesh's vertex color at the hit, blended across the triangle. White where it has none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputColor", EditConditionHides))
	FName ColorAttributeName = TEXT("HitColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable))
	bool bOutputPolygroup = false;

	/** Optional output, Int32: the hit triangle's PolyGroup ID in PolyGroup Layer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputPolygroup", EditConditionHides))
	FName PolygroupAttributeName = TEXT("PolygroupId");

	/** The named PolyGroup layer to read. None reads the mesh's default PolyGroups. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputPolygroup", EditConditionHides))
	FName PolygroupLayer = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable))
	bool bOutputMaterialId = false;

	/** Optional output, Int32: the hit triangle's material ID. Not written where the mesh has no material IDs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputMaterialId", EditConditionHides))
	FName MaterialIdAttributeName = TEXT("MaterialId");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable))
	bool bOutputMaterial = false;

	/** Optional output, Soft Object Path: the material in the DynMesh's material slot for the hit triangle's material ID. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputMaterial", EditConditionHides))
	FName MaterialAttributeName = TEXT("Material");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable))
	bool bOutputMeshIndex = false;

	/** Optional output, Int32: which input on the Mesh pin was hit, counting from 0. -1 on a miss. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable, EditCondition="bOutputMeshIndex", EditConditionHides))
	FName MeshIndexAttributeName = TEXT("MeshIndex");

protected:
	virtual FName GetMainInputPinLabel() const override { return PCGDynMeshProjectPointsConstants::MeshInputPin; }
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshProjectPointsElement : public FPCGUtilsDynMeshProcessBaseElement
{
public:
	/** Resolving PCG data and the DynMesh target actor transform is main-thread only, as in every PCGUtilsDynMesh element. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
