// Copyright Max Harris
#include "Elements/Selections/PCGDynMeshIssuesSelection.h"
#include "Data/PCGDynamicMeshData.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "Serialization/ArchiveCrc32.h"
#include "UObject/UnrealType.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshIssuesSelection"
namespace PCGUtilsDynMeshIssuesSelection
{
	class FOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FOperation(const FPCGUtilsDynMeshIssueChecks& InChecks) : Checks(InChecks) {}
		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& Evaluation) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(Evaluation)) { return false; }
			PCGUtilsDynMeshRepair::FChecks Resolved;
			FText Error;
			if (!PCGUtilsDynMeshRepair::ResolveChecks(Checks, Evaluation.MeshData, Resolved, Error))
			{
				PCGLog::LogErrorOnGraph(Error, Context);
				return false;
			}
			Triangles = PCGUtilsDynMeshRepair::FindIssues(Evaluation.Mesh, Resolved).Triangles;
			return true;
		}
		virtual bool TestElement(int32 ID) const override { return Triangles.Contains(ID); }
	private:
		FPCGUtilsDynMeshIssueChecks Checks;
		TSet<int32> Triangles;
	};
}

UPCGDynMeshIssuesSelectionSettings::UPCGDynMeshIssuesSelectionSettings()
{
	SelectionElementType = EPCGUtilsDynMeshSelectionElementType::Triangle;
	bSupportsMaterializedElementTypeOverride = false;
}
#if WITH_EDITOR
FText UPCGDynMeshIssuesSelectionSettings::GetDefaultNodeTitle() const { return LOCTEXT("Title", "Select | Mesh Issues"); }
FText UPCGDynMeshIssuesSelectionSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Find crossing triangles, coplanar overlaps, collapsed faces and optionally thin slivers. Both faces of each intersection are selected. Shared boundary contact is excluded. Emits a reusable Selector or an immediate Selection; never modifies the mesh.");
}
#endif
TSharedPtr<FPCGUtilsDynMeshSelectionOperation> UPCGDynMeshIssuesSelectionFactoryData::CreateNativeOperationInternal() const
{
	return MakeShared<PCGUtilsDynMeshIssuesSelection::FOperation>(Checks);
}
void UPCGDynMeshIssuesSelectionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc) { FPCGUtilsDynMeshIssueChecks::StaticStruct()->SerializeItem(Ar, const_cast<FPCGUtilsDynMeshIssueChecks*>(&Checks), nullptr); }
}
UPCGUtilsDynMeshFactoryData* UPCGDynMeshIssuesSelectionSettings::CreateFactory(FPCGContext* Context, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	auto* Factory = InFactory ? Cast<UPCGDynMeshIssuesSelectionFactoryData>(InFactory) : PCGUtilsAsyncObjects::NewObject<UPCGDynMeshIssuesSelectionFactoryData>(Context);
	if (!Factory) { return nullptr; }
	Factory->Checks = Checks;
	return Super::CreateFactory(Context, Factory);
}
#undef LOCTEXT_NAMESPACE
