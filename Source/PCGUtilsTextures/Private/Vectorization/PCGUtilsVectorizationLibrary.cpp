// Copyright Max Harris

#include "Vectorization/PCGUtilsVectorizationLibrary.h"

#include "Misc/Crc.h"
#include "Vectorization/PCGUtilsVectorization.h"

namespace
{
	/** Hashes a struct by its exported text, so new option fields join the hash without being listed here. */
	template <typename TStruct>
	uint32 HashVectorizeOptionsStruct(const TStruct& Value, uint32 Seed)
	{
		FString Text;
		TStruct::StaticStruct()->ExportText(Text, &Value, nullptr, nullptr, PPF_None, nullptr);
		return FCrc::StrCrc32(*Text, Seed);
	}
}

void FPCGUtilsVectorizeOptions::Vectorize(const FPCGUtilsGreyscaleImage& Image, TArray<FPCGUtilsVectorPath>& OutPaths) const
{
	switch (Algorithm)
	{
	case EPCGUtilsImageVectorizer::BorderFollowing: PCGUtilsVectorization::BorderFollowing(Image, BorderFollowing, OutPaths); break;
	case EPCGUtilsImageVectorizer::CurveFit: PCGUtilsVectorization::CurveFit(Image, CurveFit, OutPaths); break;
	case EPCGUtilsImageVectorizer::MarchingSquares:
	default: PCGUtilsVectorization::MarchingSquares(Image, MarchingSquares, OutPaths); break;
	}
}

uint32 FPCGUtilsVectorizeOptions::GetActiveHash() const
{
	const uint32 Seed = static_cast<uint32>(Algorithm) + 1;
	switch (Algorithm)
	{
	case EPCGUtilsImageVectorizer::BorderFollowing: return HashVectorizeOptionsStruct(BorderFollowing, Seed);
	case EPCGUtilsImageVectorizer::CurveFit: return HashVectorizeOptionsStruct(CurveFit, Seed);
	case EPCGUtilsImageVectorizer::MarchingSquares:
	default: return HashVectorizeOptionsStruct(MarchingSquares, Seed);
	}
}

bool UPCGUtilsVectorizationLibrary::SampleTextureToGreyscale(UTexture* Texture, const FPCGUtilsImageSampleOptions& Options, FPCGUtilsGreyscaleImage& Image, FText& Error)
{
	return PCGUtilsImageSampling::SampleTexture(Texture, Options, Image, Error);
}

void UPCGUtilsVectorizationLibrary::VectorizeMarchingSquares(const FPCGUtilsGreyscaleImage& Image, const FPCGUtilsMarchingSquaresOptions& Options, TArray<FPCGUtilsVectorPath>& Paths)
{
	PCGUtilsVectorization::MarchingSquares(Image, Options, Paths);
}

void UPCGUtilsVectorizationLibrary::VectorizeBorderFollowing(const FPCGUtilsGreyscaleImage& Image, const FPCGUtilsBorderFollowingOptions& Options, TArray<FPCGUtilsVectorPath>& Paths)
{
	PCGUtilsVectorization::BorderFollowing(Image, Options, Paths);
}

void UPCGUtilsVectorizationLibrary::VectorizeCurveFit(const FPCGUtilsGreyscaleImage& Image, const FPCGUtilsCurveFitOptions& Options, TArray<FPCGUtilsVectorPath>& Paths)
{
	PCGUtilsVectorization::CurveFit(Image, Options, Paths);
}

void UPCGUtilsVectorizationLibrary::Vectorize(const FPCGUtilsGreyscaleImage& Image, const FPCGUtilsVectorizeOptions& Options, TArray<FPCGUtilsVectorPath>& Paths)
{
	Options.Vectorize(Image, Paths);
}
