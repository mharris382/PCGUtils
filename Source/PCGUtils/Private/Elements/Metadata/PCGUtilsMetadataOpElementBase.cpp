// Copyright Max Harris

// See PCGUtilsMetadataOpElementBase.h for why this copy of the engine's metadata op element exists and how to
// re-sync it after an engine upgrade. Bodies below are copied from UE 5.8
// Engine/Plugins/PCG/Source/PCG/Private/Elements/Metadata/PCGMetadataOpElementBase.cpp.

#include "Elements/Metadata/PCGUtilsMetadataOpElementBase.h"

#include "PCGContext.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "Data/PCGPointData.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"

#define LOCTEXT_NAMESPACE "PCGUtilsMetadataOpElementBase"

namespace PCGUtilsMetadataBase
{
	TAutoConsoleVariable<bool> CVarMetadataOperationInMT(
		TEXT("pcgutils.MetadataOperationInMT"),
		true,
		TEXT("Metadata operations are now multithreaded."));

	TAutoConsoleVariable<int> CVarMetadataOperationChunkSize(
		TEXT("pcgutils.MetadataOperationChunkSize"),
		256,
		TEXT("Metadata operations chunk size."));

	TAutoConsoleVariable<bool> CVarMetadataOperationReserveValues(
		TEXT("pcgutils.MetadataOperationReserveValues"),
		true,
		TEXT("Metadata operations reserve values."));
}

void FPCGUtilsMetadataElementBase::PassthroughInput(FPCGContext* Context, TArray<FPCGTaggedData>& Outputs, const int32 Index) const
{
	check(Context);
	FPCGUtilsMetadataElementBase::ContextType* TimeSlicedContext = static_cast<FPCGUtilsMetadataElementBase::ContextType*>(Context);

	const UPCGMetadataSettingsBase* Settings = Context->GetInputSettings<UPCGMetadataSettingsBase>();
	check(Settings);

	const uint32 NumberOfOutputs = Settings->GetResultNum();
	const uint32 PrimaryPinIndex = Settings->GetInputPinToForward();

	// Cache the primary pin inputs if we haven't already done it.
	if (TimeSlicedContext->GetPerExecutionState().OperandPinData.IsEmpty())
	{
		TimeSlicedContext->GetPerExecutionState().OperandPinData.SetNum(Settings->GetOperandNum());
	}

	if (TimeSlicedContext->GetPerExecutionState().OperandPinData[PrimaryPinIndex].IsEmpty())
	{
		TimeSlicedContext->GetPerExecutionState().OperandPinData[PrimaryPinIndex] = Context->InputData.GetInputsByPin(Settings->GetInputPinLabel(PrimaryPinIndex));
	}

	const TArray<FPCGTaggedData>& InputsToForward = TimeSlicedContext->GetPerExecutionState().OperandPinData[PrimaryPinIndex];

	if (InputsToForward.IsEmpty())
	{
		return;
	}

	// Take the index of the iteration, except for the 1:N case, where we just grab the first index
	const int32 AdjustedIndex = (Index < InputsToForward.Num()) ? Index : 0;

	// Passthrough this single input to all of the outputs
	for (uint32 I = 0; I < NumberOfOutputs; ++I)
	{
		Outputs.Emplace_GetRef(InputsToForward[AdjustedIndex]).Pin = Settings->GetOutputPinLabel(I);
	}
}

namespace PCGUtilsMetadataOpPrivate
{
	using ContextType = FPCGUtilsMetadataElementBase::ContextType;
	using ExecStateType = FPCGUtilsMetadataElementBase::ExecStateType;

	void CreateAccessor(const FPCGAttributePropertyInputSelector& Selector, const FPCGTaggedData& InputData, PCGMetadataOps::FOperationData& OperationData, const int32 Index)
	{
		OperationData.InputSources[Index] = Selector.CopyAndFixLast(InputData.Data);
		const FPCGAttributePropertyInputSelector& InputSource = OperationData.InputSources[Index];

		OperationData.InputAccessors[Index] = PCGAttributeAccessorHelpers::CreateConstAccessor(InputData.Data, InputSource);
		OperationData.InputKeys[Index] = PCGAttributeAccessorHelpers::CreateConstKeys(InputData.Data, InputSource);
	}

