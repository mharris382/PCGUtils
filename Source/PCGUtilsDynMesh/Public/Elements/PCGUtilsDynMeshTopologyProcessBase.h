// Copyright Max Harris

#pragma once

#include "Elements/PCGUtilsDynMeshProcessBase.h"

#include "PCGUtilsDynMeshTopologyProcessBase.generated.h"

namespace UE::Geometry { class FDynamicMesh3; }

namespace PCGUtilsDynMeshTopologyProcessConstants
{
	inline const FName ResultSelectorPin = TEXT("Result Selector");
}

/** The region retained after a face operation separates and stitches a cap to its original boundary. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshFaceRegionResult : uint8
{
	Faces UMETA(DisplayName="Cap / Inner Faces"),
	Border UMETA(DisplayName="Side / Border Faces"),
	FacesAndBorder UMETA(DisplayName="Cap and Border Faces")
};

/** Captured while the producing node executes; no mesh IDs or authoring UObjects are stored. */
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshTopologyResultOptions
{
	bool bAssignPolygroup = false;
	FName PolygroupName = NAME_None;
};

/**
 * Topology algorithms report their result triangles explicitly. The common executor converts that region to
 * Selection data and optionally assigns a fresh default PolyGroup and a named extended membership layer.
 * Result IDs may be old IDs whose geometry moved (an extruded cap), not just newly allocated triangles.
 */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshTopologyOperation : public FPCGUtilsDynMeshProcessOperation
{
public:
	FPCGUtilsDynMeshTopologyResultOptions ResultOptions;

	virtual bool Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const final override;

protected:
	/** Mesh is private to the invocation. Null Selection means whole mesh; an empty selection is skipped by the base. */
	virtual bool Apply(UE::Geometry::FDynamicMesh3& Mesh,
		const UE::Geometry::FGeometrySelection* Selection, TArray<int32>& OutResultTriangles) const = 0;
};

/**
 * A process that produces an identifiable region of topology. Inherits input resolution, domain conversion,
 * mesh ownership and Builder composition from the process base. Derived nodes only supply an algorithm.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshTopologyProcessBaseSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
	UPCGUtilsDynMeshTopologyProcessBaseSettings();

	/** Put the result faces in one fresh default PolyGroup, and record membership in a named extended layer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Result", meta=(PCG_Overridable, EditCondition="!bOutputResultSelector"))
	bool bAssignResultPolygroup = false;

	/** Emit a reusable Result Selector. Automatically enables result PolyGroup assignment, including on deferred Builders. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Result", meta=(PCG_Overridable))
	bool bOutputResultSelector = false;

	/**
	 * Name of the extended PolyGroup layer (1 = result, 0 = other faces). None generates a unique name from this
	 * node's path. Explicitly reusing a name replaces that region. Unlike numeric default groups, names can be
	 * referenced before a Builder runs. Select by PolyGroup can retrieve this named layer with group ID 1.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Result", meta=(PCG_Overridable,
		EditCondition="bAssignResultPolygroup || bOutputResultSelector"))
	FName ResultPolygroupName = NAME_None;

	FName GetResultPolygroupName() const;
	virtual bool SupportsDeferredBuilderProcessing() const final override { return true; }
	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext* Context) const final override;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshTopologyOperation> CreateTopologyOperation(FPCGContext* Context) const PURE_VIRTUAL(
		UPCGUtilsDynMeshTopologyProcessBaseSettings::CreateTopologyOperation, return nullptr;);
	virtual void AddProcessOperationToCrc(FArchiveCrc32& Ar) const override;
	virtual void EmitAdditionalOutputs(FPCGContext* Context) const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};
