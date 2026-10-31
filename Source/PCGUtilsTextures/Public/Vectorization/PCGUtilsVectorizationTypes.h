// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

#include "PCGUtilsVectorizationTypes.generated.h"

/** The vectorization algorithms. Each has its own options struct, reusable wherever images become paths. */
UENUM(BlueprintType)
enum class EPCGUtilsImageVectorizer : uint8
{
	/**
	 * Iso-contours at a grey level, with sub-pixel interpolation (Lorensen & Cline marching squares, as in
	 * scikit-image find_contours). Follows soft greyscale gradients smoothly; polyline output.
	 */
	MarchingSquares UMETA(DisplayName="Marching Squares"),
	/**
	 * Topological border following on the thresholded image (Suzuki & Abe 1985, the algorithm behind OpenCV's
	 * findContours). Pixel-exact boundaries through pixel centers; polygon output, optionally Douglas-Peucker simplified.
	 */
	BorderFollowing UMETA(DisplayName="Border Following"),
	/**
	 * Smooth Bezier curves fitted to the iso-contours, split at corners (Schneider, "An Algorithm for Automatically
	 * Fitting Digitized Curves", Graphics Gems 1990). Curve output with tangents, for splines.
	 */
	CurveFit UMETA(DisplayName="Curve Fit")
};

/** Which contours are kept. */
UENUM(BlueprintType)
enum class EPCGUtilsContourRetrieval : uint8
{
	/** Every boundary: outer shapes, the holes in them, islands inside holes, and so on. */
	All,
	/** Only the outermost boundaries; holes and anything inside them are dropped. */
	OuterOnly UMETA(DisplayName="Outer Only")
};

/** How marching squares resolves a saddle cell: two diagonal corners above the level and two below. */
UENUM(BlueprintType)
enum class EPCGUtilsMarchingSquaresSaddle : uint8
{
	/** Decide by the cell's average value, the usual choice. */
	Average,
	/** Always join the diagonal foreground corners, so touching shapes merge. */
	ConnectForeground UMETA(DisplayName="Connect Foreground"),
	/** Always separate the diagonal foreground corners, so touching shapes stay apart. */
	ConnectBackground UMETA(DisplayName="Connect Background")
};

/** How border-following chains are reduced. */
UENUM(BlueprintType)
enum class EPCGUtilsContourApproximation : uint8
{
	/** Every boundary pixel center. */
	None,
	/** Only the pixels where the boundary changes direction (OpenCV CHAIN_APPROX_SIMPLE). */
	Simple,
	/** Douglas-Peucker simplification to within Epsilon pixels (OpenCV approxPolyDP). */
	DouglasPeucker UMETA(DisplayName="Douglas-Peucker")
};

/** Options for Marching Squares vectorization. Distances and areas are in image pixels. */
USTRUCT(BlueprintType)
struct PCGUTILSTEXTURES_API FPCGUtilsMarchingSquaresOptions
{
	GENERATED_BODY()

	/** The grey level the contours trace. Pixels above it are the shapes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Marching Squares", meta=(ClampMin="0", ClampMax="1"))
	float IsoLevel = 0.5f;

	/** Place contour points where the level is crossed between pixels, rather than at pixel-edge midpoints. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Marching Squares")
	bool bInterpolate = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Marching Squares")
	EPCGUtilsMarchingSquaresSaddle Saddle = EPCGUtilsMarchingSquaresSaddle::Average;

	/** Douglas-Peucker tolerance applied to each contour. 0 keeps every point (one per crossed pixel edge). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Marching Squares", meta=(ClampMin="0", UIMax="4"))
	float SimplifyTolerance = 0.25f;

	/** Contours enclosing less than this many square pixels are dropped as speckles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Marching Squares", meta=(ClampMin="0"))
	float MinArea = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Marching Squares")
	EPCGUtilsContourRetrieval Retrieval = EPCGUtilsContourRetrieval::All;
};

/** Options for Border Following vectorization. Distances and areas are in image pixels. */
USTRUCT(BlueprintType)
struct PCGUTILSTEXTURES_API FPCGUtilsBorderFollowingOptions
{
	GENERATED_BODY()

