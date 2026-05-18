#include "Elements/PCGSampleDistribution.h"

#include "PCGCommon.h"
#include "Data/PCGBasePointData.h"
#include "Helpers/PCGAsync.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTraits.h"

#include "Math/RandomStream.h"

#define LOCTEXT_NAMESPACE "PCGSampleDistributionSettings"

namespace PCGSampleDistribution
{
	constexpr int32 ChunkSize = 64;

	double NormalizeTime(double InTime, const UPCGSampleDistributionSettings* Settings)
	{
		if (!Settings->bClampTime)
		{
			return InTime;
		}

		const double MinTime = FMath::Min(Settings->TimeMin, Settings->TimeMax);
		const double MaxTime = FMath::Max(Settings->TimeMin, Settings->TimeMax);
		const double ClampedTime = FMath::Clamp(InTime, MinTime, MaxTime);
		const double TimeRange = Settings->TimeMax - Settings->TimeMin;

		return !FMath::IsNearlyZero(TimeRange)
			? (ClampedTime - Settings->TimeMin) / TimeRange
			: 0.0;
	}

	double SampleRange(FRandomStream& RandomSource, double MinValue, double MaxValue)
	{
		if (MinValue > MaxValue)
		{
			Swap(MinValue, MaxValue);
		}

		return MinValue + static_cast<double>(RandomSource.FRand()) * (MaxValue - MinValue);
	}

	double SampleDistribution(double Time, FRandomStream& RandomSource, const UPCGSampleDistributionSettings* Settings)
	{
		const double Alpha = NormalizeTime(Time, Settings);

		if (Settings->Mode == EPCGSampleDistributionMode::RandomBetweenCurves)
		{
			const FRichCurve* MinRichCurve = Settings->MinCurve.GetRichCurveConst();
			const FRichCurve* MaxRichCurve = Settings->MaxCurve.GetRichCurveConst();
			const double MinValue = (MinRichCurve && !MinRichCurve->IsEmpty()) ? MinRichCurve->Eval(static_cast<float>(Alpha), 0.0f) : 0.0;
			const double MaxValue = (MaxRichCurve && !MaxRichCurve->IsEmpty()) ? MaxRichCurve->Eval(static_cast<float>(Alpha), 1.0f) : 1.0;
			return SampleRange(RandomSource, MinValue, MaxValue);
		}

		const double CurrentMin = FMath::Lerp(static_cast<double>(Settings->StartRange.X), static_cast<double>(Settings->EndRange.X), Alpha);
		const double CurrentMax = FMath::Lerp(static_cast<double>(Settings->StartRange.Y), static_cast<double>(Settings->EndRange.Y), Alpha);
		return SampleRange(RandomSource, CurrentMin, CurrentMax);
	}
}

UPCGSampleDistributionSettings::UPCGSampleDistributionSettings()
{
	TimeAttribute.SetAttributeName(FName(TEXT("Time")));
	OutputTarget.SetAttributeName(FName(TEXT("SampledValue")));
}

#if WITH_EDITOR
FText UPCGSampleDistributionSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Sample Distribution");
}

FText UPCGSampleDistributionSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Samples a deterministic randomized distribution from a Time attribute and writes the result to an output attribute.");
}
#endif

TArray<FPCGPinProperties> UPCGSampleDistributionSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	FPCGPinProperties& InputPinProperty = PinProperties.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Any);
	InputPinProperty.SetRequiredPin();
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGSampleDistributionSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Any);
	return PinProperties;
}

FPCGElementPtr UPCGSampleDistributionSettings::CreateElement() const
{
	return MakeShared<FPCGSampleDistributionElement>();
}

