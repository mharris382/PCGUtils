// Fill out your copyright notice in the Description page of Project Settings.


#include "Data/PCGPathPoint.h"


FPCGPathPoint::FPCGPathPoint()
{
	Transform = FTransform();
	Density = Steepness = 1.0f;
	Color = FVector4::One();
	LeaveTangent = ArriveTangent = FVector::Zero();
}

FPCGPathPoint::FPCGPathPoint(const FPCGPoint& Point)
{
	Seed = Point.Seed;
	Transform = Point.Transform;
	Density = Point.Density;
	Color = Point.Color;
	Steepness = Point.Steepness;
	LeaveTangent = ArriveTangent = FVector::Zero();
}

FPCGPoint FPCGPathPoint::ToPCGPoint() const
{
	FPCGPoint Point = FPCGPoint(Transform, Density, Seed);
	Point.Color = Color;
	Point.BoundsMax = FVector::One() * 10;
	Point.BoundsMin = FVector::One() * -10;
	Point.Steepness = Steepness;
	return Point;
}
