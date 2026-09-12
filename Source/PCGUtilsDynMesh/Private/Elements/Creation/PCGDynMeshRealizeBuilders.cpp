// Copyright Max Harris

#include "Elements/Creation/PCGDynMeshRealizeBuilders.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Factories/PCGUtilsDynMeshBuilderFactory.h"
#include "Factories/PCGUtilsDynMeshFactories.h"
#include "GameFramework/Actor.h"
#include "GeometryScript/MeshBasicEditFunctions.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshRealizeBuilders"

namespace
{
	/**
	 * Rebases a freshly-built Builder result's material IDs onto the combined output material array. A no-op
	 * for the usual case of one Builder, or of Builders that carry no materials at all.
	 */
	void ShiftMaterialIDs(UDynamicMesh* Mesh, int32 Offset)
	{
		if (Offset == 0 || !Mesh)
		{
			return;
		}
		Mesh->EditMesh([Offset](UE::Geometry::FDynamicMesh3& EditMesh)
		{
			if (!EditMesh.HasAttributes() || !EditMesh.Attributes()->HasMaterialID())
			{
				return;
			}
			UE::Geometry::FDynamicMeshMaterialAttribute* MaterialIDs = EditMesh.Attributes()->GetMaterialID();
			for (const int32 TriangleID : EditMesh.TriangleIndicesItr())
			{
				int32 MaterialID = 0;
				MaterialIDs->GetValue(TriangleID, &MaterialID);
				MaterialIDs->SetValue(TriangleID, MaterialID + Offset);
			}
		});
	}

	/** A realized mesh represents geometry, not seed points, so only the dataset-wide @Data domain transfers. */
	void InheritSeedDataDomain(UPCGDynamicMeshData* OutputData, const UPCGSpatialData* SeedData)
	{
		if (!OutputData || !SeedData || !OutputData->MutableMetadata() || !SeedData->ConstMetadata())
		{
			return;
		}

		const FPCGMetadataDomain* SourceDomain =
			SeedData->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Data);
		FPCGMetadataDomain* OutputDomain =
			OutputData->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Data);
		if (SourceDomain && OutputDomain)
		{
			OutputDomain->Initialize(FPCGMetadataDomainInitializeParams(SourceDomain));
		}
	}
}

