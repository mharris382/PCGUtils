// Copyright Max Harris

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGPointArrayData.h"
#include "PCGContext.h"
#include "PCGSettings.h"
#include "Selectors/PCGUtilsInBoundsPointSelector.h"
#include "Selectors/PCGUtilsPointSelectorWrappers.h"

/**
 * Fixtures for the PCGUtilsSelectors tests. Local to this module on purpose: the domain modules' own test helpers
 * are private to them, and the little that is needed here (one box point, running one element) is cheaper to
 * repeat than to publish from three modules.
 */
namespace PCGUtilsSelectorsTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Point data with one point: a box by centre and half size, optionally turned. */
	inline UPCGPointArrayData* MakeBoxPoint(const FVector& Center, const FVector& HalfSize, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		UPCGPointArrayData* Points = NewObject<UPCGPointArrayData>();
		Points->SetNumPoints(1);
		Points->AllocateProperties(EPCGPointNativeProperties::All);
		Points->GetTransformValueRange()[0] = FTransform(Rotation, Center);
		Points->GetBoundsMinValueRange()[0] = -HalfSize;
		Points->GetBoundsMaxValueRange()[0] = HalfSize;
		return Points;
	}

	/** The In Bounds point selector over one box, prepared as its node would prepare it. */
	inline UPCGUtilsInBoundsPointSelectorData* MakeInBounds(const FVector& Center, const FVector& HalfSize,
		EPCGUtilsSelectorContainment Containment, double Expand = 0.0)
	{
		UPCGUtilsInBoundsPointSelectorData* Selector = NewObject<UPCGUtilsInBoundsPointSelectorData>();
		UPCGPointArrayData* Points = MakeBoxPoint(Center, HalfSize);
		Selector->PointData.Add(Points);
		Selector->AddDataDependency(Points);
		Selector->Containment = Containment;
		Selector->Expand = Expand;
		Selector->Prepare(nullptr);
		return Selector;
	}

	inline UPCGUtilsDynMeshPointSelectorData* WrapForDynMesh(const UPCGUtilsPointSelectorData* Selector,
		const FPCGUtilsDynMeshPointConversion& Conversion = FPCGUtilsDynMeshPointConversion(), bool bInvert = false)
	{
		UPCGUtilsDynMeshPointSelectorData* Wrapper = NewObject<UPCGUtilsDynMeshPointSelectorData>();
		Wrapper->PointSelector = Selector;
		Wrapper->Conversion = Conversion;
		Wrapper->bInvertSelection = bInvert;
		return Wrapper;
	}

	inline UPCGUtilsGeometryCollectionPointSelectorData* WrapForGC(const UPCGUtilsPointSelectorData* Selector,
		const FPCGUtilsGeometryCollectionPointConversion& Conversion = FPCGUtilsGeometryCollectionPointConversion(), bool bInvert = false)
	{
		UPCGUtilsGeometryCollectionPointSelectorData* Wrapper = NewObject<UPCGUtilsGeometryCollectionPointSelectorData>();
		Wrapper->PointSelector = Selector;
		Wrapper->Conversion = Conversion;
		Wrapper->bInvertSelection = bInvert;
		return Wrapper;
	}

	inline UPCGVoxelPointSelectorData* WrapForVoxel(const UPCGUtilsPointSelectorData* Selector,
		const FPCGVoxelPointConversion& Conversion = FPCGVoxelPointConversion(), bool bInvert = false)
	{
		UPCGVoxelPointSelectorData* Wrapper = NewObject<UPCGVoxelPointSelectorData>();
		Wrapper->PointSelector = Selector;
		Wrapper->Conversion = Conversion;
		Wrapper->bInvert = bInvert;
		return Wrapper;
	}

	/** Runs one element to completion outside a graph and returns its outputs. */
	inline TArray<FPCGTaggedData> Run(UPCGSettings* Settings, const TArray<TPair<FName, const UPCGData*>>& Inputs)
	{
		FPCGDataCollection Input;
		for (const TPair<FName, const UPCGData*>& Pair : Inputs)
		{
			FPCGTaggedData& Tagged = Input.TaggedData.Emplace_GetRef();
			Tagged.Pin = Pair.Key;
			Tagged.Data = Pair.Value;
		}
		Input.TaggedData.Emplace_GetRef().Data = Settings;
		const FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 32; ++Iteration)
		{
			if (Element->Execute(Context.Get()))
			{
				return Context->OutputData.TaggedData;
			}
		}
		return {};
	}

	template<typename T>
	const T* FirstOutput(const TArray<FPCGTaggedData>& Outputs)
	{
		for (const FPCGTaggedData& Tagged : Outputs)
		{
			if (const T* Typed = Cast<const T>(Tagged.Data))
			{
				return Typed;
			}
		}
		return nullptr;
	}
}

#endif // WITH_AUTOMATION_TESTS
