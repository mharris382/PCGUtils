// Copyright Max Harris

#include "Elements/Selections/PCGDynMeshFaceAngleSelection.h"

#include "Data/PCGDynamicMeshData.h"
#include "Geometry/PCGUtilsDynMeshFaceAngles.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshFaceAngleSelection"

namespace
{
	class FFaceAngleSelectionOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FFaceAngleSelectionOperation(const FPCGUtilsDynMeshDotComparison& InComparison) : Comparison(InComparison) {}
		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& Evaluation) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(Evaluation)) { return false; }
			FText Error;
			if (!PCGUtilsDynMeshFaceAngles::ResolveComparison(Comparison, Evaluation.MeshData, ThresholdDot, Error))
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("ThresholdError", "Select By Face Angle: {0}"), Error), Context);
				return false;
			}
			return true;
		}
		virtual bool TestElement(int32 EdgeID) const override
		{ return PCGUtilsDynMeshFaceAngles::TestEdge(SelectionContext->Mesh, EdgeID, Comparison, ThresholdDot); }
	private:
		FPCGUtilsDynMeshDotComparison Comparison;
		double ThresholdDot = 1.0;
	};
}

UPCGDynMeshFaceAngleSelectionFactoryData::UPCGDynMeshFaceAngleSelectionFactoryData()
	: Comparison(PCGUtilsDynMeshFaceAngles::MakeDefaultComparison()) {}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation> UPCGDynMeshFaceAngleSelectionFactoryData::CreateNativeOperationInternal() const
{ return MakeShared<FFaceAngleSelectionOperation>(Comparison); }

void UPCGDynMeshFaceAngleSelectionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc) { Comparison.AddToCrc(Ar); }
}

UPCGDynMeshFaceAngleSelectionSettings::UPCGDynMeshFaceAngleSelectionSettings()
	: Comparison(PCGUtilsDynMeshFaceAngles::MakeDefaultComparison())
{
	SelectionElementType = EPCGUtilsDynMeshSelectionElementType::Edge;
}

#if WITH_EDITOR
FText UPCGDynMeshFaceAngleSelectionSettings::GetDefaultNodeTitle() const
{ return LOCTEXT("Title", "Select | By Face Angle"); }

FText UPCGDynMeshFaceAngleSelectionSettings::GetNodeTooltipText() const
{ return LOCTEXT("Tooltip", "Selects edges by comparing the geometric normals of their two adjacent faces, using the same comparison and preview as Select By Normal. The default keeps unsigned face-normal differences of at least 45 degrees. Excludes boundary edges and degenerate faces. Supports reusable Selector and materialized Selection modes, with automatic vertex/face conversion."); }
#endif

UPCGUtilsDynMeshFactoryData* UPCGDynMeshFaceAngleSelectionSettings::CreateFactory(FPCGContext* Context, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	auto* Factory = InFactory ? Cast<UPCGDynMeshFaceAngleSelectionFactoryData>(InFactory)
		: PCGUtilsAsyncObjects::NewObject<UPCGDynMeshFaceAngleSelectionFactoryData>(Context);
	if (!Factory) { return nullptr; }
	Factory->Comparison = Comparison;
	Factory->Priority = Priority;
	return Super::CreateFactory(Context, Factory);
}

#undef LOCTEXT_NAMESPACE
