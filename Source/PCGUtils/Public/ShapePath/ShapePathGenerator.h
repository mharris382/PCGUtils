#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ShapePathGenerator.generated.h"

UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories, BlueprintType)
class PCGUTILS_API UShapePathGenerator : public UObject
{
	GENERATED_BODY()
public:
	virtual void GeneratePoints(TArray<FVector>& OutPoints) const PURE_VIRTUAL(UShapePathGenerator::GeneratePoints,);
	virtual bool IsClosedLoop() const { return true; }
};

UCLASS(DisplayName="Circle")
class PCGUTILS_API UCircleGenerator : public UShapePathGenerator
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circle", meta=(ClampMin=1.0))
	float Radius = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circle", meta=(ClampMin=3))
	int32 Segments = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circle")
	float StartAngleDeg = 0.f;

	virtual void GeneratePoints(TArray<FVector>& OutPoints) const override;
	virtual bool IsClosedLoop() const override { return true; }
};

UCLASS(DisplayName="Rectangle")
class PCGUTILS_API URectangleGenerator : public UShapePathGenerator
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rectangle")
	float Width = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rectangle")
	float Height = 100.f;

	/** Points per edge, not counting the shared endpoint — interpolates each side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rectangle", meta=(ClampMin=2))
	int32 PointsPerSide = 2;

	virtual void GeneratePoints(TArray<FVector>& OutPoints) const override;
	virtual bool IsClosedLoop() const override { return true; }
};

UCLASS(DisplayName="Regular Polygon")
class PCGUTILS_API URegularPolygonGenerator : public UShapePathGenerator
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon", meta=(ClampMin=1.0))
	float Radius = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon", meta=(ClampMin=3))
	int32 Sides = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon")
	float StartAngleDeg = 0.f;

	virtual void GeneratePoints(TArray<FVector>& OutPoints) const override;
	virtual bool IsClosedLoop() const override { return true; }
};

UCLASS(DisplayName="Arc / Semi-Circle")
class PCGUTILS_API UArcGenerator : public UShapePathGenerator
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arc", meta=(ClampMin=1.0))
	float Radius = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arc", meta=(ClampMin=1.0, ClampMax=360.0))
	float ArcDeg = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arc")
	float StartAngleDeg = -90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arc", meta=(ClampMin=2))
	int32 Segments = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arc")
	bool bIsClosedLoop = false;

	virtual void GeneratePoints(TArray<FVector>& OutPoints) const override;
	virtual bool IsClosedLoop() const override { return bIsClosedLoop; }
};

UCLASS(DisplayName="Star")
class PCGUTILS_API UStarGenerator : public UShapePathGenerator
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Star")
	float OuterRadius = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Star")
	float InnerRadius = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Star", meta=(ClampMin=3))
	int32 Points = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Star")
	float StartAngleDeg = -90.f;

	virtual void GeneratePoints(TArray<FVector>& OutPoints) const override;
	virtual bool IsClosedLoop() const override { return true; }
};

/** Open Archimedean spiral with optional vertical rise. */
UCLASS(DisplayName="Spiral")
class PCGUTILS_API USpiralGenerator : public UShapePathGenerator
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral", meta=(ClampMin=0.0))
	float StartRadius = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral", meta=(ClampMin=0.0))
	float EndRadius = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral", meta=(ClampMin=0.01))
	float Turns = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral", meta=(ClampMin=1))
	int32 SegmentsPerTurn = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral")
	float Height = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral")
	float StartAngleDeg = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral")
	bool bClockwise = false;

	virtual void GeneratePoints(TArray<FVector>& OutPoints) const override;
	virtual bool IsClosedLoop() const override { return false; }
};
