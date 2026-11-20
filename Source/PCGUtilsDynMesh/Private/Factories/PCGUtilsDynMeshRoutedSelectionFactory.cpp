// Copyright Max Harris

#include "Factories/PCGUtilsDynMeshRoutedSelectionFactory.h"
#include "Data/PCGDynamicMeshData.h"
#include "Metadata/PCGMetadata.h"
#include "Serialization/ArchiveCrc32.h"

namespace
{
	class FDynMeshRoutedSelectionOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FDynMeshRoutedSelectionOperation(const UPCGUtilsDynMeshRoutedSelectionFactoryData* InData) : Data(InData) {}
		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& Evaluation) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(Evaluation) || !Data) { return false; }
			Operations.Reset();
			int32 BestDistance = MAX_int32;
			for (const auto& Route : Data->Routes)
			{
				const int32 Distance = PCGUtilsDynMeshSelectionRouting::GetMatchDistance(Evaluation.MeshData, Route.Mesh);
				if (Distance != INDEX_NONE) { BestDistance = FMath::Min(BestDistance, Distance); }
			}
			for (const auto& Route : Data->Routes)
			{
				if (PCGUtilsDynMeshSelectionRouting::GetMatchDistance(Evaluation.MeshData, Route.Mesh) != BestDistance) { continue; }
				if (!Route.Selector || !Route.Selector->SupportsDomain(Evaluation.Domain)) { return false; }
				auto Operation = Route.Selector->CreateOperation(Context);
				if (!Operation || !Operation->Initialize(Evaluation)) { return false; }
				Operations.Add(MoveTemp(Operation));
			}
			return true; // No matching route is an explicit empty selection, never a select-all fallback.
		}
		virtual bool TestElement(int32 ElementID) const override
		{
			for (const auto& Operation : Operations) { if (Operation->TestElement(ElementID)) { return true; } }
			return false;
		}
	private:
		TObjectPtr<const UPCGUtilsDynMeshRoutedSelectionFactoryData> Data;
		TArray<TSharedPtr<FPCGUtilsDynMeshSelectionOperation>> Operations;
	};
}

int32 PCGUtilsDynMeshSelectionRouting::GetMatchDistance(const UPCGDynamicMeshData* Target, const UPCGDynamicMeshData* Reference)
{
	if (!Target || !Reference) { return INDEX_NONE; }
	if (Target == Reference) { return 0; }
	const UPCGMetadata* ReferenceMetadata = Reference->ConstMetadata();
	int32 Distance = 0;
	for (const UPCGMetadata* Metadata = Target->ConstMetadata(); Metadata; Metadata = Metadata->GetParent(), ++Distance)
	{
		if (Metadata == ReferenceMetadata) { return Distance; }
	}
	return INDEX_NONE;
}

bool UPCGUtilsDynMeshRoutedSelectionFactoryData::SupportsDomain(const FPCGUtilsDynMeshSelectionDomain& Domain) const
{
	for (const auto& Route : Routes) { if (!Route.Selector || !Route.Selector->SupportsDomain(Domain)) { return false; } }
	return Super::SupportsDomain(Domain);
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation> UPCGUtilsDynMeshRoutedSelectionFactoryData::CreateOperationInternal() const
{
	return MakeShared<FDynMeshRoutedSelectionOperation>(this);
}

void UPCGUtilsDynMeshRoutedSelectionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc) { return; }
	for (const auto& Route : Routes)
	{
		// Geometry CRC alone aliases identical meshes with different result memberships. Identity matters.
		uint32 Identity = Route.Mesh ? Route.Mesh->GetUniqueID() : 0;
		uint32 SelectorCrc = Route.Selector ? Route.Selector->GetOrComputeCrc(true).GetValue() : 0;
		Ar << Identity << SelectorCrc;
	}
}