bool PCGUtilsDynMeshBuilderRealization::Realize(
	FPCGContext* Context,
	bool bConvertSeedsToLocalSpace,
	EPCGUtilsDynMeshBuilderOutputMode OutputMode,
	const FText& NodeNameForMessages)
{
	TArray<TObjectPtr<const UPCGUtilsDynMeshBuilderFactoryData>> Factories;
	if (!PCGUtilsDynMeshFactories::GetInputFactories(
		Context, PCGUtilsDynMeshBuilderFactoryConstants::BuildersInputPin, Factories,
		PCGUtilsDynMeshFactories::GetBuilderFactoryTypes(), /*bRequired=*/true))
	{
		return true;
	}

	// This is the materialization point: every connected Builder expression is evaluated here, once per seed.
	// Nothing upstream of this node has touched geometry.
	TArray<TSharedPtr<FPCGUtilsDynMeshBuilderOperation>> Operations;
	Operations.Reserve(Factories.Num());
	for (const UPCGUtilsDynMeshBuilderFactoryData* Factory : Factories)
	{
		TSharedPtr<FPCGUtilsDynMeshBuilderOperation> Operation = Factory->CreateOperation(Context);
		if (!Operation)
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("BuilderOperationFailed",
				"{0} could not create an operation from one of its Builder inputs."), NodeNameForMessages), Context);
			return true;
		}
		Operations.Add(MoveTemp(Operation));
	}

	const bool bSplitByBuilder =
		OutputMode == EPCGUtilsDynMeshBuilderOutputMode::PerBuilder ||
		OutputMode == EPCGUtilsDynMeshBuilderOutputMode::PerBuilderPerSeed;

	FTransform ActorTransform = FTransform::Identity;
	if (bConvertSeedsToLocalSpace)
	{
		if (const AActor* TargetActor = Context->GetTargetActor(nullptr))
		{
			ActorTransform = TargetActor->GetActorTransform();
		}
		else
		{
			PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("MissingTargetActor",
				"{0} could not resolve a target actor; seed positions remain in their original space."),
				NodeNameForMessages), Context);
		}
	}

	auto EmitMesh = [Context](UDynamicMesh* Mesh, const TArray<UMaterialInterface*>& Materials,
		const FPCGTaggedData* SourceSeedInput, const UPCGSpatialData* SeedMetadataSource)
	{
		UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
		OutputData->Initialize(Mesh, /*bCanTakeOwnership=*/true, Materials);
		InheritSeedDataDomain(OutputData, SeedMetadataSource);
		FPCGTaggedData& Output = SourceSeedInput
			? Context->OutputData.TaggedData.Emplace_GetRef(*SourceSeedInput)
			: Context->OutputData.TaggedData.Emplace_GetRef();
		Output.Data = OutputData;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	};

	// When several Builders share one output mesh, their material IDs must be rebased onto a concatenated
	// array. When the output is split per Builder that never arises, so each output just carries its own
	// Builder's materials verbatim.
	TArray<UMaterialInterface*> ComposedMaterials;
	TArray<int32> ComposedMaterialOffsets;
	ComposedMaterialOffsets.Init(0, Operations.Num());
	TArray<TArray<UMaterialInterface*>> PerBuilderMaterials;
	PerBuilderMaterials.SetNum(Operations.Num());
	bool bMaterialLayoutResolved = false;

	// Accumulators for the modes that span seeds.
	UDynamicMesh* SingleTarget = (OutputMode == EPCGUtilsDynMeshBuilderOutputMode::Single)
		? FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context) : nullptr;
	TArray<UDynamicMesh*> PerBuilderTargets;
	if (OutputMode == EPCGUtilsDynMeshBuilderOutputMode::PerBuilder)
	{
		PerBuilderTargets.Reserve(Operations.Num());
		for (int32 Index = 0; Index < Operations.Num(); ++Index)
		{
			PerBuilderTargets.Add(FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context));
		}
	}

	int32 SeedCount = 0;
	const UPCGSpatialData* AggregateSeedMetadataSource = nullptr;
	bool bAggregateSeedMetadataIsAmbiguous = false;

	for (const FPCGTaggedData& SeedInput :
		Context->InputData.GetInputsByPin(PCGDynMeshRealizeBuildersConstants::SeedsPin))
	{
		const UPCGBasePointData* SeedPointData = Cast<const UPCGBasePointData>(SeedInput.Data);
		if (!SeedPointData)
		{
			PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("InvalidSeedInput",
				"{0} skipped a Seeds input that was not Point Data."), NodeNameForMessages), Context);
			continue;
		}

		if (!AggregateSeedMetadataSource)
		{
			AggregateSeedMetadataSource = SeedPointData;
		}
		else if (AggregateSeedMetadataSource != SeedPointData)
		{
			bAggregateSeedMetadataIsAmbiguous = true;
		}

		const int32 NumPoints = SeedPointData->GetNumPoints();
		const auto TransformRange = SeedPointData->GetConstTransformValueRange();
		const auto BoundsMinRange = SeedPointData->GetConstBoundsMinValueRange();
		const auto BoundsMaxRange = SeedPointData->GetConstBoundsMaxValueRange();

		for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
		{
			FPCGUtilsDynMeshBuildContext BuildContext;
			BuildContext.Context = Context;
			BuildContext.SeedTransform = bConvertSeedsToLocalSpace
				? TransformRange[PointIndex].GetRelativeTransform(ActorTransform)
				: TransformRange[PointIndex];
			BuildContext.SeedLocalBounds = FBox(BoundsMinRange[PointIndex], BoundsMaxRange[PointIndex]);
			BuildContext.SeedData = SeedPointData;
			BuildContext.SeedIndex = PointIndex;

			++SeedCount;

			UDynamicMesh* SeedTarget = (OutputMode == EPCGUtilsDynMeshBuilderOutputMode::PerSeed)
				? FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context) : nullptr;

			for (int32 OperationIndex = 0; OperationIndex < Operations.Num(); ++OperationIndex)
			{
				FPCGUtilsDynMeshBuildResult BuildResult;
				if (!Operations[OperationIndex]->Build(BuildContext, BuildResult) || !BuildResult.IsValid())
				{
					continue;
				}

				if (!bMaterialLayoutResolved)
				{
					ComposedMaterialOffsets[OperationIndex] = ComposedMaterials.Num();
					for (UMaterialInterface* Material : BuildResult.MeshData->GetMaterials())
					{
						ComposedMaterials.Add(Material);
						PerBuilderMaterials[OperationIndex].Add(Material);
					}
				}

				UDynamicMesh* SeedMesh = BuildResult.MeshData->GetMutableDynamicMesh();

				if (OutputMode == EPCGUtilsDynMeshBuilderOutputMode::PerBuilderPerSeed)
				{
					// The Builder's result already *is* this output; no append or ID rebasing needed.
					EmitMesh(SeedMesh, PerBuilderMaterials[OperationIndex], &SeedInput, SeedPointData);
					continue;
				}

				UDynamicMesh* Target = SeedTarget ? SeedTarget
					: (SingleTarget ? SingleTarget : PerBuilderTargets[OperationIndex]);

				if (!bSplitByBuilder)
				{
					ShiftMaterialIDs(SeedMesh, ComposedMaterialOffsets[OperationIndex]);
				}
				UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMeshTransformed(
					Target, SeedMesh, TArray<FTransform>{FTransform::Identity}, FTransform::Identity);
			}

			bMaterialLayoutResolved = true;

			if (SeedTarget)
			{
				EmitMesh(SeedTarget, ComposedMaterials, &SeedInput, SeedPointData);
			}
		}
	}

	if (SeedCount == 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("NoSeeds",
			"{0} received no seed points."), NodeNameForMessages), Context);
	}

	if (SingleTarget)
	{
		EmitMesh(SingleTarget, ComposedMaterials, /*SourceSeedInput=*/nullptr,
			bAggregateSeedMetadataIsAmbiguous ? nullptr : AggregateSeedMetadataSource);
	}
	for (int32 Index = 0; Index < PerBuilderTargets.Num(); ++Index)
	{
		// Spans every seed, so no single seed input's tags apply.
		EmitMesh(PerBuilderTargets[Index], PerBuilderMaterials[Index], /*SourceSeedInput=*/nullptr,
			bAggregateSeedMetadataIsAmbiguous ? nullptr : AggregateSeedMetadataSource);
	}

	return true;
}

