// Copyright Max Harris

#include "Factories/PCGUtilsDynMeshOperandProcessBuilderFactory.h"

#include "Data/PCGDynamicMeshData.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshOperandProcessBuilder"

namespace
{
	class FOperandProcessBuilderOperation final : public FPCGUtilsDynMeshBuilderOperation
	{
	public:
		explicit FOperandProcessBuilderOperation(const UPCGUtilsDynMeshOperandProcessBuilderFactoryData* InSource)
			: Source(InSource) {}

		virtual bool Prepare() override
		{
			if (!Source->PrimaryBuilder || !Source->OperandBuilder || !Source->Operation)
			{
				PCGLog::LogErrorOnGraph(LOCTEXT("IncompleteExpression",
					"A deferred DynMesh operand process is missing a child Builder or its operation."), Context);
				return false;
			}
			Primary = Source->PrimaryBuilder->CreateOperation(Context);
			Operand = Source->OperandBuilder->CreateOperation(Context);
			return Primary.IsValid() && Operand.IsValid();
		}

		virtual bool Build(const FPCGUtilsDynMeshBuildContext& BuildContext,
			FPCGUtilsDynMeshBuildResult& OutResult) const override
		{
			FPCGUtilsDynMeshBuildResult A, B;
			if (!Primary->Build(BuildContext, A) || !A.IsValid() ||
				!Operand->Build(BuildContext, B) || !B.IsValid())
			{
				// A failed evaluation is not a missing input. Do not hide malformed Builder expressions.
				return false;
			}
			FPCGContext* EvaluationContext = BuildContext.Context ? BuildContext.Context : Context;
			const FPCGUtilsDynMeshProcessSelectionPolicy WholeMeshPolicy;
			const FPCGUtilsDynMeshResolvedInput ResolvedA = FPCGUtilsDynMeshProcessFunctions::ResolveInput(
				A.MeshData, nullptr, WholeMeshPolicy, EvaluationContext);
			const FPCGUtilsDynMeshResolvedInput ResolvedB = FPCGUtilsDynMeshProcessFunctions::ResolveInput(
				B.MeshData, nullptr, WholeMeshPolicy, EvaluationContext);
			if (!ResolvedA.IsValid() || !ResolvedB.IsValid())
			{
				return false;
			}

			FPCGUtilsDynMeshProcessInvocation Invocation;
			Invocation.Context = EvaluationContext;
			Invocation.MeshData = A.MeshData;
			Invocation.SourceMeshData = A.MeshData;
			Invocation.OperandMeshData = ResolvedB.MeshData;
			Invocation.bHasBuilderFrame = A.bHasBuilderFrame;
			Invocation.BuilderFrame = A.BuilderFrame;
			FPCGUtilsDynMeshProcessOutcome Outcome;
			if (!Source->Operation->Execute(Invocation, Outcome))
			{
				return false;
			}
			OutResult = A;
			OutResult.SetMeshData(Invocation.MeshData);
			OutResult.ClearSelection();
			OutResult.MoveBuilderFrame(Outcome.GeometryTransform);
			return true;
		}

	private:
		TObjectPtr<const UPCGUtilsDynMeshOperandProcessBuilderFactoryData> Source;
		TSharedPtr<FPCGUtilsDynMeshBuilderOperation> Primary, Operand;
	};
}

#undef LOCTEXT_NAMESPACE

TSharedPtr<FPCGUtilsDynMeshBuilderOperation> UPCGUtilsDynMeshOperandProcessBuilderFactoryData::CreateOperationInternal() const
{
	return MakeShared<FOperandProcessBuilderOperation>(this);
}

void UPCGUtilsDynMeshOperandProcessBuilderFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		// Dependencies are an unordered set. Include the ordered roles so A-B and B-A cannot alias.
		uint32 PrimaryCrc = PrimaryBuilder ? PrimaryBuilder->GetOrComputeCrc(true).GetValue() : 0;
		uint32 OperandCrc = OperandBuilder ? OperandBuilder->GetOrComputeCrc(true).GetValue() : 0;
		uint32 ConfigCrc = OperationConfigCrc;
		Ar << PrimaryCrc << OperandCrc << ConfigCrc;
	}
}
