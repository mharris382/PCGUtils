// Copyright Max Harris

#include "Visualizers/ImagePathComponentVisualizer.h"

#include "Components/ImagePathComponent.h"
#include "SceneManagement.h"

void FImagePathComponentVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	const UImagePathComponent* ImagePath = Cast<UImagePathComponent>(Component);
	if (!ImagePath || ImagePath->GetImageSize().X <= 0)
	{
		return;
	}
	const FTransform& ToWorld = ImagePath->GetComponentTransform();

	const TArray<FVector, TFixedAllocator<4>> Corners = ImagePath->GetLocalImageCorners();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		PDI->DrawLine(ToWorld.TransformPosition(Corners[Index]), ToWorld.TransformPosition(Corners[(Index + 1) % 4]),
			FLinearColor(0.35f, 0.35f, 0.35f), SDPG_World);
	}

	const FLinearColor OuterColor = FLinearColor::Green;
	const FLinearColor HoleColor(1.0f, 0.45f, 0.05f);
	for (const FPCGUtilsVectorPath& Path : ImagePath->GetVectorPaths())
	{
		const int32 Num = Path.Points.Num();
		if (Num < 2)
		{
			continue;
		}
		const FLinearColor& Color = Path.bIsHole ? HoleColor : OuterColor;
		const int32 SegmentCount = Path.bClosed ? Num : Num - 1;
		for (int32 Index = 0; Index < SegmentCount; ++Index)
		{
			const int32 Next = (Index + 1) % Num;
			const FVector P0 = ImagePath->ImageToLocal(Path.Points[Index]);
			const FVector P1 = ImagePath->ImageToLocal(Path.Points[Next]);
			if (!Path.HasTangents())
			{
				PDI->DrawLine(ToWorld.TransformPosition(P0), ToWorld.TransformPosition(P1), Color, SDPG_Foreground);
				continue;
			}
			// Hermite segment, drawn as a short polyline.
			const FVector T0 = ImagePath->ImageVectorToLocal(Path.LeaveTangents[Index]);
			const FVector T1 = ImagePath->ImageVectorToLocal(Path.ArriveTangents[Next]);
			FVector Previous = P0;
			constexpr int32 Steps = 8;
			for (int32 Step = 1; Step <= Steps; ++Step)
			{
				const float T = static_cast<float>(Step) / Steps;
				const FVector Point = FMath::CubicInterp(P0, T0, P1, T1, T);
				PDI->DrawLine(ToWorld.TransformPosition(Previous), ToWorld.TransformPosition(Point), Color, SDPG_Foreground);
				Previous = Point;
			}
		}
	}
}
