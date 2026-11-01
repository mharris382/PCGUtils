// Copyright Max Harris

#pragma once

#include "Components/SceneComponent.h"
#include "Data/PCGVolumeComponentData.h"

#include "PCGVolumeComponent.generated.h"

class AVolume;

/**
 * Adds PCG metadata and editor display settings to a standard UE volume.
 * Add to a Volume actor and author its shape with the normal brush settings/tools.
 * Geometry and placement come from the owner's brush, not this component's transform.
 */
UCLASS(ClassGroup="PCG", BlueprintType, Blueprintable, hidecategories=(Transform), meta=(BlueprintSpawnableComponent))
class PCGUTILS_API UPCGVolumeComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPCGVolumeComponent(const FObjectInitializer& ObjectInitializer);

	/** Values optionally written to @Data by Get PCG Volume Data, in either output mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG", meta=(ShowOnlyInnerProperties, SkipUCSModifiedProperties))
	FVolumeComponentData VolumeData;

	/** The standard UE volume that supplies this component's geometry. Null on non-volume actors. */
	UFUNCTION(BlueprintPure, Category="PCG|Volume")
	AVolume* GetVolume() const;

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
};