	/** Pixels at or above this value are foreground. Foreground is 8-connected, background 4-connected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Border Following", meta=(ClampMin="0", ClampMax="1"))
	float Threshold = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Border Following")
	EPCGUtilsContourApproximation Approximation = EPCGUtilsContourApproximation::Simple;

	/** Douglas-Peucker tolerance. Douglas-Peucker approximation only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Border Following",
		meta=(ClampMin="0", UIMax="8", EditCondition="Approximation == EPCGUtilsContourApproximation::DouglasPeucker", EditConditionHides))
	float Epsilon = 1.0f;

	/** Contours enclosing less than this many square pixels are dropped as speckles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Border Following", meta=(ClampMin="0"))
	float MinArea = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Border Following")
	EPCGUtilsContourRetrieval Retrieval = EPCGUtilsContourRetrieval::All;
};

/** Options for Curve Fit vectorization. Distances and areas are in image pixels. */
USTRUCT(BlueprintType)
struct PCGUTILSTEXTURES_API FPCGUtilsCurveFitOptions
{
	GENERATED_BODY()

	/** The grey level the fitted contours trace. Pixels above it are the shapes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Curve Fit", meta=(ClampMin="0", ClampMax="1"))
	float IsoLevel = 0.5f;

	/** The furthest a fitted curve may stray from the traced contour. Larger is smoother, with fewer points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Curve Fit", meta=(ClampMin="0.01", UIMax="8"))
	float MaxError = 1.0f;

	/** A turn sharper than this many degrees is kept as a corner instead of being smoothed over. 180 disables corners. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Curve Fit", meta=(ClampMin="0", ClampMax="180", Units="Degrees"))
	float CornerAngle = 60.0f;

	/** Contours enclosing less than this many square pixels are dropped as speckles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Curve Fit", meta=(ClampMin="0"))
	float MinArea = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Curve Fit")
	EPCGUtilsContourRetrieval Retrieval = EPCGUtilsContourRetrieval::All;
};

/**
 * One vectorized boundary, in image pixel coordinates: X right, Y down, (0,0) at the image's top-left corner.
 * Outer boundaries wind with positive signed area in those coordinates; holes wind the other way.
 */
USTRUCT(BlueprintType)
struct PCGUTILSTEXTURES_API FPCGUtilsVectorPath
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Path")
	TArray<FVector2D> Points;

	/** Hermite tangents per point (curve output only; empty for polylines). A segment's Bezier handles are P +- T / 3. */
	UPROPERTY(BlueprintReadOnly, Category="Path")
	TArray<FVector2D> ArriveTangents;

	UPROPERTY(BlueprintReadOnly, Category="Path")
	TArray<FVector2D> LeaveTangents;

	/** Image boundaries are always closed; kept explicit for consumers that also take open paths. */
	UPROPERTY(BlueprintReadOnly, Category="Path")
	bool bClosed = true;

	/** True for a boundary around a hole in a shape (an odd nesting depth). */
	UPROPERTY(BlueprintReadOnly, Category="Path")
	bool bIsHole = false;

	/** Index of the path directly enclosing this one, or -1 for an outermost boundary. */
	UPROPERTY(BlueprintReadOnly, Category="Path")
	int32 ParentIndex = INDEX_NONE;

	/** 0 for an outermost boundary, 1 for its holes, 2 for islands inside those holes, ... */
	UPROPERTY(BlueprintReadOnly, Category="Path")
	int32 Depth = 0;

	/** Enclosed area in square pixels (unsigned). */
	UPROPERTY(BlueprintReadOnly, Category="Path")
	float Area = 0.0f;

	bool HasTangents() const { return ArriveTangents.Num() == Points.Num() && LeaveTangents.Num() == Points.Num() && Points.Num() > 0; }
};
