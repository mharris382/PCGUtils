#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGContext.h"
#include "Elements/PCGDynamicMeshBaseElement.h"
#include "Materials/MaterialInterface.h"
#include "PCGAssignMaterialToDynMesh.generated.h"

/**
 * PCGAssignMaterialToDynMesh
 *
 * Assigns material slots to a dynamic mesh based on per-triangle face normals.
 *
 * Faces within SlopeThresholdDegrees of world up (0,0,1) → slot 0 (FlatGreenMaterial)
 * All other faces                                        → slot 1 (SlopedGreenMaterial)
 *
 * Material definitions sourced from a UCourseGenDataAsset assigned in settings.
 * Run after boolean cuts, before bake — output mesh has correct material IDs
 * for both visual rendering and physics material assignment.
 *
 * Lives in CourseGenCore. Uses CourseGen.Surface.Green pin convention.
 */

 // ── Context ───────────────────────────────────────────────────────────────────

struct FPCGAssignMaterialToDynMeshContext : public FPCGContext
{
	// No async asset loading. Exists to match BaseElement pattern.
};

// ── Settings ──────────────────────────────────────────────────────────────────

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCGUTILS_API UPCGAssignMaterialToDynMeshSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override;
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif



protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

	virtual FPCGElementPtr CreateElement() const override;

public:
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material", meta=(PCG_Overridable))
	TSoftObjectPtr<UMaterialInterface> Material;

	///**
	// * Faces within this angle of world up are classified as flat (slot 0).
	// * Default is intentionally tiny — only near-perfectly horizontal faces get flat material.
	// * Increase if ramps are being misclassified as sloped.
	// */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Materials",
	//	meta = (PCG_Overridable, ClampMin = "0.0", ClampMax = "89.0", UIMin = "0.0", UIMax = "45.0"))
	//float SlopeThresholdDegrees = 0.5f;
};

// ── Element ───────────────────────────────────────────────────────────────────

class PCGUTILS_API FPCGAssignMaterialToDynMeshElement : public IPCGDynamicMeshBaseElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override;

protected:
	virtual FPCGContext* CreateContext() override;
	virtual bool PrepareDataInternal(FPCGContext* InContext) const override;
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};