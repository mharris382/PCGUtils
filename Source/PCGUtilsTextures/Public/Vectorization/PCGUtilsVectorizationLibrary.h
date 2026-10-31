// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Image/PCGUtilsGreyscaleImage.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Vectorization/PCGUtilsVectorizationTypes.h"

#include "PCGUtilsVectorizationLibrary.generated.h"

class UTexture;

/** The options for every vectorizer, with the one in use chosen by Algorithm. */
USTRUCT(BlueprintType)
struct PCGUTILSTEXTURES_API FPCGUtilsVectorizeOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vectorize")
	EPCGUtilsImageVectorizer Algorithm = EPCGUtilsImageVectorizer::MarchingSquares;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vectorize",
		meta=(EditCondition="Algorithm == EPCGUtilsImageVectorizer::MarchingSquares", EditConditionHides, ShowOnlyInnerProperties))
	FPCGUtilsMarchingSquaresOptions MarchingSquares;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vectorize",
		meta=(EditCondition="Algorithm == EPCGUtilsImageVectorizer::BorderFollowing", EditConditionHides, ShowOnlyInnerProperties))
	FPCGUtilsBorderFollowingOptions BorderFollowing;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vectorize",
		meta=(EditCondition="Algorithm == EPCGUtilsImageVectorizer::CurveFit", EditConditionHides, ShowOnlyInnerProperties))
	FPCGUtilsCurveFitOptions CurveFit;

	/** Runs the selected algorithm. */
	void Vectorize(const FPCGUtilsGreyscaleImage& Image, TArray<FPCGUtilsVectorPath>& OutPaths) const;

	/** A hash of the algorithm and its own options only: editing an unused algorithm's options does not change it. */
	uint32 GetActiveHash() const;
};

/** Texture sampling and image vectorization, in the style of the Geometry Script libraries. */
UCLASS(meta=(ScriptName="PCGUtilsImageVectorization"))
class PCGUTILSTEXTURES_API UPCGUtilsVectorizationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Reduces a texture to one value per pixel. Returns false, with the reason in Error, when it cannot be read. */
	UFUNCTION(BlueprintCallable, Category="PCGUtils|Textures")
	static bool SampleTextureToGreyscale(UTexture* Texture, const FPCGUtilsImageSampleOptions& Options, FPCGUtilsGreyscaleImage& Image, FText& Error);

	UFUNCTION(BlueprintCallable, Category="PCGUtils|Textures|Vectorize")
	static void VectorizeMarchingSquares(const FPCGUtilsGreyscaleImage& Image, const FPCGUtilsMarchingSquaresOptions& Options, TArray<FPCGUtilsVectorPath>& Paths);

	UFUNCTION(BlueprintCallable, Category="PCGUtils|Textures|Vectorize")
	static void VectorizeBorderFollowing(const FPCGUtilsGreyscaleImage& Image, const FPCGUtilsBorderFollowingOptions& Options, TArray<FPCGUtilsVectorPath>& Paths);

	UFUNCTION(BlueprintCallable, Category="PCGUtils|Textures|Vectorize")
	static void VectorizeCurveFit(const FPCGUtilsGreyscaleImage& Image, const FPCGUtilsCurveFitOptions& Options, TArray<FPCGUtilsVectorPath>& Paths);

	UFUNCTION(BlueprintCallable, Category="PCGUtils|Textures|Vectorize")
	static void Vectorize(const FPCGUtilsGreyscaleImage& Image, const FPCGUtilsVectorizeOptions& Options, TArray<FPCGUtilsVectorPath>& Paths);
};
