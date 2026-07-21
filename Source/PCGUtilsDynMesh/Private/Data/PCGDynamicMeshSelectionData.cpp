#include "Data/PCGDynamicMeshSelectionData.h"

#include "Data/PCGDynamicMeshData.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"

PCG_DEFINE_TYPE_INFO(FPCGDataTypeInfoDynamicMeshSelection, UPCGDynamicMeshSelectionData)

void UPCGDynamicMeshSelectionData::Initialize(
	const UPCGDynamicMeshData* InSourceMesh,
	UE::Geometry::FGeometrySelection InSelection)
{
	check(InSelection.ElementType == UE::Geometry::EGeometryElementType::Face);
	check(InSelection.TopologyType == UE::Geometry::EGeometryTopologyType::Triangle);
	SourceMeshData = const_cast<UPCGDynamicMeshData*>(InSourceMesh);
	Selection = MoveTemp(InSelection);
}

void UPCGDynamicMeshSelectionData::VisitDataNetwork(TFunctionRef<void(const UPCGData*)> Action) const
{
	Action(this);
	if (SourceMeshData)
	{
		SourceMeshData->VisitDataNetwork(Action);
	}
}

UPCGData* UPCGDynamicMeshSelectionData::DuplicateData(FPCGContext* Context, bool bInitializeMetadata) const
{
	UPCGDynamicMeshSelectionData* Copy = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshSelectionData>(Context);
	Copy->SourceMeshData = SourceMeshData;
	Copy->Selection = Selection;
	return Copy;
}

void UPCGDynamicMeshSelectionData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		AddUIDToCrc(Ar);
		return;
	}

	uint32 SourceCrc = SourceMeshData ? SourceMeshData->GetOrComputeCrc(true).GetValue() : 0;
	Ar << SourceCrc;
	uint8 ElementType = static_cast<uint8>(Selection.ElementType);
	uint8 TopologyType = static_cast<uint8>(Selection.TopologyType);
	Ar << ElementType;
	Ar << TopologyType;
	TArray<uint64> SortedSelection = Selection.Selection.Array();
	SortedSelection.Sort();
	Ar << SortedSelection;
}
