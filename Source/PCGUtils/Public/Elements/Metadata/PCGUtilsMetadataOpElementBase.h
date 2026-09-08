// Copyright Max Harris

#pragma once

//
// NOTE: This is a near-verbatim copy of the engine's `FPCGMetadataElementBase` (UE 5.8,
// Engine/Plugins/PCG/Source/PCG/{Public,Private}/Elements/Metadata/PCGMetadataOpElementBase.{h,cpp}).
//
// The copy exists because that class carries no `PCG_API`/`UE_API` and its `pcg.MetadataOperation*` console
// variables are not exported either, so no module outside `PCG` can derive from it - the vtable of any derived
// class needs the addresses of `PrepareDataInternal` and `ExecuteInternal`, which the PCG DLL does not export.
// `UPCGMetadataSettingsBase` IS exported, so only the element half is duplicated here; settings classes still
// derive from the engine type and keep the engine's pin, typing and default-value behaviour.
//
// Everything a PCGUtils metadata operation needs from the engine header (`PCGMetadataOps::FOperationData`,
// `FInputStateData`, `PCG::Private::NAryOperation`) is header-only and is reused directly rather than copied.
// The console variables are renamed to `pcgutils.*` so they do not collide with the engine's registrations.
//
// When upgrading the engine, diff this file and its .cpp against the engine originals above and re-sync.
//

#include "Elements/Metadata/PCGMetadataOpElementBase.h"

namespace PCGUtilsMetadataBase
{
	extern TAutoConsoleVariable<bool> CVarMetadataOperationInMT;
	extern TAutoConsoleVariable<int> CVarMetadataOperationChunkSize;
	extern TAutoConsoleVariable<bool> CVarMetadataOperationReserveValues;
}

class FPCGUtilsMetadataElementBase : public TPCGTimeSlicedElementBase<PCGMetadataOps::FInputStateData, PCGMetadataOps::FOperationData>
{
protected:
	virtual bool PrepareDataInternal(FPCGContext* Context) const override;
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual EPCGElementExecutionLoopMode ExecutionLoopMode(const UPCGSettings* Settings) const override { return EPCGElementExecutionLoopMode::SinglePrimaryPin; }
	virtual bool SupportsBasePointDataInputs(FPCGContext* InContext) const override { return true; }

	virtual bool DoOperation(PCGMetadataOps::FOperationData& InOperationData) const = 0;

	/**
	* Generic method to factorise all the boilerplate code for a variable number of inputs/outputs
	*/
	template <typename... InputTypes, typename... Callbacks>
	inline bool DoNAryOp(PCGMetadataOps::FOperationData& InOperationData, TTuple<Callbacks...>&& InCallbacks) const;

	/* All operations can have a fixed number of inputs and a variable number of outputs.
	* Each output need to have its own callback, all taking the exact number of "const InType&" as input
	* and each can return a different output type.
	*/
	template <typename InType, typename... Callbacks>
	bool DoUnaryOp(PCGMetadataOps::FOperationData& InOperationData, Callbacks&& ...InCallbacks) const;

	template <typename InType1, typename InType2, typename... Callbacks>
	bool DoBinaryOp(PCGMetadataOps::FOperationData& InOperationData, Callbacks&& ...InCallbacks) const;

	template <typename InType1, typename InType2, typename InType3, typename... Callbacks>
	bool DoTernaryOp(PCGMetadataOps::FOperationData& InOperationData, Callbacks&& ...InCallbacks) const;

	template <typename InType1, typename InType2, typename InType3, typename InType4, typename... Callbacks>
	bool DoQuaternaryOp(PCGMetadataOps::FOperationData& InOperationData, Callbacks&& ...InCallbacks) const;

	/** To be called if we have no data to perform any operation, it will passthrough the input. The per execution state should be initialized before calling this, or performance issues might ensue if there is a large amount of inputs. */
	void PassthroughInput(FPCGContext* Context, TArray<FPCGTaggedData>& Outputs, const int32 Index) const;
};

