// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

#include "PCGUtilsFractureNoise.generated.h"

struct FNoiseSettings;
class FArchiveCrc32;

/**
 * Surface noise shared by every fracture operation, matching the Noise section of Unreal's Fracture Mode.
 *
 * Always convert through `ApplyTo()` rather than filling an FNoiseSettings by hand - see the comment there for
 * why the disabled case is not simply "amplitude zero".
 */
USTRUCT(BlueprintType)
struct PCGUTILSFRACTURE_API FPCGFractureNoiseSettings
{
	GENERATED_BODY()

	/** Displacement magnitude of the fracture surfaces, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Noise", meta=(ClampMin="0.0", Units="cm"))
	float Amplitude = 2.0f;

	/** Period of the noise. Smaller values give a smoother pattern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Noise", meta=(ClampMin="0.00001"))
	float Frequency = 0.1f;

	/** Amplitude scale applied at each octave after the first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Noise", meta=(ClampMin="0.0"))
	float Persistence = 0.5f;

	/** Frequency scale applied at each octave after the first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Noise", meta=(ClampMin="0.0"))
	float Lacunarity = 2.0f;

	/** 1-2 reads as gentle rolling hills; above 4 as craggy rock. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Noise", meta=(ClampMin="0", UIMax="8"))
	int32 OctaveNumber = 4;

	/**
	 * Target edge length the cut surfaces are subdivided to before noise is applied.
	 *
	 * By far the most expensive setting: halving it roughly quadruples the triangle count of every cut face.
	 * On a 100cm object a resolution of 1 produces hundreds of thousands of triangles. Start high, come down.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Noise", meta=(ClampMin="0.01", Units="cm"))
	float SurfaceResolution = 10.0f;

	/**
	 * Fills an engine FNoiseSettings.
	 *
	 * When bEnabled is false this does NOT simply zero the amplitude. Every FFractureEngineFracturing entry
	 * point assigns FInternalSurfaceMaterials::NoiseSettings unconditionally, and PlanarCut selects between its
	 * cheap and expensive meshing paths on `NoiseSettings.IsSet()` rather than on the amplitude - so the cheap
	 * path is unreachable and every cut face gets remeshed to PointSpacing regardless of displacement. Since
	 * PointSpacing is only a target edge length fed to RemeshForNoise(SplitsOnly), passing one larger than any
	 * cut face suppresses the subdivision entirely. That is a ~500x difference in triangle count on a fractured
	 * box, so any new fracture operation must route its noise through here.
	 */
	void ApplyTo(FNoiseSettings& OutNoiseSettings, bool bEnabled) const;

	/** Folds the settings into a Crc, skipping the unused values when noise is disabled. */
	void AddToCrc(FArchiveCrc32& Ar, bool bEnabled) const;
};
