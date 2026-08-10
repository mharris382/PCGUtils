#include "Elements/PCGUtilsSplineHelpers.h"

#include "Data/PCGSplineData.h"
#include "GameFramework/Actor.h"
#include "PCGContext.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsSplineHelpers"

namespace PCGUtilsSplineHelpers
{
	const UPCGSplineData* ResolveSingleSpline(FPCGContext* Context, FName SplinePinLabel)
	{
		const TArray<FPCGTaggedData> SplineInputs = Context->InputData.GetInputsByPin(SplinePinLabel);

		TArray<const UPCGSplineData*, TInlineAllocator<2>> SplineDataObjects;
		for (const FPCGTaggedData& Input : SplineInputs)
		{
			if (const UPCGSplineData* SplineData = Cast<const UPCGSplineData>(Input.Data))
			{
				SplineDataObjects.Add(SplineData);
			}
		}

		if (SplineDataObjects.IsEmpty())
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("NoSpline", "No valid Spline data was supplied; exactly one is required."), Context);
			return nullptr;
		}
		if (SplineDataObjects.Num() > 1)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("MultipleSplines", "{0} Spline data objects were supplied; exactly one is required for V1."),
				FText::AsNumber(SplineDataObjects.Num())), Context);
			return nullptr;
		}

		return SplineDataObjects[0];
	}

	FTransform ResolveActorTransformForSpline(FPCGContext* Context, const UPCGSplineData* SplineData, bool bConvertToLocalSpace)
	{
		if (!bConvertToLocalSpace)
		{
			return FTransform::Identity;
		}

		if (const AActor* TargetActor = Context->GetTargetActor(SplineData))
		{
			return TargetActor->GetActorTransform();
		}

		PCGLog::LogWarningOnGraph(LOCTEXT("MissingTargetActor",
			"Could not resolve a PCG target actor for spline/mesh coordinate-space conversion; the spline will be treated as already being in the Dynamic Mesh's coordinate space."), Context);
		return FTransform::Identity;
	}
}

#undef LOCTEXT_NAMESPACE