template <typename... InputTypes, typename... Callbacks>
inline bool FPCGUtilsMetadataElementBase::DoNAryOp(PCGMetadataOps::FOperationData& InOperationData, TTuple<Callbacks...>&& InCallbacks) const
{
	// Validate that all is good
	constexpr uint32 NbInputs = (uint32)sizeof...(InputTypes);
	constexpr uint32 NbOutputs = (uint32)sizeof...(Callbacks);

	static_assert(NbInputs <= UPCGMetadataSettingsBase::MaxNumberOfInputs);
	static_assert(NbOutputs <= UPCGMetadataSettingsBase::MaxNumberOfOutputs);

	InOperationData.Validate<NbInputs, NbOutputs>();

	EPCGAttributeAccessorFlags Flags = EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible;

	// First set the default value (only on first pass)
	PCG::Private::NAryOperation::Options Options{ Flags, Flags | EPCGAttributeAccessorFlags::AllowSetDefaultValue, true };
	if (!InOperationData.Context->AsyncState.bStarted)
	{
		if (PCGUtilsMetadataBase::CVarMetadataOperationReserveValues.GetValueOnAnyThread())
		{
			for (int32 j = 0; j < NbOutputs; ++j)
			{
				// We can't re-use entry keys yet, it can be dangerous in some situations where some points share their entry key.
				InOperationData.OutputAccessors[j]->Prepare(*InOperationData.OutputKeys[j], InOperationData.NumberOfElementsToProcess, /*bCanReuseEntryKeys=*/false);
			}
		}

		PCG::Private::NAryOperation::Operation<InputTypes...>(InOperationData, /*StartIndex=*/0, /*Range=*/1, Options, InCallbacks);
	}

	// If nothing to do now, we can early out.
	if (InOperationData.NumberOfElementsToProcess == 0)
	{
		return true;
	}

	// Then iterate over all the values
	Options.SetFlags = Flags;
	Options.bUseDefaultKey = false;

	const int32 ChunkSize = PCGUtilsMetadataBase::CVarMetadataOperationChunkSize.GetValueOnAnyThread();

	if (PCGUtilsMetadataBase::CVarMetadataOperationInMT.GetValueOnAnyThread())
	{
		return FPCGAsync::AsyncProcessingOneToOneRangeEx(&InOperationData.Context->AsyncState, InOperationData.NumberOfElementsToProcess, []() {},
			[&InOperationData, &InCallbacks, &Options](int32 StartReadIndex, int32 StartWriteIndex, int32 Count)
		{
			PCG::Private::NAryOperation::Operation<InputTypes...>(InOperationData, StartReadIndex, Count, Options, InCallbacks);
			return Count;
		}, /*bEnableTimeSlicing=*/true, ChunkSize);
	}
	else
	{
		const int32 NumberOfIterations = (InOperationData.NumberOfElementsToProcess + PCG::Private::NAryOperation::DefaultChunkSize - 1) / PCG::Private::NAryOperation::DefaultChunkSize;
		for (int32 i = 0; i < NumberOfIterations; ++i)
		{
			int32 StartIndex = i * PCG::Private::NAryOperation::DefaultChunkSize;
			int32 Range = FMath::Min(InOperationData.NumberOfElementsToProcess - StartIndex, PCG::Private::NAryOperation::DefaultChunkSize);
			PCG::Private::NAryOperation::Operation<InputTypes...>(InOperationData, StartIndex, Range, Options, InCallbacks);
		}

		return true;
	}
}

template <typename InType, typename... Callbacks>
inline bool FPCGUtilsMetadataElementBase::DoUnaryOp(PCGMetadataOps::FOperationData& InOperationData, Callbacks&& ...InCallbacks) const
{
	return DoNAryOp<InType>(InOperationData, ForwardAsTuple(std::forward<Callbacks>(InCallbacks)...));
}

template <typename InType1, typename InType2, typename... Callbacks>
inline bool FPCGUtilsMetadataElementBase::DoBinaryOp(PCGMetadataOps::FOperationData& InOperationData, Callbacks&& ...InCallbacks) const
{
	return DoNAryOp<InType1, InType2>(InOperationData, ForwardAsTuple(std::forward<Callbacks>(InCallbacks)...));
}

template <typename InType1, typename InType2, typename InType3, typename... Callbacks>
inline bool FPCGUtilsMetadataElementBase::DoTernaryOp(PCGMetadataOps::FOperationData& InOperationData, Callbacks&& ...InCallbacks) const
{
	return DoNAryOp<InType1, InType2, InType3>(InOperationData, ForwardAsTuple(std::forward<Callbacks>(InCallbacks)...));
}

template <typename InType1, typename InType2, typename InType3, typename InType4, typename... Callbacks>
inline bool FPCGUtilsMetadataElementBase::DoQuaternaryOp(PCGMetadataOps::FOperationData& InOperationData, Callbacks&& ...InCallbacks) const
{
	return DoNAryOp<InType1, InType2, InType3, InType4>(InOperationData, ForwardAsTuple(std::forward<Callbacks>(InCallbacks)...));
}

