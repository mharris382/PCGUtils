// Copyright Max Harris

#include "Factories/PCGUtilsFractureNoise.h"

#include "PlanarCut.h"
#include "Serialization/ArchiveCrc32.h"

namespace
{
	/**
	 * A target edge length far larger than any plausible cut face, so PlanarCut's RemeshForNoise(SplitsOnly)
	 * splits nothing and the cut surfaces keep their base triangulation.
	 */
	constexpr float NoSubdivisionSpacing = 1.0e6f;
}

void FPCGFractureNoiseSettings::ApplyTo(FNoiseSettings& OutNoiseSettings, bool bEnabled) const
{
	OutNoiseSettings.Amplitude = bEnabled ? Amplitude : 0.0f;
	OutNoiseSettings.Frequency = Frequency;
	OutNoiseSettings.Persistence = Persistence;
	OutNoiseSettings.Lacunarity = Lacunarity;
	OutNoiseSettings.Octaves = OctaveNumber;
	OutNoiseSettings.PointSpacing = bEnabled ? SurfaceResolution : NoSubdivisionSpacing;
}

void FPCGFractureNoiseSettings::AddToCrc(FArchiveCrc32& Ar, bool bEnabled) const
{
	bool bLocalEnabled = bEnabled;
	Ar << bLocalEnabled;
	if (!bEnabled)
	{
		return;
	}

	FPCGFractureNoiseSettings Local = *this;
	Ar << Local.Amplitude;
	Ar << Local.Frequency;
	Ar << Local.Persistence;
	Ar << Local.Lacunarity;
	Ar << Local.OctaveNumber;
	Ar << Local.SurfaceResolution;
}