#if WITH_EDITOR
FText UPCGDynMeshRealizeBuildersSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "DynMesh | Realize Builders");
}

FText UPCGDynMeshRealizeBuildersSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Evaluates every connected Builder once per seed point and outputs the resulting geometry as DynMesh "
		"data. This is where a Builder chain stops being a description of a shape and becomes triangles. "
		"Several Builders on the pin compose one shape per seed; Output Mode decides how the results are "
		"split across output data.");
}
#endif

TArray<FPCGPinProperties> UPCGDynMeshRealizeBuildersSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGDynMeshRealizeBuildersConstants::SeedsPin,
		EPCGDataType::Point, true, true).SetRequiredPin();
	// Multiple connections: every Builder on this pin is evaluated for every seed and appended, which is
	// how a compound shape (column, frame, window) is expressed.
	Pins.Emplace_GetRef(
		PCGUtilsDynMeshBuilderFactoryConstants::BuildersInputPin,
		FPCGUtilsDynMeshBuilderFactoryDataTypeInfo::AsId(), true, true).SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGDynMeshRealizeBuildersSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, true, true)};
}

FPCGElementPtr UPCGDynMeshRealizeBuildersSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshRealizeBuildersElement>();
}

bool FPCGDynMeshRealizeBuildersElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDynMeshRealizeBuildersElement::ExecuteInternal);
	check(Context);

	const UPCGDynMeshRealizeBuildersSettings* Settings =
		Context->GetInputSettings<UPCGDynMeshRealizeBuildersSettings>();
	check(Settings);

	return PCGUtilsDynMeshBuilderRealization::Realize(
		Context, Settings->bConvertSeedsToLocalSpace, Settings->OutputMode,
		LOCTEXT("NodeName", "Realize Builders"));
}

#undef LOCTEXT_NAMESPACE
