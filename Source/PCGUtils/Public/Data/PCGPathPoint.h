// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "PCGPoint.h"
#include "PCGPathPoint.generated.h"


USTRUCT(BlueprintType)
struct PCGUTILS_API FPCGPathPoint
{
	GENERATED_BODY()
	
	
	FPCGPathPoint();
	FPCGPathPoint(const FPCGPoint& Point);
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Properties")
	int32 Seed = 0;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Properties")
	FTransform Transform = FTransform();
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Properties")
	float Density = 1.0f;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Properties")
	FVector4 Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Properties")
	float Steepness = 1.0f;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Properties")
	FVector ArriveTangent = FVector();
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Properties")
	FVector LeaveTangent = FVector();
	
	
	 FLinearColor GetColor() const { return FLinearColor(Color); }
	 void SetColor(const FLinearColor& ColorIn) { Color = ColorIn; }
	
	FVector GetArriveTangent() const { return FVector(ArriveTangent); }
	FVector GetLeaveTangent() const { return FVector(LeaveTangent); }
	
	FPCGPoint ToPCGPoint() const;
};