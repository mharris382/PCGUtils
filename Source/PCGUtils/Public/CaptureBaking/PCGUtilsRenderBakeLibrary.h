// Copyright Max Harris
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/TextureRenderTarget2D.h"        // ETextureRenderTargetFormat
#include "Components/SceneCaptureComponent2D.h"  // ESceneCaptureSource
#include "Materials/MaterialInterface.h"

#include "PCGUtilsRenderBakeLibrary.generated.h"

class UTexture;

// ─────────────────────────────────────────────────────────────────────────────
// FPCGUtilsTopDownCaptureRequest
// ─────────────────────────────────────────────────────────────────────────────

USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGUtilsTopDownCaptureRequest
{
	GENERATED_BODY()

	/** World-space AABB that defines the capture region. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Capture")
	FBox WorldBounds = FBox(ForceInit);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Capture", meta = (ClampMin = "1"))
	int32 ResolutionX = 1024;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Capture", meta = (ClampMin = "1"))
	int32 ResolutionY = 1024;

	/** Actors to render when bUseShowOnlyList is true. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Capture|ShowOnly")
	TArray<TObjectPtr<AActor>> ShowOnlyActors;

	/** Individual primitive components to render when bUseShowOnlyList is true. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Capture|ShowOnly")
	TArray<TObjectPtr<UPrimitiveComponent>> ShowOnlyComponents;

	/**
	 * TODO: USceneCaptureComponent2D does not expose a direct per-primitive material override in UE5.7.
	 * To support this, temporarily swap materials on captured actors/components before CaptureScene(),
	 * then restore them. Reserved for a future pass — currently ignored.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Capture")
	TObjectPtr<UMaterialInterface> OverrideMaterial = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Capture")
	FLinearColor ClearColor = FLinearColor::Black;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Capture")
	TEnumAsByte<ETextureRenderTargetFormat> RenderTargetFormat = RTF_RGBA8;

	/** Restrict capture to ShowOnlyActors / ShowOnlyComponents. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Capture|ShowOnly")
	bool bUseShowOnlyList = false;

	/** Distance above WorldBounds.Max.Z where the capture camera is placed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Capture")
	float CaptureHeightOffset = 100.f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Capture")
	TEnumAsByte<ESceneCaptureSource> CaptureSource = SCS_BaseColor;
};


// ─────────────────────────────────────────────────────────────────────────────
// FPCGUtilsTopDownCaptureResult
// ─────────────────────────────────────────────────────────────────────────────

USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGUtilsTopDownCaptureResult
{
	GENERATED_BODY()

	/** Transient render target containing the captured image. Caller must hold a UPROPERTY ref to prevent GC. */
	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	TObjectPtr<UTextureRenderTarget2D> RenderTarget = nullptr;

	/** Mirrors WorldBounds from the request — the world-space region that was captured. */
	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	FBox CaptureWorldBounds = FBox(ForceInit);

	/** World XY coordinate of the render target's (0,0) texel corner (== WorldBounds.Min XY). */
	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	FVector2D CaptureMinXY = FVector2D::ZeroVector;

	/** World-space XY size of the captured region (== WorldBounds size XY). */
	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	FVector2D CaptureSizeXY = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	FString ErrorMessage;
};


// ─────────────────────────────────────────────────────────────────────────────
// FPCGUtilsRenderTargetProcessRequest
// ─────────────────────────────────────────────────────────────────────────────

USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGUtilsRenderTargetProcessRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Process")
	TObjectPtr<UTextureRenderTarget2D> InputRenderTarget = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Process")
	TObjectPtr<UTextureRenderTarget2D> OutputRenderTarget = nullptr;

	/** Material to draw. Should read the texture parameter named InputTextureParameterName. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Process")
	TObjectPtr<UMaterialInterface> ProcessingMaterial = nullptr;

	/** Name of the texture parameter on ProcessingMaterial that receives InputRenderTarget. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Process")
	FName InputTextureParameterName = TEXT("InputTexture");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Process")
	TMap<FName, float> ScalarParameters;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Process")
	TMap<FName, FLinearColor> VectorParameters;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Process")
	TMap<FName, TObjectPtr<UTexture>> TextureParameters;
};


// ─────────────────────────────────────────────────────────────────────────────
// FPCGUtilsRenderTargetProcessResult
// ─────────────────────────────────────────────────────────────────────────────

USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGUtilsRenderTargetProcessResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Process")
	TObjectPtr<UTextureRenderTarget2D> OutputRenderTarget = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Process")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "Process")
	FString ErrorMessage;
};


// ─────────────────────────────────────────────────────────────────────────────
// UPCGUtilsRenderBakeLibrary
// ─────────────────────────────────────────────────────────────────────────────

UCLASS()
class PCGUTILS_API UPCGUtilsRenderBakeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Captures a top-down orthographic view of the specified world bounds into a transient render target.
	 * Spawns a temporary ASceneCapture2D, calls CaptureScene(), then destroys it.
	 * Must be called from the game thread.
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGUtils|Capture Baking", meta = (WorldContext = "WorldContextObject"))
	static FPCGUtilsTopDownCaptureResult CaptureTopDownToRenderTarget(
		UObject* WorldContextObject,
		const FPCGUtilsTopDownCaptureRequest& Request);

	/**
	 * Draws ProcessingMaterial into OutputRenderTarget with InputRenderTarget bound to the named
	 * texture parameter. Use this for post-capture processing (masking, blurring, remapping, etc.).
	 */
 	UFUNCTION(BlueprintCallable, Category = "PCGUtils|Capture Baking", meta = (WorldContext = "WorldContextObject"))
	static FPCGUtilsRenderTargetProcessResult ProcessRenderTargetWithMaterial(
		UObject* WorldContextObject,
		const FPCGUtilsRenderTargetProcessRequest& Request);
};
