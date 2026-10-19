// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

#include "PCGUtilsGreyscaleImage.generated.h"

class UTexture;

/** Which part of a texture becomes the greyscale value. */
UENUM(BlueprintType)
enum class EPCGUtilsImageChannel : uint8
{
	/** Rec. 709 luminance of RGB. */
	Luminance,
	Red,
	Green,
	Blue,
	Alpha,
	/** The brightest of R, G and B. */
	MaxRGB UMETA(DisplayName="Max RGB")
};

/** How an sRGB texture's values are read. Linear textures read the same either way. */
UENUM(BlueprintType)
enum class EPCGUtilsImageValueSpace : uint8
{
	/** The values as they were painted: 0.5 is the mid-grey the artist picked. Right for masks and thresholds. */
	Authored,
	/** The linear values the renderer shades with. */
	Linear
};

/** Where the pixels are read from. */
UENUM(BlueprintType)
enum class EPCGUtilsImageReadSource : uint8
{
	/** Texture source data in the editor, when there is any; the GPU otherwise (render targets, cooked builds). */
	Auto,
	/** The texture's uncompressed source data, on the CPU. Editor only; exact, never compressed. */
	SourceData UMETA(DisplayName="Source Data (CPU)"),
	/** The texture's GPU resource, resampled, reduced and blurred by compute shaders. Works for render targets. */
	GPU UMETA(DisplayName="GPU")
};

/** How a texture is reduced to one value per pixel before it is processed. Shared by every texture consumer. */
USTRUCT(BlueprintType)
struct PCGUTILSTEXTURES_API FPCGUtilsImageSampleOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Image")
	EPCGUtilsImageChannel Channel = EPCGUtilsImageChannel::Luminance;

	/** The longest side is resampled down to this many pixels, keeping the aspect ratio. 0 keeps the native size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Image", meta=(ClampMin="0", UIMin="0", UIMax="4096"))
	int32 MaxResolution = 1024;

	/** Levels: this value maps to 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Image", meta=(ClampMin="0", ClampMax="1"))
	float BlackPoint = 0.0f;

	/** Levels: this value maps to 1. Must be greater than Black Point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Image", meta=(ClampMin="0", ClampMax="1"))
	float WhitePoint = 1.0f;

	/** Flip the values after levels, so dark areas become the shapes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Image")
	bool bInvert = false;

	/** Gaussian blur radius, in pixels of the resampled image, applied last. Softens noise and staircase edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Image", meta=(ClampMin="0", UIMin="0", UIMax="16"))
	float BlurRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Image", AdvancedDisplay)
	EPCGUtilsImageValueSpace ValueSpace = EPCGUtilsImageValueSpace::Authored;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Image", AdvancedDisplay)
	EPCGUtilsImageReadSource ReadSource = EPCGUtilsImageReadSource::Auto;
};

/** One float per pixel, row-major, top row first, normally in [0, 1]. */
USTRUCT(BlueprintType)
struct PCGUTILSTEXTURES_API FPCGUtilsGreyscaleImage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Image")
	int32 Width = 0;

	UPROPERTY(BlueprintReadOnly, Category="Image")
	int32 Height = 0;

	UPROPERTY()
	TArray<float> Values;

	void Init(int32 InWidth, int32 InHeight, float Fill = 0.0f);
	bool IsValid() const { return Width > 0 && Height > 0 && Values.Num() == Width * Height; }
	float At(int32 X, int32 Y) const { return Values[Y * Width + X]; }
	float& At(int32 X, int32 Y) { return Values[Y * Width + X]; }

	/** The value at (X, Y), or OutsideValue past the edge. */
	float AtOrDefault(int32 X, int32 Y, float OutsideValue) const
	{
		return X >= 0 && Y >= 0 && X < Width && Y < Height ? Values[Y * Width + X] : OutsideValue;
	}
};

namespace PCGUtilsImageSampling
{
	/**
	 * Reduces a texture to a greyscale image: resample to MaxResolution, pick the channel, levels, invert, blur.
	 * The GPU path does the same work in compute shaders; the CPU path reads the texture's source data. Either way
	 * the result matches up to compression in the GPU resource. Game thread only.
	 *
	 * @return false, with the exact reason in OutError, when the texture cannot be read.
	 */
	PCGUTILSTEXTURES_API bool SampleTexture(
		UTexture* Texture, const FPCGUtilsImageSampleOptions& Options, FPCGUtilsGreyscaleImage& OutImage, FText& OutError);

	/** Applies the resample, channel, levels, invert and blur steps to an RGBA image on the CPU. */
	PCGUTILSTEXTURES_API void ProcessLinearColors(
		TConstArrayView<FLinearColor> Colors, int32 Width, int32 Height,
		const FPCGUtilsImageSampleOptions& Options, FPCGUtilsGreyscaleImage& OutImage);

	/** Separable Gaussian blur, sigma = Radius / 2 out to 3 sigma: the kernel the GPU path uses too. */
	PCGUTILSTEXTURES_API void Blur(FPCGUtilsGreyscaleImage& Image, float Radius);

	/** The size an image of SourceSize is resampled to under MaxResolution. */
	PCGUTILSTEXTURES_API FIntPoint ResolveOutputSize(FIntPoint SourceSize, int32 MaxResolution);
}
