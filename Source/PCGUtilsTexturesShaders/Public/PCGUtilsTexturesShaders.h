// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FRHICommandListImmediate;
class FRHITexture;

/** Maps /Plugin/PCGUtils to the plugin's Shaders folder so the texture compute shaders can be found. */
class FPCGUtilsTexturesShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override {}
};

/** What the greyscale compute pass does to a texture. Mirrors FPCGUtilsImageSampleOptions, resolved to numbers. */
struct FPCGUtilsGreyscaleGPUParams
{
	/** Output size in pixels. The source is resampled bilinearly to it. */
	FIntPoint OutputSize = FIntPoint(256, 256);

	/** Dot-product weights applied to the sampled RGBA: one-hot for a channel, luminance weights for Luminance. */
	FVector4f ChannelWeights = FVector4f(0.2126f, 0.7152f, 0.0722f, 0.0f);

	/** Take the max of RGB instead of the weighted sum. */
	bool bMaxOfRGB = false;

	/** Re-encode the hardware-decoded linear value to sRGB, so an sRGB texture reads as it was authored. */
	bool bEncodeSRGB = false;

	/** Levels: the value at BlackPoint maps to 0 and at WhitePoint to 1, clamped. */
	float BlackPoint = 0.0f;
	float WhitePoint = 1.0f;
	bool bInvert = false;

	/** Separable Gaussian blur radius in output pixels; 0 skips the blur passes. */
	float BlurRadius = 0.0f;
};

namespace PCGUtilsTexturesShaders
{
	/** True when a GPU is available to run the greyscale pass (false under -nullrhi). */
	PCGUTILSTEXTURESSHADERS_API bool IsGPUSamplingAvailable();

	/**
	 * Render thread: resamples Source to Params.OutputSize, reduces it to one channel, applies levels, invert and
	 * blur on the GPU, and reads the result back into OutValues (row-major, OutputSize.X * OutputSize.Y floats).
	 * Blocks the render thread until the GPU has finished; intended for editor-time rebuilds, which are rare.
	 *
	 * @return false when Source is null or the output size is empty.
	 */
	PCGUTILSTEXTURESSHADERS_API bool ReadGreyscale_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		FRHITexture* Source,
		const FPCGUtilsGreyscaleGPUParams& Params,
		TArray<float>& OutValues);
}
