// Copyright Max Harris

#pragma once

#include "Components/PrimitiveComponent.h"
#include "Data/PCGVolumeComponentData.h"
#include "Data/PCGVolumeShape.h"

#include "PCGVolumeComponent.generated.h"

class AVolume;

/**
 * A PCG volume that can sit on any actor: a box, sphere, cylinder, cone or capsule placed and sized by the
 * component's own transform and shape settings, read by Get PCG Volume Data as volume data or a DynMesh.
 *
 * The engine's brush cannot be used for this: UBrushComponent is not exported for subclassing, and its geometry
 * tools only edit Volume actors. On a Volume actor, Shape = Owner Volume reads that actor's brush instead, which
 * is what components added before the shapes existed are set to.
 *
 * Contributes its shape to a PCG Actor's automatic bounds box. Has no collision and draws only in the editor.
 */
UCLASS(ClassGroup="PCG", BlueprintType, Blueprintable, meta=(BlueprintSpawnableComponent),
	hidecategories=(Collision, Physics, Lighting, Rendering, LOD, TextureStreaming, Mobile, HLOD, Navigation,
		VirtualTexture, MaterialParameters, Activation, Cooking, AssetUserData))
class PCGUTILS_API UPCGVolumeComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UPCGVolumeComponent(const FObjectInitializer& ObjectInitializer);

	/** The shape this volume encloses, in the component's space. Owner Volume reads the owning Volume actor's brush. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Volume")
	EPCGVolumeShape Shape = EPCGVolumeShape::Box;

	/** Half the box's size along each axis. Box only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Volume",
		meta=(ClampMin="0", EditCondition="Shape == EPCGVolumeShape::Box", EditConditionHides))
	FVector BoxExtent = FVector(50.0);

	/** Sphere radius; the radius across a cylinder or capsule; a cone's base radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Volume",
		meta=(ClampMin="0", EditCondition="Shape == EPCGVolumeShape::Sphere || Shape == EPCGVolumeShape::Cylinder || Shape == EPCGVolumeShape::Cone || Shape == EPCGVolumeShape::Capsule", EditConditionHides))
	double Radius = 50.0;

	/** Half the height along Z. For a capsule it includes the rounded ends, as on a Capsule Component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Volume",
		meta=(ClampMin="0", EditCondition="Shape == EPCGVolumeShape::Cylinder || Shape == EPCGVolumeShape::Cone || Shape == EPCGVolumeShape::Capsule", EditConditionHides))
	double HalfHeight = 50.0;

	/** Segments around a round shape when it is drawn or made into a DynMesh. Point tests use the exact shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Volume",
		meta=(ClampMin="3", ClampMax="128", EditCondition="Shape == EPCGVolumeShape::Sphere || Shape == EPCGVolumeShape::Cylinder || Shape == EPCGVolumeShape::Cone || Shape == EPCGVolumeShape::Capsule", EditConditionHides))
	int32 Sides = 16;

	/** Values optionally written to @Data by Get PCG Volume Data, in either output mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG", meta=(ShowOnlyInnerProperties, SkipUCSModifiedProperties))
	FVolumeComponentData VolumeData;

	/** The engine Volume actor this component is on, which Owner Volume reads. Null on any other actor. */
	UFUNCTION(BlueprintPure, Category="PCG|Volume")
	AVolume* GetVolume() const;

	/** This volume's shape placed in the world. Not meaningful for Owner Volume. */
	UFUNCTION(BlueprintPure, Category="PCG|Volume")
	FPCGVolumeShapeDesc GetShapeDesc() const;

	/** Whether a world-space point is inside the volume, whichever shape it has. */
	UFUNCTION(BlueprintPure, Category="PCG|Volume")
	bool EncompassesPoint(const FVector& WorldPosition) const;

	//~ Begin USceneComponent / UPrimitiveComponent interface
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override { return nullptr; }
	//~ End interface

	virtual void PostLoad() override;
	virtual void OnComponentCreated() override;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG|Editor")
	bool bRegeneratePCGOnVolumeEdits = true;

	UPROPERTY(EditAnywhere, Category="PCG|Editor", meta=(DisplayName="Editor Selected Color"))
	FLinearColor EditorSelectedVolumeColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, Category="PCG|Editor", meta=(DisplayName="Editor Unselected Color"))
	FLinearColor EditorUnselectedVolumeColor = FLinearColor::Gray;

	UPROPERTY(EditAnywhere, Category="PCG|Editor")
	bool bDrawSelectedAsWireframe = false;

	UPROPERTY(EditAnywhere, Category="PCG|Editor")
	bool bDrawUnselectedAsWireframe = true;

	UPROPERTY(EditAnywhere, Category="PCG|Editor", meta=(DisplayName="Fill Opacity", ClampMin="0", ClampMax="1", UIMin="0", UIMax="1"))
	float EditorFillOpacity = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG|Editor", meta=(DisplayName="Use Volume Color As Fill Color"))
	bool bUseVolumeColorAsEditorColor = true;
#endif

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditComponentMove(bool bFinished) override;

private:
	void TriggerRegeneratePCGOnVolumeEdits();
#endif

private:
	/** False on components saved before shapes existed; PostLoad sets those on a Volume actor to Owner Volume. */
	UPROPERTY()
	bool bShapeMigrated = false;
};
