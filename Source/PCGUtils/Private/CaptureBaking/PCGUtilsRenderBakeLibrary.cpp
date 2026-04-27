// Copyright Max Harris
#include "CaptureBaking/PCGUtilsRenderBakeLibrary.h"

#include "Engine/World.h"
#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace PCGUtils_RenderBake
{
	static FPCGUtilsTopDownCaptureResult MakeCaptureError(const TCHAR* Msg)
	{
		FPCGUtilsTopDownCaptureResult R;
		R.bSuccess = false;
		R.ErrorMessage = Msg;
		return R;
	}

	static FPCGUtilsRenderTargetProcessResult MakeProcessError(const TCHAR* Msg)
	{
		FPCGUtilsRenderTargetProcessResult R;
		R.bSuccess = false;
		R.ErrorMessage = Msg;
		return R;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// CaptureTopDownToRenderTarget
// ─────────────────────────────────────────────────────────────────────────────

FPCGUtilsTopDownCaptureResult UPCGUtilsRenderBakeLibrary::CaptureTopDownToRenderTarget(
	UObject* WorldContextObject,
	const FPCGUtilsTopDownCaptureRequest& Request)
{
	using namespace PCGUtils_RenderBake;

	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return MakeCaptureError(TEXT("CaptureTopDownToRenderTarget: invalid WorldContextObject or no world."));
	}

	if (!Request.WorldBounds.IsValid)
	{
		return MakeCaptureError(TEXT("CaptureTopDownToRenderTarget: WorldBounds is invalid or degenerate."));
	}

	if (Request.ResolutionX <= 0 || Request.ResolutionY <= 0)
	{
		return MakeCaptureError(TEXT("CaptureTopDownToRenderTarget: ResolutionX and ResolutionY must be > 0."));
	}

	// Create transient render target — caller must hold a UPROPERTY ref to prevent GC.
	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!RenderTarget)
	{
		return MakeCaptureError(TEXT("CaptureTopDownToRenderTarget: failed to create UTextureRenderTarget2D."));
	}
	RenderTarget->RenderTargetFormat = Request.RenderTargetFormat;
	RenderTarget->ClearColor = Request.ClearColor;
	RenderTarget->bAutoGenerateMips = false;
	RenderTarget->InitAutoFormat(Request.ResolutionX, Request.ResolutionY);
	RenderTarget->UpdateResourceImmediate(true);

	// OrthoWidth covers the larger XY extent so the full bounds always fits.
	const FVector BoundsSize   = Request.WorldBounds.GetSize();
	const float   OrthoWidth   = FMath::Max(BoundsSize.X, BoundsSize.Y);
	const FVector BoundsCenter = Request.WorldBounds.GetCenter();

	// Spawn a temporary capture actor (transient, not saved).
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags = RF_Transient;
	ASceneCapture2D* CaptureActor = World->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(), SpawnParams);
	if (!CaptureActor)
	{
		return MakeCaptureError(TEXT("CaptureTopDownToRenderTarget: failed to spawn ASceneCapture2D."));
	}

	// Place above the bounds center, rotated to look straight down.
	const FVector   CaptureLocation(BoundsCenter.X, BoundsCenter.Y, Request.WorldBounds.Max.Z + Request.CaptureHeightOffset);
	const FRotator  CaptureRotation(-90.f, 0.f, 0.f);
	CaptureActor->SetActorLocationAndRotation(CaptureLocation, CaptureRotation);

	USceneCaptureComponent2D* Comp = CaptureActor->GetCaptureComponent2D();

	Comp->ProjectionType      = ECameraProjectionMode::Orthographic;
	Comp->OrthoWidth          = OrthoWidth;
	Comp->CaptureSource       = static_cast<ESceneCaptureSource>(Request.CaptureSource.GetValue());
	Comp->TextureTarget       = RenderTarget;
	Comp->bCaptureEveryFrame  = false;
	Comp->bCaptureOnMovement  = false;

	if (Request.bUseShowOnlyList)
	{
		Comp->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;

		for (AActor* Actor : Request.ShowOnlyActors)
		{
			if (Actor)
			{
				Comp->ShowOnlyActors.Add(Actor);
			}
		}

		for (UPrimitiveComponent* PrimComp : Request.ShowOnlyComponents)
		{
			if (PrimComp)
			{
				// ShowOnlyComponents is TArray<TWeakObjectPtr<UPrimitiveComponent>> on USceneCaptureComponent.
				Comp->ShowOnlyComponents.Add(PrimComp);
			}
		}
	}

	// TODO: Override material is not implemented. USceneCaptureComponent2D has no direct per-primitive
	// material override API in UE5.7. Future implementation should temporarily swap mesh component
	// materials on ShowOnlyActors/ShowOnlyComponents before CaptureScene(), then restore them.

	Comp->CaptureScene();

	CaptureActor->Destroy();

	FPCGUtilsTopDownCaptureResult Result;
	Result.bSuccess           = true;
	Result.RenderTarget       = RenderTarget;
	Result.CaptureWorldBounds = Request.WorldBounds;
	Result.CaptureMinXY       = FVector2D(Request.WorldBounds.Min.X, Request.WorldBounds.Min.Y);
	Result.CaptureSizeXY      = FVector2D(BoundsSize.X, BoundsSize.Y);
	return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// ProcessRenderTargetWithMaterial
// ─────────────────────────────────────────────────────────────────────────────

FPCGUtilsRenderTargetProcessResult UPCGUtilsRenderBakeLibrary::ProcessRenderTargetWithMaterial(
	UObject* WorldContextObject,
	const FPCGUtilsRenderTargetProcessRequest& Request)
{
	using namespace PCGUtils_RenderBake;

	if (!Request.InputRenderTarget)
	{
		return MakeProcessError(TEXT("ProcessRenderTargetWithMaterial: InputRenderTarget is null."));
	}
	if (!Request.OutputRenderTarget)
	{
		return MakeProcessError(TEXT("ProcessRenderTargetWithMaterial: OutputRenderTarget is null."));
	}
	if (!Request.ProcessingMaterial)
	{
		return MakeProcessError(TEXT("ProcessRenderTargetWithMaterial: ProcessingMaterial is null."));
	}

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Request.ProcessingMaterial, GetTransientPackage());
	if (!MID)
	{
		return MakeProcessError(TEXT("ProcessRenderTargetWithMaterial: failed to create UMaterialInstanceDynamic."));
	}

	MID->SetTextureParameterValue(Request.InputTextureParameterName, Request.InputRenderTarget);

	for (const auto& Pair : Request.ScalarParameters)
	{
		MID->SetScalarParameterValue(Pair.Key, Pair.Value);
	}
	for (const auto& Pair : Request.VectorParameters)
	{
		MID->SetVectorParameterValue(Pair.Key, Pair.Value);
	}
	for (const auto& Pair : Request.TextureParameters)
	{
		if (Pair.Value)
		{
			MID->SetTextureParameterValue(Pair.Key, Pair.Value);
		}
	}

	UKismetRenderingLibrary::DrawMaterialToRenderTarget(WorldContextObject, Request.OutputRenderTarget, MID);

	FPCGUtilsRenderTargetProcessResult Result;
	Result.bSuccess        = true;
	Result.OutputRenderTarget = Request.OutputRenderTarget;
	return Result;
}
