// Copyright Max Harris

#include "Factories/PCGUtilsDynMeshProcessBuilderFactory.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshProcessBuilderFactory"

namespace
{
	/**
	 * Evaluation order, mirroring immediate mode exactly:
	 *   build child -> resolve incoming selection -> evaluate Selector -> convert/intersect domains ->
	 *   run the reusable operation -> return a Builder result.
	 */
	class FPCGUtilsDynMeshProcessBuilderDecoratorOperation final : public FPCGUtilsDynMeshBuilderOperation
	{
	public:
		explicit FPCGUtilsDynMeshProcessBuilderDecoratorOperation(
			const UPCGUtilsDynMeshProcessBuilderFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Prepare() override
		{
			if (!Factory || !Factory->ChildBuilder || !Factory->Operation)
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("IncompleteDecorator", "A deferred DynMesh process Builder is missing its child Builder or its process operation."),
					Context);
				return false;
			}

			// Created once per materializer rather than once per seed, so a child leaf keeps its own per-node
			// caches (eg the primitive's reference mesh) across every seed it is evaluated for.
			ChildOperation = Factory->ChildBuilder->CreateOperation(Context);
			return ChildOperation.IsValid();
		}

		virtual bool Build(
			const FPCGUtilsDynMeshBuildContext& BuildContext, FPCGUtilsDynMeshBuildResult& OutResult) const override
		{
			FPCGContext* EvaluationContext = BuildContext.Context ? BuildContext.Context : Context;

			FPCGUtilsDynMeshBuildResult ChildResult;
			if (!ChildOperation->Build(BuildContext, ChildResult) || !ChildResult.IsValid())
			{
				return false;
			}

			const FPCGUtilsDynMeshResolvedInput Resolved = FPCGUtilsDynMeshProcessFunctions::ResolveInput(
				ChildResult.GetProcessInputData(), Factory->Selector, Factory->Policy, EvaluationContext);
			if (!Resolved.IsValid())
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("DeferredResolveFailed", "Deferred DynMesh process '{0}' could not resolve its effective selection."),
					FText::FromName(Factory->ProcessLabel)), EvaluationContext);
				return false;
			}

			// The child's mesh is private to this subtree, so mutating it in place is safe and is exactly the
			// point of the Builder architecture: no deep copy between decorators.
			FPCGUtilsDynMeshProcessInvocation Invocation;
			Invocation.Context = EvaluationContext;
			Invocation.MeshData = const_cast<UPCGDynamicMeshData*>(Resolved.MeshData);
			Invocation.SelectionData = Resolved.SelectionData;
			// Deferred: the subtree's own result is the source. These deliberately alias - see the caution on
			// FPCGUtilsDynMeshProcessInvocation::SourceMeshData.
			Invocation.SourceMeshData = Resolved.MeshData;
			Invocation.bHasBuilderFrame = ChildResult.bHasBuilderFrame;
			Invocation.BuilderFrame = ChildResult.BuilderFrame;

			FPCGUtilsDynMeshProcessOutcome Outcome;
			if (!Factory->Operation->Execute(Invocation, Outcome))
			{
				return false;
			}

			OutResult.SetMeshData(Invocation.MeshData);

			// The frame travels with the subtree, moved by whatever rigid motion the operation reported, so a
			// builder-local transform further up the chain still acts about where the content actually is.
			if (ChildResult.bHasBuilderFrame)
			{
				OutResult.SetBuilderFrame(ChildResult.BuilderFrame);
				OutResult.MoveBuilderFrame(Outcome.GeometryTransform);
			}

			switch (Outcome.SelectionOutcome)
			{
			case EPCGUtilsDynMeshProcessSelectionOutcome::Preserve:
				// Keep whatever selection is effective at this point in the chain, so a Selector established
				// upstream (or by this very node) continues to travel with the geometry.
				OutResult.SetSelectionData(Resolved.SelectionData);
				break;
			case EPCGUtilsDynMeshProcessSelectionOutcome::Replace:
				OutResult.SetSelectionData(Outcome.NewSelectionData);
				break;
			case EPCGUtilsDynMeshProcessSelectionOutcome::Clear:
			default:
				OutResult.ClearSelection();
				break;
			}
			return true;
		}

	private:
		TObjectPtr<const UPCGUtilsDynMeshProcessBuilderFactoryData> Factory;
		TSharedPtr<FPCGUtilsDynMeshBuilderOperation> ChildOperation;
	};
}

TSharedPtr<FPCGUtilsDynMeshBuilderOperation> UPCGUtilsDynMeshProcessBuilderFactoryData::CreateOperationInternal() const
{
	return MakeShared<FPCGUtilsDynMeshProcessBuilderDecoratorOperation>(this);
}

void UPCGUtilsDynMeshProcessBuilderFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	// The base folds in DataDependencies, which the authoring element populates with the child Builder and the
	// Selector - so the whole deferred chain's identity comes from the existing factory dependency
	// infrastructure rather than a disconnected cache-key system invented here.
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	uint32 ConfigCrc = OperationConfigCrc;
	Ar << ConfigCrc;

	uint8 PolicyRequiresSelection = Policy.bRequiresSelection ? 1 : 0;
	uint8 PolicyRequiresDomain = Policy.bRequiresSpecificDomain ? 1 : 0;
	uint8 PolicyRequiredDomain = static_cast<uint8>(Policy.RequiredDomain);
	uint8 PolicyPartialInclusion = Policy.bAllowPartialDomainInclusion ? 1 : 0;
	uint8 PolicySelectorDomain = static_cast<uint8>(Policy.SelectorEvaluationDomain);
	Ar << PolicyRequiresSelection << PolicyRequiresDomain << PolicyRequiredDomain
		<< PolicyPartialInclusion << PolicySelectorDomain;
}

#undef LOCTEXT_NAMESPACE
