// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGGeometryCollectionData.h"
#include "Elements/PCGUtilsFractureElementBase.h"
#include "Engine/EngineTypes.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionSupportSampling.h"

#include "PCGGeometryCollectionProjectBones.generated.h"

namespace PCGGeometryCollectionProjectBonesConstants
{
	inline const FName CollectionInputPin = TEXT("GC");
	inline const FName TargetInputPin = TEXT("Target");
	inline const FName CollectionOutputPin = TEXT("GC");
}

/** Which bones a projection moves, and therefore what counts as one rigid unit. */
UENUM(BlueprintType)
enum class EPCGGeometryCollectionProjectionResolution : uint8
{
	/**
	 * Exactly the bones the Selection names, or every piece when nothing is connected.
	 *
	 * Select a cluster and it settles as one unit, keeping its pieces' relative placement. This is the
	 * setting the fidelity/performance trade-off actually lives on.
	 */
	Selection,

	/** Descend to the individual fracture pieces, so each one settles on its own. Right for rubble. */
	Pieces
};

/**
 * Settles Geometry Collection pieces against surrounding geometry by projecting them, with no simulation.
 *
 * The problem this solves is procedural ruins: fracture a wall, remove some of it, and the remaining fragments
 * keep the positions they had inside the intact solid - so they float above the terrain they are supposed to be
 * lying on. Running a Chaos simulation to fix that is slow, unpredictable and hard to author against. Tracing
 * each fragment down onto whatever is beneath it produces most of the visual benefit for a fraction of the cost,
 * and the result is deterministic.
 *
 * It moves bones and nothing else: no vertex changes, no bone is added, removed or reparented.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Geometry Collection GC Project Bones Drop Settle Ground Snap Trace Pieces GeometryCollection"))
class PCGUTILSFRACTURE_API UPCGGeometryCollectionProjectBonesSettings : public UPCGUtilsFractureElementBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GCProjectBones"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	// --- Projection -----------------------------------------------------------------------------------

	/**
	 * The direction pieces travel. Down by default.
	 *
	 * Expressed in whichever space is being traced: world space when tracing level collision, or the
	 * collection's own space when a Target mesh is connected. Normalised before use.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Projection", meta=(PCG_Overridable))
	FVector Direction = FVector(0.0, 0.0, -1.0);

	/** How far a piece may travel before the projection gives up and leaves it alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Projection", meta=(PCG_Overridable, ClampMin="0.0"))
	double MaximumDistance = 10000.0;

	/**
	 * How far back along the direction each trace starts.
	 *
	 * This is what lets a piece that already intersects the surface be pushed back out instead of ignored: the
	 * trace begins above the sample, so a contact behind it registers as a negative distance. Raise it if
	 * fragments start deeply buried.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Projection", AdvancedDisplay,
		meta=(PCG_Overridable, ClampMin="0.0"))
	double StartOffset = 10.0;

	/** Which bones move, and therefore what settles as one rigid unit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Projection", meta=(PCG_Overridable))
	EPCGGeometryCollectionProjectionResolution Resolution = EPCGGeometryCollectionProjectionResolution::Selection;

	/** How much of each piece's shape is taken into account. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Projection", meta=(PCG_Overridable))
	EPCGGeometryCollectionProjectionAccuracy Accuracy = EPCGGeometryCollectionProjectionAccuracy::Bounds;

	// --- Collision ------------------------------------------------------------------------------------
	// Only used when nothing is connected to the Target pin; a Target mesh is traced directly, with no
	// physics scene involved at all.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision", meta=(PCG_Overridable))
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_WorldStatic;

	/** Trace against render geometry rather than simplified collision. Accurate, and markedly slower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision", meta=(PCG_Overridable))
	bool bTraceComplex = false;

	/**
	 * Exclude the PCG target actor from the traces.
	 *
	 * Leave this on. The target actor is where this graph's own output lands, so letting fragments hit it is
	 * how a projection ends up settling pieces onto a previous run of itself.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision", AdvancedDisplay, meta=(PCG_Overridable))
	bool bIgnoreTargetActor = true;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSFRACTURE_API FPCGGeometryCollectionProjectBonesElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
