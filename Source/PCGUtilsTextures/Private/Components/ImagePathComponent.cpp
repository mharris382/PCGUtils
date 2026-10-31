// Copyright Max Harris

#include "Components/ImagePathComponent.h"

#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/Crc.h"
#include "PCGUtilsHelpers.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "ImagePathComponent"

UImagePathComponent::UImagePathComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

uint32 UImagePathComponent::ComputeTraceKey() const
{
	uint32 Key = Vectorization.GetActiveHash();

	FString SamplingText;
	FPCGUtilsImageSampleOptions::StaticStruct()->ExportText(SamplingText, &Sampling, nullptr, nullptr, PPF_None, nullptr);
	Key = FCrc::StrCrc32(*SamplingText, Key);

	if (Texture)
	{
		Key = FCrc::StrCrc32(*Texture->GetPathName(), Key);
		// The texture's content: its source id changes on every import or edit in the editor.
#if WITH_EDITORONLY_DATA
		const FGuid ContentId = Texture->Source.IsValid() ? Texture->Source.GetId() : Texture->GetLightingGuid();
#else
		const FGuid ContentId = Texture->GetLightingGuid();
#endif
		Key = FCrc::MemCrc32(&ContentId, sizeof(ContentId), Key);
	}
	return Key;
}

bool UImagePathComponent::RebuildPaths(bool bForce)
{
	const uint32 Key = ComputeTraceKey();
	if (!bForce && bHasTrace && Key == CachedTraceKey)
	{
		if (Status.IsEmpty())
		{
			Status = FString::Printf(TEXT("%d paths, cached, from %dx%d pixels."), CachedPaths.Num(), CachedImageSize.X, CachedImageSize.Y);
		}
		return true;
	}

	++TraceCount;
	if (!Texture)
	{
		CachedPaths.Reset();
		CachedImageSize = FIntPoint::ZeroValue;
		CachedTraceKey = Key;
		bHasTrace = true;
		Status = TEXT("No texture: no paths.");
		return true;
	}

	FPCGUtilsGreyscaleImage Image;
	FText Error;
	if (!PCGUtilsImageSampling::SampleTexture(Texture, Sampling, Image, Error))
	{
		// Keep the last good paths and leave the key stale, so the next change or Rebuild tries again.
		Status = FString::Printf(TEXT("Could not read the image: %s."), *Error.ToString());
		return false;
	}

	Vectorization.Vectorize(Image, CachedPaths);
	CachedImageSize = FIntPoint(Image.Width, Image.Height);
	CachedTraceKey = Key;
	bHasTrace = true;

	int32 Holes = 0;
	for (const FPCGUtilsVectorPath& Path : CachedPaths)
	{
		Holes += Path.bIsHole ? 1 : 0;
	}
	Status = FString::Printf(TEXT("%d paths (%d holes) traced from %dx%d pixels."),
		CachedPaths.Num(), Holes, Image.Width, Image.Height);
	return true;
}

FVector UImagePathComponent::ImageToLocal(const FVector2D& Pixel) const
{
	if (CachedImageSize.X <= 0 || CachedImageSize.Y <= 0)
	{
		return FVector::ZeroVector;
	}
	const FVector2D PivotPixel(Pivot.X * CachedImageSize.X, Pivot.Y * CachedImageSize.Y);
	return ImageVectorToLocal(Pixel - PivotPixel);
}

FVector UImagePathComponent::ImageVectorToLocal(const FVector2D& PixelVector) const
{
	if (CachedImageSize.X <= 0)
	{
		return FVector::ZeroVector;
	}
	// Upright in the Top viewport: image right is +Y, image up (-V) is +X. A rotation, so winding is kept.
	const double Scale = ImageWidth / CachedImageSize.X;
	return FVector(-PixelVector.Y * Scale, PixelVector.X * Scale, 0.0);
}

TArray<FVector, TFixedAllocator<4>> UImagePathComponent::GetLocalImageCorners() const
{
	const FVector2D Size(CachedImageSize);
	return {ImageToLocal(FVector2D(0, 0)), ImageToLocal(FVector2D(Size.X, 0)), ImageToLocal(Size), ImageToLocal(FVector2D(0, Size.Y))};
}

TArray<FVector> UImagePathComponent::GetPathLocalPoints(int32 PathIndex) const
{
	TArray<FVector> Result;
	if (CachedPaths.IsValidIndex(PathIndex))
	{
		for (const FVector2D& Point : CachedPaths[PathIndex].Points)
		{
			Result.Add(ImageToLocal(Point));
		}
	}
	return Result;
}