	bool ValidateAccessor(const FPCGContext* Context, const UPCGMetadataSettingsBase* Settings, const FPCGTaggedData& InputData, PCGMetadataOps::FOperationData& OperationData, int32 Index)
	{
		const FPCGAttributePropertyInputSelector& InputSource = OperationData.InputSources[Index];
		const FText InputSourceText = InputSource.GetDisplayText();

		if (!OperationData.InputAccessors[Index].IsValid() || !OperationData.InputKeys[Index].IsValid())
		{
			PCGE_LOG_C(Error, GraphAndLog, Context, FText::Format(LOCTEXT("AttributeDoesNotExist", "Attribute/Property '{0}' from pin {1} does not exist"), InputSourceText, FText::FromName(InputData.Pin)));
			return false;
		}

		const uint16 AttributeTypeId = OperationData.InputAccessors[Index]->GetUnderlyingType();

		// Then verify that the type is OK
		bool bHasSpecialRequirement = false;
		if (!Settings->IsSupportedInputType(AttributeTypeId, Index, bHasSpecialRequirement))
		{
			const FText AttributeTypeName = PCG::Private::GetTypeNameText(AttributeTypeId);
			PCGE_LOG_C(Error, GraphAndLog, Context, FText::Format(LOCTEXT("UnsupportedAttributeType", "Attribute/Property '{0}' from pin {1} is not a supported type ('{2}')"),
				InputSourceText,
				FText::FromName(InputData.Pin),
				AttributeTypeName));
			return false;
		}

		if (!bHasSpecialRequirement)
		{
			// In this case, check if we have a more complex type, or if we can broadcast to the most complex type.
			if (OperationData.MostComplexInputType == static_cast<uint16>(EPCGMetadataTypes::Unknown) || PCG::Private::IsMoreComplexType(AttributeTypeId, OperationData.MostComplexInputType))
			{
				OperationData.MostComplexInputType = AttributeTypeId;
			}
			else if (OperationData.MostComplexInputType != AttributeTypeId && !PCG::Private::IsBroadcastable(AttributeTypeId, OperationData.MostComplexInputType))
			{
				const FText AttributeTypeName = PCG::Private::GetTypeNameText(AttributeTypeId);
				const FText MostComplexTypeName = PCG::Private::GetTypeNameText(OperationData.MostComplexInputType);
				PCGE_LOG_C(Error, GraphAndLog, Context, FText::Format(LOCTEXT("AttributeCannotBeBroadcasted", "Attribute '{0}' (from pin {1}) of type '{2}' cannot be used for operation with type '{3}'"),
					InputSourceText,
					FText::FromName(InputData.Pin),
					AttributeTypeName,
					MostComplexTypeName));
				return false;
			}
		}

		return true;
	}

	bool ValidateSecondaryInputClassMatches(const FPCGTaggedData& PrimaryInputData, const FPCGTaggedData& SecondaryInputData)
	{
		// First, verify the input data matches the primary. If the pin to forward is not connected, behave like a param data
		const UClass* InputPinToForwardClass = (PrimaryInputData.Data ? PrimaryInputData.Data->GetClass() : UPCGParamData::StaticClass());

		// TODO: Consider updating this to check if its a child class instead to be more future proof. For now this is good.
		// Check for data mismatch between primary pin and current pin
		if (InputPinToForwardClass != SecondaryInputData.Data->GetClass() && !SecondaryInputData.Data->IsA<UPCGParamData>())
		{
			return false;
		}

		return true;
	}
}

