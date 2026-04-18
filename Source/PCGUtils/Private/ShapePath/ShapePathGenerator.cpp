#include "ShapePath/ShapePathGenerator.h"

void UCircleGenerator::GeneratePoints(TArray<FVector>& OutPoints) const
{
	const int32 N = FMath::Max(Segments, 3);
	OutPoints.Reset(N);
	const float StartRad = FMath::DegreesToRadians(StartAngleDeg);
	const float Step = 2.f * PI / N;
	for (int32 i = 0; i < N; ++i)
	{
		const float Angle = StartRad + Step * i;
		OutPoints.Add(FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.f));
	}
}

void URectangleGenerator::GeneratePoints(TArray<FVector>& OutPoints) const
{
	const float HW = Width * 0.5f;
	const float HH = Height * 0.5f;
	const FVector Corners[4] = {
		FVector(-HW, -HH, 0.f),
		FVector( HW, -HH, 0.f),
		FVector( HW,  HH, 0.f),
		FVector(-HW,  HH, 0.f),
	};

	const int32 PPS = FMath::Max(PointsPerSide, 2);
	OutPoints.Reset(4 * PPS);
	for (int32 Side = 0; Side < 4; ++Side)
	{
		const FVector& A = Corners[Side];
		const FVector& B = Corners[(Side + 1) % 4];
		for (int32 p = 0; p < PPS; ++p)
		{
			const float T = static_cast<float>(p) / static_cast<float>(PPS);
			OutPoints.Add(FMath::Lerp(A, B, T));
		}
	}
}

void URegularPolygonGenerator::GeneratePoints(TArray<FVector>& OutPoints) const
{
	const int32 N = FMath::Max(Sides, 3);
	OutPoints.Reset(N);
	const float StartRad = FMath::DegreesToRadians(StartAngleDeg);
	const float Step = 2.f * PI / N;
	for (int32 i = 0; i < N; ++i)
	{
		const float Angle = StartRad + Step * i;
		OutPoints.Add(FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.f));
	}
}

void UArcGenerator::GeneratePoints(TArray<FVector>& OutPoints) const
{
	const int32 N = FMath::Max(Segments, 2);
	OutPoints.Reset(N + 1);
	const float StartRad = FMath::DegreesToRadians(StartAngleDeg);
	const float ArcRad   = FMath::DegreesToRadians(FMath::Clamp(ArcDeg, 1.f, 360.f));
	const float Step     = ArcRad / N;
	for (int32 i = 0; i <= N; ++i)
	{
		const float Angle = StartRad + Step * i;
		OutPoints.Add(FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.f));
	}
}

void UStarGenerator::GeneratePoints(TArray<FVector>& OutPoints) const
{
	const int32 N = FMath::Max(Points, 3);
	const int32 Total = N * 2;
	OutPoints.Reset(Total);
	const float StartRad = FMath::DegreesToRadians(StartAngleDeg);
	const float Step = PI / N;
	for (int32 i = 0; i < Total; ++i)
	{
		const float Angle = StartRad + Step * i;
		const float R = (i % 2 == 0) ? OuterRadius : InnerRadius;
		OutPoints.Add(FVector(R * FMath::Cos(Angle), R * FMath::Sin(Angle), 0.f));
	}
}