TArray<FPCGPathPoint> UImagePathComponent::GetPCGPathPointsAt_Implementation(
	int32 PathIndex, bool& bIsLocalSpace, bool& bIsLinearPath, bool& bIsClosedLoop, bool& bIsHole) const
{
	bIsLocalSpace = true;
	bIsLinearPath = true;
	bIsClosedLoop = true;
	bIsHole = false;
	TArray<FPCGPathPoint> Result;
	if (!CachedPaths.IsValidIndex(PathIndex))
	{
		return Result;
	}

	const FPCGUtilsVectorPath& Path = CachedPaths[PathIndex];
	const bool bTangents = Path.HasTangents();
	bIsLinearPath = !bTangents;
	bIsClosedLoop = Path.bClosed;
	bIsHole = Path.bIsHole;
	Result.Reserve(Path.Points.Num());
	for (int32 Index = 0; Index < Path.Points.Num(); ++Index)
	{
		FPCGPathPoint& Point = Result.Emplace_GetRef();
		Point.Transform = FTransform(ImageToLocal(Path.Points[Index]));
		Point.Density = PathData.GetPathDensity();
		Point.SetColor(PathData.GetPathColor());
		if (bTangents)
		{
			Point.ArriveTangent = ImageVectorToLocal(Path.ArriveTangents[Index]);
			Point.LeaveTangent = ImageVectorToLocal(Path.LeaveTangents[Index]);
		}
	}
	return Result;
}

TArray<FPCGPathPoint> UImagePathComponent::GetPCGPathPoints_Implementation(bool& bIsLocalSpace, bool& bIsLinearPath, bool& bIsClosedLoop) const
{
	// Single-path consumers see the first path: the largest outer boundary.
	bool bIsHole = false;
	return GetPCGPathPointsAt_Implementation(0, bIsLocalSpace, bIsLinearPath, bIsClosedLoop, bIsHole);
}

bool UImagePathComponent::GetPCGActorBoundingBox_Implementation(AActor* Actor, FBox& OutBounds) const
{
	OutBounds.Init();
	if (!IsValid(Actor) || CachedImageSize.X <= 0)
	{
		return false;
	}
	const FTransform ComponentTransform = GetComponentTransform();
	const FTransform ActorTransform = Actor->GetActorTransform();
	for (const FVector& Corner : GetLocalImageCorners())
	{
		OutBounds += ActorTransform.InverseTransformPosition(ComponentTransform.TransformPosition(Corner));
	}
	OutBounds = OutBounds.ExpandBy(1.0f);
	return OutBounds.IsValid != 0;
}

void UImagePathComponent::RefreshPCG()
{
#if WITH_EDITORONLY_DATA
	if (bAllowRegeneratePCGOnEdits)
	{
		UPCGUtilsHelpers::TryRefreshPCGGeneration(this, true);
	}
#endif
}

void UImagePathComponent::OnRegister()
{
	Super::OnRegister();
#if WITH_EDITOR
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		// Picks up a texture that changed while this level was closed; the key check makes this free otherwise.
		RebuildPaths(false);
		if (!ObjectChangedHandle.IsValid())
		{
			ObjectChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &UImagePathComponent::OnObjectPropertyChanged);
		}
	}
#endif
}

void UImagePathComponent::OnUnregister()
{
#if WITH_EDITOR
	if (ObjectChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectChangedHandle);
		ObjectChangedHandle.Reset();
	}
#endif
	Super::OnUnregister();
}

#if WITH_EDITOR
void UImagePathComponent::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	// A texture edit or reimport: retrace only if its content actually changed.
	if (Object && Object == Texture)
	{
		const uint32 PreviousKey = CachedTraceKey;
		if (RebuildPaths(false) && CachedTraceKey != PreviousKey)
		{
			RefreshPCG();
		}
	}
}

void UImagePathComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FName Member = PropertyChangedEvent.GetMemberPropertyName();
	if (Member == GET_MEMBER_NAME_CHECKED(UImagePathComponent, Texture)
		|| Member == GET_MEMBER_NAME_CHECKED(UImagePathComponent, Sampling)
		|| Member == GET_MEMBER_NAME_CHECKED(UImagePathComponent, Vectorization))
	{
		// Still key-checked: re-entering the same value, or editing an algorithm that is not selected, reuses the trace.
		RebuildPaths(false);
	}
	RefreshPCG();
}

void UImagePathComponent::PostEditComponentMove(bool bFinished)
{
	Super::PostEditComponentMove(bFinished);
	if (bFinished)
	{
		RefreshPCG();
	}
}
#endif

#undef LOCTEXT_NAMESPACE