bool FPCGUtilsMetadataElementBase::PrepareDataInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGUtilsMetadataElementBase::PrepareDataInternal);

	FPCGUtilsMetadataElementBase::ContextType* TimeSlicedContext = static_cast<FPCGUtilsMetadataElementBase::ContextType*>(Context);
	check(TimeSlicedContext);

	const UPCGMetadataSettingsBase* Settings = Context->GetInputSettings<UPCGMetadataSettingsBase>();
	check(Settings);

	const uint32 OperandNum = Settings->GetOperandNum();
	const uint32 ResultNum = Settings->GetResultNum();

	check(OperandNum > 0);
	check(ResultNum <= UPCGMetadataSettingsBase::MaxNumberOfOutputs);

	const FName PrimaryPinLabel = Settings->GetInputPinLabel(Settings->GetInputPinToForward());
	const TArray<FPCGTaggedData> PrimaryInputs = Context->InputData.GetInputsByPin(PrimaryPinLabel);

	// There are no inputs on the primary pin, so pass-through inputs if the primary pin is required
	if (!Settings->IsPinDefaultValueActivated(PrimaryPinLabel) && PrimaryInputs.IsEmpty())
	{
		return true;
	}

	int32 OperandInputNumMax = 0;

	// Initiialize execution state, which will setup default data as need and perform early validation.
	EPCGTimeSliceInitResult ExecStateInitResult = TimeSlicedContext->InitializePerExecutionState([this, Settings, &OperandInputNumMax, OperandNum](ContextType* Context, ExecStateType& OutState) -> EPCGTimeSliceInitResult
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGUtilsMetadataElementBase::PrepareDataInternal::InitializePerExecutionState);
		FPCGUtilsMetadataElementBase::ContextType* TimeSlicedContext = static_cast<FPCGUtilsMetadataElementBase::ContextType*>(Context);
		OutState.DefaultValueOverriddenPins.AddZeroed(OperandNum);
		OutState.OperandPinData.SetNum(OperandNum);

		for (uint32 OperandPinIndex = 0; OperandPinIndex < OperandNum; ++OperandPinIndex)
		{
			const FName CurrentPinLabel = Settings->GetInputPinLabel(OperandPinIndex);
			const bool bIsInputConnected = Context->Node ? Context->Node->IsInputPinConnected(CurrentPinLabel) : false;
			TArray<FPCGTaggedData>& CurrentPinInputData = OutState.OperandPinData[OperandPinIndex];
			// Override data regardless.
			CurrentPinInputData = Context->InputData.GetInputsByPin(CurrentPinLabel);

			const int32 CurrentInputNum = CurrentPinInputData.Num();

			// For the current input, no input (0) could be default value and we support N:1 and 1:N
			if (CurrentInputNum > 1 && OperandInputNumMax > 1 && CurrentInputNum != OperandInputNumMax)
			{
				PCGE_LOG(Error, GraphAndLog, LOCTEXT("MismatchedOperandDataCount", "Number of data elements provided on inputs must be 1:N, N:1, or N:N."));
				return EPCGTimeSliceInitResult::AbortExecution;
			}

			OperandInputNumMax = FMath::Max(OperandInputNumMax, FMath::Max(1, CurrentInputNum));

			if (Settings->DefaultValuesAreEnabled() && !bIsInputConnected && CurrentPinInputData.IsEmpty() && Settings->IsPinDefaultValueActivated(CurrentPinLabel))
			{
				FPCGTaggedData& DefaultData = CurrentPinInputData.Emplace_GetRef();
				DefaultData.Pin = CurrentPinLabel;

				// @todo_pcg: Future optimizations/refactors - cache the param data on the settings, or use accessors on the default value struct, etc.
				// Create from the Default Value Container if it exists
				DefaultData.Data = Settings->CreateDefaultValueParamData(Context, CurrentPinLabel);

				// Couldn't create a default value
				if (!DefaultData.Data)
				{
					PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("CantCreateDefaultValue", "Pin '{0}' supports default value but we could not create it."), FText::FromName(CurrentPinLabel)));
					return EPCGTimeSliceInitResult::AbortExecution;
				}
				else
				{
					// Need to make sure the param data is properly tracked by the context to prevent garbage collection
					TimeSlicedContext->TrackObject(DefaultData.Data);

					UPCGMetadata* DefaultParamMetadata = CastChecked<UPCGParamData>(DefaultData.Data)->Metadata;
					if (DefaultParamMetadata->GetLocalItemCount() == 0)
					{
						DefaultParamMetadata->AddEntry();
					}

					OutState.DefaultValueOverriddenPins[OperandPinIndex] = true;
				}
			}

			if (CurrentPinInputData.IsEmpty())
			{
				// If we have no data, there is no operation
				PCGE_LOG(Verbose, LogOnly, FText::Format(LOCTEXT("MissingInputDataForPin", "No data provided on pin '{0}'."), FText::FromName(CurrentPinLabel)));
				return EPCGTimeSliceInitResult::NoOperation;
			}
			else if (CurrentPinInputData.Num() != 1 && CurrentPinInputData.Num() != OperandInputNumMax)
			{
				PCGE_LOG(Error, GraphAndLog,
					FText::Format(LOCTEXT("MismatchedDataCountForPin", "Number of data elements ({0}) provided on pin '{1}' doesn't match number of expected elements ({2}). Only 1 input or {2} are supported."),
						CurrentPinInputData.Num(),
						FText::FromName(CurrentPinLabel),
						OperandInputNumMax));
				return EPCGTimeSliceInitResult::AbortExecution;
			}
		}

		return EPCGTimeSliceInitResult::Success;
	});

	if (ExecStateInitResult == EPCGTimeSliceInitResult::AbortExecution)
	{
		return true;
	}
	else if (ExecStateInitResult == EPCGTimeSliceInitResult::NoOperation)
	{
		// Passthrough all inputs
		// @todo_pcg - this could be optimized somewhat by doing a version that takes the OperandInputNumMax here.
		TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;
		for (int IterationIndex = 0; IterationIndex < OperandInputNumMax; ++IterationIndex)
		{
			PassthroughInput(Context, Outputs, IterationIndex);
		}

		return true;
	}

	// Set up the iterations on the multiple inputs of the primary pin
	TimeSlicedContext->InitializePerIterationStates(OperandInputNumMax, [this, Context, OperandNum, Settings, OperandInputNumMax](IterStateType& OutState, const ExecStateType& ExecState, const uint32 IterationIndex)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGUtilsMetadataElementBase::PrepareDataInternal::InitializePerIterationStates);
		FPCGUtilsMetadataElementBase::ContextType* TimeSlicedContext = static_cast<FPCGUtilsMetadataElementBase::ContextType*>(Context);
		TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;
		const uint32 NumberOfResults = Settings->GetResultNum();

		OutState.Context = Context;

		// Gathering all the inputs metadata
		TArray<const UPCGMetadata*> SourceMetadata;
		TArray<const FPCGMetadataAttributeBase*> SourceAttribute;
		TArray<FPCGTaggedData> InputTaggedData;
		SourceMetadata.SetNum(OperandNum);
		SourceAttribute.SetNum(OperandNum);
		InputTaggedData.SetNum(OperandNum);

		// Since we add the output data (in CreateAttribute below) if the operation is valid in the PrepareData, if we ever have a no-op, we have to passthrough the inputs now and not in the Execute. So that the order is respected
		// in the end. (ie. { Input1(Valid), Input2(No-Op), Input3(Valid) } will have in output { Output1, Input2, Output3 } and not { Output1, Output3, Input2 } if we do the passthrough in the Execute.)
		auto NoOperation = [this, Context, IterationIndex, &Outputs]() { PassthroughInput(Context, Outputs, IterationIndex); return EPCGTimeSliceInitResult::NoOperation; };

		const uint32 PrimaryPinIndex = Settings->GetInputPinToForward();

		// Iterate over the inputs and validate
		for (uint32 OperandPinIndex = 0; OperandPinIndex < OperandNum; ++OperandPinIndex)
		{
			const TArray<FPCGTaggedData>& CurrentPinInputData = ExecState.OperandPinData[OperandPinIndex];

			// The operand inputs must either be N:1 or N:N or 1:N
			InputTaggedData[OperandPinIndex] = CurrentPinInputData.Num() == 1 ? CurrentPinInputData[0] : CurrentPinInputData[IterationIndex];

			// Check if we have any points
			if (const UPCGBasePointData* PointInput = Cast<const UPCGBasePointData>(InputTaggedData[OperandPinIndex].Data))
			{
				if (PointInput->GetNumPoints() == 0)
				{
					const FName CurrentPinLabel = Settings->GetInputPinLabel(OperandPinIndex);
					// If we have no points, there is no operation
					PCGE_LOG(Verbose, LogOnly, FText::Format(LOCTEXT("NoPointsForPin", "No points in point data provided on pin {0}"), FText::FromName(CurrentPinLabel)));
					return NoOperation();
				}
			}

			SourceMetadata[OperandPinIndex] = InputTaggedData[OperandPinIndex].Data->ConstMetadata();
			if (!SourceMetadata[OperandPinIndex])
			{
				const FName CurrentPinLabel = Settings->GetInputPinLabel(OperandPinIndex);
				// Since this aborts execution, and the user can fix it, it should be a node error
				PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("InvalidInputDataTypeForPin", "Invalid data provided on pin '{0}', must be of type Spatial or Attribute Set."), FText::FromName(CurrentPinLabel)));
				return EPCGTimeSliceInitResult::AbortExecution;
			}
		}

		PCGMetadataOps::FOperationData& OperationData = OutState;
		OperationData.Settings = Settings;
		OperationData.InputAccessors.SetNum(OperandNum);
		OperationData.InputKeys.SetNum(OperandNum);
		OperationData.InputSources.SetNum(OperandNum);
		OperationData.MostComplexInputType = static_cast<uint16>(EPCGMetadataTypes::Unknown);

		const FPCGTaggedData& PrimaryPinData = InputTaggedData[PrimaryPinIndex];

		// First create an accessor for the input to forward (it's our control data)
		const FPCGAttributePropertyInputSelector PrimarySelector = !ExecState.DefaultValueOverriddenPins[PrimaryPinIndex] ? Settings->GetInputSource(PrimaryPinIndex) : FPCGAttributePropertyInputSelector{};

		PCGUtilsMetadataOpPrivate::CreateAccessor(PrimarySelector, PrimaryPinData, OperationData, PrimaryPinIndex);
		if (!PCGUtilsMetadataOpPrivate::ValidateAccessor(Context, Settings, PrimaryPinData, OperationData, PrimaryPinIndex))
		{
			return EPCGTimeSliceInitResult::AbortExecution;
		}

		// Update the number of elements to process, it's OK to be 0 if it is an attribute, as we can do a default value operation.
		OperationData.NumberOfElementsToProcess = OperationData.InputKeys[PrimaryPinIndex]->GetNum();
		if (OperationData.NumberOfElementsToProcess == 0 && !OperationData.InputAccessors[PrimaryPinIndex]->IsAttribute())
		{
			PCGE_LOG(Verbose, LogOnly, FText::Format(LOCTEXT("NoElementsInForwardedInput", "No elements in data from forwarded pin '{0}'."), FText::FromName(PrimaryPinData.Pin)));
			return NoOperation();
		}

		// Create the accessors and validate them for each of the other operands
		for (uint32 Index = 0; Index < OperandNum; ++Index)
		{
			if (Index != PrimaryPinIndex)
			{
				// Secondary input class should match the forwarded one
				if (!PCGUtilsMetadataOpPrivate::ValidateSecondaryInputClassMatches(PrimaryPinData, InputTaggedData[Index]))
				{
					PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("InputTypeMismatch", "Data on pin '{0}' is not of the same type than on pin '{1}' and is not an Attribute Set. This is not supported."), FText::FromName(InputTaggedData[Index].Pin), FText::FromName(PrimaryPinData.Pin)));
					return EPCGTimeSliceInitResult::AbortExecution;
				}

				const FPCGAttributePropertyInputSelector Selector = !ExecState.DefaultValueOverriddenPins[Index] ? Settings->GetInputSource(Index) : FPCGAttributePropertyInputSelector{};

				PCGUtilsMetadataOpPrivate::CreateAccessor(Selector, InputTaggedData[Index], OperationData, Index);
				if (!PCGUtilsMetadataOpPrivate::ValidateAccessor(Context, Settings, InputTaggedData[Index], OperationData, Index))
				{
					return EPCGTimeSliceInitResult::AbortExecution;
				}

				const int32 ElementNum = OperationData.InputKeys[Index]->GetNum();

				// No elements on secondary pin, early out for no operation, only if it is not an attribute, as we could still do a default value operation
				if (ElementNum == 0 && !OperationData.InputAccessors[Index]->IsAttribute())
				{
					PCGE_LOG(Verbose, LogOnly, FText::Format(LOCTEXT("NoElementsInInput", "No elements in data from secondary pin '{0}'."), FText::FromName(PrimaryPinData.Pin)));
					return NoOperation();
				}

				// Verify that the number of elements makes sense
				if (ElementNum != 0 && OperationData.NumberOfElementsToProcess % ElementNum != 0)
				{
					PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("MismatchInNumberOfElements", "Mismatch between the number of elements from pin '{0}' ({1}) and from pin '{2}' ({3})."), FText::FromName(PrimaryPinData.Pin), OperationData.NumberOfElementsToProcess, FText::FromName(InputTaggedData[Index].Pin), ElementNum));
					return EPCGTimeSliceInitResult::AbortExecution;
				}

				// If selection is an attribute, get it from the metadata
				FPCGAttributePropertyInputSelector& InputSource = OperationData.InputSources[Index];
				if (InputSource.GetSelection() == EPCGAttributePropertySelection::Attribute)
				{
					SourceAttribute[Index] = SourceMetadata[Index]->GetConstAttribute(InputSource.GetName());
				}
				else
				{
					SourceAttribute[Index] = nullptr;
				}
			}
		}

		// At this point, we verified everything, so we can go forward with the computation, depending on the most complex type
		// So first forward outputs and create the attribute
		OperationData.OutputAccessors.SetNum(Settings->GetResultNum());
		OperationData.OutputKeys.SetNum(Settings->GetResultNum());

		const FPCGAttributePropertyOutputSelector OutputTarget = Settings->OutputTarget.CopyAndFixSource(&OperationData.InputSources[PrimaryPinIndex]);

		// Use implicit capture, since we capture a lot
		auto CreateAttribute = [&]<typename AttributeType>(uint32 OutputIndex, AttributeType DummyOutValue) -> bool
		{
			FPCGTaggedData& OutputTaggedData = Outputs.Add_GetRef(InputTaggedData[PrimaryPinIndex]);
			OutputTaggedData.Pin = Settings->GetOutputPinLabel(OutputIndex);

			// In case of property or attribute with extra accessor, we need to validate that the property/attribute can accept the output type.
			// Verify this before duplicating, because an extra allocation is certainly less costly than duplicating the data.
			// Do it with a const accessor, since OutputTaggedData.Data is still pointing on the const input data.

			if (!OutputTarget.IsBasicAttribute())
			{
				const TUniquePtr<const IPCGAttributeAccessor> TempConstAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(OutputTaggedData.Data.Get(), OutputTarget);

				if (!TempConstAccessor.IsValid())
				{
					PCGLog::Metadata::LogFailToCreateAccessorError(OutputTarget, Context);
					return false;
				}

				if (!PCG::Private::IsBroadcastable(PCG::Private::MetadataTypes<AttributeType>::Id, TempConstAccessor->GetUnderlyingType()))
				{
					PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("AttributeTypeBroadcastFailed_Updated", "Output Attribute/Property '{0}' ({1}) is not compatible with operation output type ({2})."),
						OutputTarget.GetDisplayText(),
						PCG::Private::GetTypeNameText(TempConstAccessor->GetUnderlyingType()),
						PCG::Private::GetTypeNameText<AttributeType>()));
					return false;
				}

				// We have no element to process but we try to write into a property, early out.
				if (OperationData.NumberOfElementsToProcess == 0 && !TempConstAccessor->IsAttribute())
				{
					PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NoDefaultValue", "Operation is done on the default value, but output attribute '{0}' does not support default values"), OutputTarget.GetDisplayText()), Context);
					return false;
				}
			}

			check(InputTaggedData[PrimaryPinIndex].Data);
			UPCGData* OutputData = InputTaggedData[PrimaryPinIndex].Data->DuplicateData(Context);
			check(OutputData);
			OutputTaggedData.Data = OutputData;

			if (OutputTarget.IsBasicAttribute())
			{
				FPCGMetadataAttributeBase* OutputAttribute = PCGMetadataElementCommon::ClearOrCreateAttribute<AttributeType>(OutputData->MutableMetadata(), OutputTarget);
				if (!OutputAttribute)
				{
					return false;
				}
			}

			OperationData.OutputAccessors[OutputIndex] = PCGAttributeAccessorHelpers::CreateAccessor(OutputData, OutputTarget);

			if (!OperationData.OutputAccessors[OutputIndex].IsValid())
			{
				return false;
			}

			if (OperationData.OutputAccessors[OutputIndex]->IsReadOnly())
			{
				PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("OutputAccessorIsReadOnly", "Attribute/Property '{0}' is read only."), OutputTarget.GetDisplayText()));
				return false;
			}

			if (OperationData.OutputAccessors[OutputIndex]->IsAttribute()
				&& OperationData.NumberOfElementsToProcess > 1
				&& !OutputData->ConstMetadata()->MetadataDomainSupportsMultiEntries(OutputData->GetMetadataDomainIDFromSelector(OutputTarget)))
			{
				PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("OutputAccessorIsNotSupportingMultiEntries", "Output attribute '{0}' is on a domain that doesn't support multi entries, but we try to process multiple elements ({1}). It's invalid."), OutputTarget.GetDisplayText(), OperationData.NumberOfElementsToProcess));
				return false;
			}

			OperationData.OutputKeys[OutputIndex] = PCGAttributeAccessorHelpers::CreateKeys(OutputData, OutputTarget);

			return OperationData.OutputKeys[OutputIndex].IsValid();
		};

		auto CreateAllSameAttributes = [NumberOfResults, &CreateAttribute](auto DummyOutValue) -> bool
		{
			for (uint32 i = 0; i < NumberOfResults; ++i)
			{
				if (!CreateAttribute(i, DummyOutValue))
				{
					return false;
				}
			}

			return true;
		};

		OperationData.OutputType = Settings->GetOutputType(OperationData.MostComplexInputType);

		bool bCreateAttributeSucceeded = true;

		if (!Settings->HasDifferentOutputTypes())
		{
			bCreateAttributeSucceeded = PCGMetadataAttribute::CallbackWithRightType(OperationData.OutputType, CreateAllSameAttributes);
		}
		else
		{
			TArray<uint16> OutputTypes = Settings->GetAllOutputTypes();
			check(OutputTypes.Num() == NumberOfResults);

			for (uint32 i = 0; i < NumberOfResults && bCreateAttributeSucceeded; ++i)
			{
				bCreateAttributeSucceeded &= PCGMetadataAttribute::CallbackWithRightType(OutputTypes[i], [&CreateAttribute, i](auto DummyOutValue) -> bool
				{
					return CreateAttribute(i, DummyOutValue);
				});
			}
		}

		if (!bCreateAttributeSucceeded)
		{
			PCGE_LOG(Error, GraphAndLog, LOCTEXT("ErrorCreatingOutputAttributes", "Error while creating output attributes"));
			return EPCGTimeSliceInitResult::AbortExecution;
		}

		OperationData.Settings = Settings;

		return EPCGTimeSliceInitResult::Success;
	});

	return true;
}

bool FPCGUtilsMetadataElementBase::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGUtilsMetadataElementBase::Execute);
	ContextType* TimeSlicedContext = static_cast<ContextType*>(Context);
	check(TimeSlicedContext);

	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	// Prepare data failed, no need to execute. Return an empty output
	if (!TimeSlicedContext->DataIsPreparedForExecution())
	{
		return true;
	}

	return ExecuteSlice(TimeSlicedContext, [this, &Outputs](ContextType* Context, const ExecStateType& ExecState, IterStateType& IterState, const uint32 IterationIndex) -> bool
	{
		// No operation, so skip the iteration.
		if (Context->GetIterationStateResult(IterationIndex) == EPCGTimeSliceInitResult::NoOperation)
		{
			return true;
		}

		const bool bIsDone = DoOperation(IterState);
		
		if (bIsDone)
		{
			// Make sure the async state is reset, otherwise it means the metadata op is not taking into account time-slicing correctly
			ensureMsgf(!Context->AsyncState.bStarted,
				TEXT("Metadata operation has not finish processing the previous data and is starting a new processing.\n"
				"Make sure that the DoOperation is returning true only when the async processing is done."));
		}

		return bIsDone;
	});
}

#undef LOCTEXT_NAMESPACE
