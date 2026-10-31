#include "Elements/PCGSwizzleBounds.h"

#include "Data/PCGBasePointData.h"
#include "PCGContext.h"
#include "PCGPin.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGSwizzleBounds)

#define LOCTEXT_NAMESPACE "PCGSwizzleBounds"

namespace PCGUtilsSwizzleBounds
{
	void Apply(EPCGUtilsSwizzleAxes Axes, bool bFlip, EPCGUtilsSwizzlePivot Pivot,
		FTransform& InOutTransform, FVector& InOutBoundsMin, FVector& InOutBoundsMax)
	{
		int32 A = 0, B = 1;
		switch (Axes)
		{
		case EPCGUtilsSwizzleAxes::XY: A = 0; B = 1; break;
		case EPCGUtilsSwizzleAxes::XZ: A = 0; B = 2; break;
		case EPCGUtilsSwizzleAxes::YZ: A = 1; B = 2; break;
		}

		// New local axis i points along old local axis Source[i] scaled by Sign[i].
		// A takes +B and B takes -A: one swap and one negation, so the frame is a proper rotation.
		int32 Source[3] = {0, 1, 2};
		double Sign[3] = {1.0, 1.0, 1.0};
		const double S = bFlip ? -1.0 : 1.0;
		Source[A] = B; Sign[A] = S;
		Source[B] = A; Sign[B] = -S;

		FVector NewAxes[3];
		FVector NewScale, NewMin, NewMax;
		const FVector OldScale = InOutTransform.GetScale3D();
		for (int32 I = 0; I < 3; ++I)
		{
			NewAxes[I] = FVector::ZeroVector;
			NewAxes[I][Source[I]] = Sign[I];
			// Old local value v along Source[i] is new local value Sign[i] * v along i; scale moves with its axis.
			NewScale[I] = OldScale[Source[I]];
			NewMin[I] = Sign[I] > 0.0 ? InOutBoundsMin[Source[I]] : -InOutBoundsMax[Source[I]];
			NewMax[I] = Sign[I] > 0.0 ? InOutBoundsMax[Source[I]] : -InOutBoundsMin[Source[I]];
		}

		// FMatrix rows are the images of the local basis vectors.
		const FQuat LocalTurn(FMatrix(NewAxes[0], NewAxes[1], NewAxes[2], FVector::ZeroVector));
		FTransform Result(InOutTransform.GetRotation() * LocalTurn, InOutTransform.GetLocation(), NewScale);

		if (Pivot == EPCGUtilsSwizzlePivot::LocalCenter)
		{
			// Keep the old local center values, then move the point so the box center lands where it was.
			const FVector OldCenter = (InOutBoundsMin + InOutBoundsMax) * 0.5;
			const FVector SwizzledCenter = (NewMin + NewMax) * 0.5;
			const FVector Shift = SwizzledCenter - OldCenter;
			NewMin -= Shift;
			NewMax -= Shift;
			Result.SetLocation(Result.GetLocation() + Result.TransformVector(Shift));
		}

		InOutTransform = Result;
		InOutBoundsMin = NewMin;
		InOutBoundsMax = NewMax;
	}
}

TArray<FPCGPinProperties> UPCGSwizzleBoundsSettings::InputPinProperties() const
{
	FPCGPinProperties Pin(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point);
#if WITH_EDITOR
	Pin.Tooltip = LOCTEXT("InTip", "Points to swizzle. Each input is emitted on Out with its metadata and tags intact.");
#endif
	return {Pin};
}

TArray<FPCGPinProperties> UPCGSwizzleBoundsSettings::OutputPinProperties() const
{
	FPCGPinProperties Pin(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point);
#if WITH_EDITOR
	Pin.Tooltip = LOCTEXT("OutTip", "The input points with two local axes exchanged. Each point covers the same world-space box.");
#endif
	return {Pin};
}

#if WITH_EDITOR
FText UPCGSwizzleBoundsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Point | Swizzle Bounds");
}

FText UPCGSwizzleBoundsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Exchanges two local axes of each point (rotation, scale and bounds) without moving the box the point occupies. "
		"Use it before fitting a mesh to point bounds when the mesh fills the right box along the wrong axis.");
}

FString UPCGSwizzleBoundsSettings::GetAdditionalTitleInformation() const
{
	FString Info = StaticEnum<EPCGUtilsSwizzleAxes>()->GetDisplayValueAsText(Axes).ToString();
	if (bFlip) Info += TEXT(", Flipped");
	if (Preserve == EPCGUtilsSwizzlePivot::LocalCenter) Info += TEXT(", Local Center");
	return Info;
}
#endif

FPCGElementPtr UPCGSwizzleBoundsSettings::CreateElement() const
{
	return MakeShared<FPCGSwizzleBoundsElement>();
}

bool FPCGSwizzleBoundsElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGSwizzleBoundsElement::ExecuteInternal);
	const UPCGSwizzleBoundsSettings* Settings = Context->GetInputSettings<UPCGSwizzleBoundsSettings>();
	check(Settings);

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		const FPCGTaggedData& Input = Inputs[InputIndex];
		const UPCGBasePointData* InputPoints = Cast<const UPCGBasePointData>(Input.Data);
		if (!InputPoints)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("NotPoints", "Swizzle Bounds: input {0} on pin 'In' is {1}, expected point data. It was skipped."),
				FText::AsNumber(InputIndex),
				FText::FromString(Input.Data ? Input.Data->GetClass()->GetName() : TEXT("null"))), Context);
			continue;
		}

		// Duplicate keeps every point property, all metadata domains and the target actor;
		// copying the tagged data keeps tags. Only transform and bounds are changed below.
		UPCGBasePointData* OutputPoints = CastChecked<UPCGBasePointData>(InputPoints->DuplicateData(Context));
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
		Output.Data = OutputPoints;

		const int32 NumPoints = OutputPoints->GetNumPoints();
		if (NumPoints == 0) continue;

		auto Transforms = OutputPoints->GetTransformValueRange();
		auto Mins = OutputPoints->GetBoundsMinValueRange();
		auto Maxs = OutputPoints->GetBoundsMaxValueRange();
		for (int32 Index = 0; Index < NumPoints; ++Index)
		{
			FTransform Transform = Transforms[Index];
			FVector Min = Mins[Index];
			FVector Max = Maxs[Index];
			PCGUtilsSwizzleBounds::Apply(Settings->Axes, Settings->bFlip, Settings->Preserve, Transform, Min, Max);
			Transforms[Index] = Transform;
			Mins[Index] = Min;
			Maxs[Index] = Max;
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
