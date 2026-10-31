// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Data/PCGUtilsComponentData.h"
#include "Image/PCGUtilsGreyscaleImage.h"
#include "Interfaces/PCGBoundsProvider.h"
#include "Interfaces/PCGPathProvider.h"
#include "Vectorization/PCGUtilsVectorizationLibrary.h"
#include "Vectorization/PCGUtilsVectorizationTypes.h"

#include "ImagePathComponent.generated.h"

class UTexture;

/**
 * A PCG path provider whose paths are traced from an image: every shape boundary in the texture becomes one closed
 * path, holes included, laid on the component's XY plane.
 *
 * Tracing is cached. The paths are recomputed only when the texture's content, the sampling options, the algorithm
 * or that algorithm's options change; moving the component, resizing or re-pivoting the image, or editing path data
 * reuses the cached contours. The cache is saved with the component, so loading a level or a cooked build never
 * traces anything. A render target's content changes without the component knowing: call Rebuild Paths with Force.
 *
 * Image space to component space: the image lies flat in local XY, reading upright in the Top viewport (its right
 * edge toward +Y, its top edge toward +X), Image Width centimetres wide, with Pivot (in 0..1 image UV) at the origin.
 *
 * Get PCG Path Data emits one data per path; turn on its Path Index and Is Hole outputs to tell them apart.
 */
UCLASS(ClassGroup=(PCG), meta=(BlueprintSpawnableComponent, DisplayName="Image Path"))
class PCGUTILSTEXTURES_API UImagePathComponent : public USceneComponent, public IPCGPathProvider, public IPCGBoundsProvider
{
	GENERATED_BODY()

public:
	UImagePathComponent();

	/** The image to trace. Any 2D texture or render target; greyscale masks work best. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Image", meta=(DisplayPriority=0))
	TObjectPtr<UTexture> Texture;

	/** How the texture becomes one value per pixel before tracing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Image")
	FPCGUtilsImageSampleOptions Sampling;

	/** The tracing algorithm and its options. Only the selected algorithm's options affect the result. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Image|Vectorize", meta=(ShowOnlyInnerProperties))
	FPCGUtilsVectorizeOptions Vectorization;

	/** Width of the whole image in centimetres; its height follows the image's aspect ratio. Never retraces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Image|Placement", meta=(ClampMin="0.01", Units="cm"))
	float ImageWidth = 1000.0f;

	/** The image point placed at the component origin, in 0..1 UV: (0.5, 0.5) is the center, (0, 0) the top-left. Never retraces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Image|Placement")
	FVector2D Pivot = FVector2D(0.5, 0.5);

	/** Standardized path data used by PCG path getter elements, shared by every traced path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCG", meta=(ShowOnlyInnerProperties))
	FPathComponentData PathData;

	/** What the last trace produced, or why it failed. */
	UPROPERTY(VisibleAnywhere, Transient, Category="Image", meta=(DisplayPriority=1))
	FString Status;

	/**
	 * Retraces the image if anything that affects the paths changed since the last trace, or always with bForce.
	 * @return true when the cached paths are up to date afterwards.
	 */
	UFUNCTION(BlueprintCallable, Category="PCGUtils|Image Path")
	bool RebuildPaths(bool bForce = false);

	UFUNCTION(BlueprintPure, Category="PCGUtils|Image Path")
	int32 GetPathCount() const { return CachedPaths.Num(); }

	/** The traced paths in image pixel coordinates, as the vectorizer produced them. */
	UFUNCTION(BlueprintPure, Category="PCGUtils|Image Path")
	const TArray<FPCGUtilsVectorPath>& GetVectorPaths() const { return CachedPaths; }

	/** One path's points in component space. */
	UFUNCTION(BlueprintPure, Category="PCGUtils|Image Path")
	TArray<FVector> GetPathLocalPoints(int32 PathIndex) const;

	/** The traced image's size in pixels (after Max Resolution), or zero before the first trace. */
	UFUNCTION(BlueprintPure, Category="PCGUtils|Image Path")
	FIntPoint GetImageSize() const { return CachedImageSize; }

	/** The image rectangle's four corners in component space: top-left, top-right, bottom-right, bottom-left. */
	TArray<FVector, TFixedAllocator<4>> GetLocalImageCorners() const;

	/** Image pixel coordinates to component space, and the matching linear map for tangents. */
	FVector ImageToLocal(const FVector2D& Pixel) const;
	FVector ImageVectorToLocal(const FVector2D& PixelVector) const;

	/** How many traces this component has run; tests use it to prove a change did or did not retrace. */
	int32 GetTraceCount() const { return TraceCount; }

	// IPCGPathProvider
	virtual TArray<FPCGPathPoint> GetPCGPathPoints_Implementation(bool& bIsLocalSpace, bool& bIsLinearPath, bool& bIsClosedLoop) const override;
	virtual FPathComponentData GetPathData_Implementation() const override { return PathData; }
	virtual int32 GetPCGPathCount_Implementation() const override { return CachedPaths.Num(); }
	virtual TArray<FPCGPathPoint> GetPCGPathPointsAt_Implementation(
		int32 PathIndex, bool& bIsLocalSpace, bool& bIsLinearPath, bool& bIsClosedLoop, bool& bIsHole) const override;

	// IPCGBoundsProvider
	virtual bool GetPCGActorBoundingBox_Implementation(AActor* Actor, FBox& OutBounds) const override;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditComponentMove(bool bFinished) override;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category="Image", AdvancedDisplay)
	bool bAllowRegeneratePCGOnEdits = true;
#endif

private:
	/** A hash of everything that affects the traced paths and nothing else. */
	uint32 ComputeTraceKey() const;

	void RefreshPCG();

#if WITH_EDITOR
	void OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);
	FDelegateHandle ObjectChangedHandle;
#endif

	/** The traced paths, saved with the component so nothing is traced on load. */
	UPROPERTY()
	TArray<FPCGUtilsVectorPath> CachedPaths;

	UPROPERTY()
	FIntPoint CachedImageSize = FIntPoint::ZeroValue;

	UPROPERTY()
	uint32 CachedTraceKey = 0;

	UPROPERTY()
	bool bHasTrace = false;

	int32 TraceCount = 0;
};