bool FPCGSampleDistributionElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGSampleDistributionElement::Execute);

	FPCGSampleDistributionContext* Context = static_cast<FPCGSampleDistributionContext*>(InContext);
	check(Context);

	const UPCGSampleDistributionSettings* Settings = Context->GetInputSettings<UPCGSampleDistributionSettings>();
	check(Settings);

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	const int32 BaseSeed = Context->GetSeed();

	while (Context->CurrentInput < Inputs.Num())
	{
		const FPCGTaggedData& Input = Inputs[Context->CurrentInput];

		if (!Context->bDataPreparedForCurrentInput)
		{
			const UPCGData* InputData = Input.Data;
			if (!InputData || !InputData->ConstMetadata())
			{
				PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("InputUnsupportedData", "Data {0} has no metadata and cannot be sampled."), Context->CurrentInput));
				++Context->CurrentInput;
				continue;
			}

			Context->TimeAttribute = Settings->TimeAttribute.CopyAndFixLast(InputData);
			Context->TimeAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(InputData, Context->TimeAttribute);
			if (!Context->TimeAccessor)
			{
				PCGLog::Metadata::LogFailToCreateAccessorError(Context->TimeAttribute, Context);
				++Context->CurrentInput;
				continue;
			}

			if (!PCG::Private::IsBroadcastableOrConstructible(Context->TimeAccessor->GetUnderlyingType(), PCG::Private::MetadataTypes<double>::Id))
			{
				PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("TimeAttributeNotNumeric", "Time attribute/property '{0}' is not numeric."), Context->TimeAttribute.GetDisplayText()));
				++Context->CurrentInput;
				continue;
			}

			const UPCGBasePointData* InputPointData = Cast<UPCGBasePointData>(InputData);
			if (Settings->bHasCustomSeedSource || InputPointData)
			{
				if (Settings->bHasCustomSeedSource)
				{
					Context->CustomSeedSource = Settings->CustomSeedSource.CopyAndFixLast(InputData);
				}
				else
				{
					Context->CustomSeedSource.SetPointProperty(EPCGPointProperties::Seed);
				}

				Context->CustomSeedAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(InputData, Context->CustomSeedSource);
				if (!Context->CustomSeedAccessor)
				{
					PCGLog::Metadata::LogFailToCreateAccessorError(Context->CustomSeedSource, Context);
					++Context->CurrentInput;
					continue;
				}

				if (!PCG::Private::IsBroadcastableOrConstructible(Context->CustomSeedAccessor->GetUnderlyingType(), PCG::Private::MetadataTypes<int32>::Id))
				{
					PCGLog::Metadata::LogFailToGetAttributeError<int32>(Context->CustomSeedSource, Context->CustomSeedAccessor.Get(), Context);
					++Context->CurrentInput;
					continue;
				}

				Context->CustomSeedKeys = PCGAttributeAccessorHelpers::CreateConstKeys(InputData, Context->CustomSeedSource);
				check(Context->CustomSeedKeys);
			}

			FPCGTaggedData& Output = Outputs.Add_GetRef(Input);
			UPCGData* OutputData = InputData->DuplicateData(Context);
			Output.Data = OutputData;

			Context->TimeKeys = PCGAttributeAccessorHelpers::CreateConstKeys(InputData, Context->TimeAttribute);
			check(Context->TimeAccessor && Context->TimeKeys);

			Context->OutputTarget = Settings->OutputTarget.CopyAndFixSource(&Context->TimeAttribute, InputData);
			Context->OutputAccessor = PCGAttributeAccessorHelpers::CreateAccessor(OutputData, Context->OutputTarget);
			if (!Context->OutputAccessor && Context->OutputTarget.IsBasicAttribute())
			{
				OutputData->MutableMetadata()->CreateAttribute<double>(
					Context->OutputTarget.GetName(),
					0.0,
					/*bAllowsInterpolation=*/true,
					/*bOverrideParent=*/false);
				Context->OutputAccessor = PCGAttributeAccessorHelpers::CreateAccessor(OutputData, Context->OutputTarget);
			}

			Context->OutputKeys = PCGAttributeAccessorHelpers::CreateKeys(OutputData, Context->OutputTarget);
			if (!Context->OutputAccessor || !Context->OutputKeys)
			{
				PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("OutputTargetInvalid", "Failed to find/create output attribute/property '{0}'."), Context->OutputTarget.GetDisplayText()));
				Outputs.RemoveAt(Outputs.Num() - 1);
				++Context->CurrentInput;
				continue;
			}

			if (!PCG::Private::IsBroadcastableOrConstructible(PCG::Private::MetadataTypes<double>::Id, Context->OutputAccessor->GetUnderlyingType()))
			{
				PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("OutputTargetNotNumeric", "Output attribute/property '{0}' cannot accept sampled double values."), Context->OutputTarget.GetDisplayText()));
				Outputs.RemoveAt(Outputs.Num() - 1);
				++Context->CurrentInput;
				continue;
			}

			if (Settings->Mode == EPCGSampleDistributionMode::RandomBetweenCurves)
			{
				const FRichCurve* MinRichCurve = Settings->MinCurve.GetRichCurveConst();
				const FRichCurve* MaxRichCurve = Settings->MaxCurve.GetRichCurveConst();
				if (!MinRichCurve || MinRichCurve->IsEmpty() || !MaxRichCurve || MaxRichCurve->IsEmpty())
				{
					PCGE_LOG(Warning, GraphAndLog, LOCTEXT("EmptyCurveFallback", "One or both distribution curves are empty. Falling back to 0 for MinCurve and 1 for MaxCurve as needed."));
				}
			}

			Context->bDataPreparedForCurrentInput = true;
		}

		const bool bDone = FPCGAsync::AsyncProcessingRangeEx(
			&Context->AsyncState,
			Context->TimeKeys->GetNum(),
			[]() {},
			[Context, Settings, BaseSeed](int32 StartReadIndex, int32 StartWriteIndex, int32 Count) -> int32
			{
				TArray<double, TInlineAllocator<PCGSampleDistribution::ChunkSize>> TimeValues;
				TimeValues.SetNumUninitialized(Count);

				TArray<int32, TInlineAllocator<PCGSampleDistribution::ChunkSize>> LocalSeeds;
				const bool bHasCustomSeed = Context->CustomSeedAccessor.IsValid();
				if (bHasCustomSeed)
				{
					LocalSeeds.SetNumUninitialized(Count);
					if (!ensure(Context->CustomSeedAccessor->GetRange<int32>(LocalSeeds, StartReadIndex, *Context->CustomSeedKeys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible)))
					{
						return 0;
					}
				}

				if (!Context->TimeAccessor->GetRange<double>(TimeValues, StartReadIndex, *Context->TimeKeys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
				{
					return 0;
				}

				TArray<double, TInlineAllocator<PCGSampleDistribution::ChunkSize>> OutputValues;
				OutputValues.SetNumUninitialized(Count);

				for (int32 Index = 0; Index < Count; ++Index)
				{
					const int32 ElementSeed = bHasCustomSeed ? LocalSeeds[Index] : StartReadIndex + Index + 1;
					FRandomStream RandomSource(PCGHelpers::ComputeSeed(BaseSeed, ElementSeed));
					OutputValues[Index] = PCGSampleDistribution::SampleDistribution(TimeValues[Index], RandomSource, Settings);
				}

				Context->OutputAccessor->SetRange<double>(OutputValues, StartWriteIndex, *Context->OutputKeys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);
				return Count;
			},
			[](int32, int32, int32) { ensure(false); },
			[Context](int32 Count) { ensure(Context->TimeKeys->GetNum() == Count); },
			/*bEnableTimeSlicing=*/true,
			PCGSampleDistribution::ChunkSize);

		if (bDone)
		{
			++Context->CurrentInput;
			Context->bDataPreparedForCurrentInput = false;
			Context->TimeAccessor.Reset();
			Context->CustomSeedAccessor.Reset();
			Context->OutputAccessor.Reset();
			Context->TimeKeys.Reset();
			Context->CustomSeedKeys.Reset();
			Context->OutputKeys.Reset();
		}

		if (!bDone || Context->ShouldStop())
		{
			return false;
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
