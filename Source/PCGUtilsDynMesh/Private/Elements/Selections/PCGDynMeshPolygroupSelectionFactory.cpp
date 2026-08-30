// Copyright Max Harris

#include "Elements/Selections/PCGDynMeshPolygroupSelectionFactory.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshPolygroupSelectionFactory"

namespace
{
	class FPolygroupSelectionOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FPolygroupSelectionOperation(const UPCGDynMeshPolygroupSelectionFactoryData& Data)
			: GroupLayer(Data.GroupLayer), SelectionMode(Data.SelectionMode), bInvertSelection(Data.bInvertSelection)
		{
			for (int32 ID : Data.GroupIDs)
			{
				if (ID >= 0) { GroupIDs.Add(ID); }
			}
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InContext)) { return false; }
			GroupAttribute = nullptr;
			HighestGroupID = INDEX_NONE;
			const UE::Geometry::FDynamicMesh3& Mesh = InContext.Mesh;
			if (GroupLayer.bDefaultLayer)
			{
				if (!Mesh.HasTriangleGroups())
				{
					PCGLog::LogErrorOnGraph(LOCTEXT("MissingDefaultLayer",
						"Select by PolyGroup requires a mesh with default triangle groups."), Context);
					return false;
				}
			}
			else
			{
				if (!Mesh.HasAttributes() || GroupLayer.ExtendedLayerIndex < 0 ||
					GroupLayer.ExtendedLayerIndex >= Mesh.Attributes()->NumPolygroupLayers())
				{
					PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("MissingExtendedLayer",
						"Select by PolyGroup could not find extended PolyGroup layer {0}."),
						FText::AsNumber(GroupLayer.ExtendedLayerIndex)), Context);
					return false;
				}
				GroupAttribute = Mesh.Attributes()->GetPolygroupLayer(GroupLayer.ExtendedLayerIndex);
			}
			if (SelectionMode == EPCGUtilsDynMeshPolygroupSelectionMode::HighestGroupID)
			{
				// The allocation counter may include deleted groups. Only existing triangle values matter.
				for (int32 TriangleID : Mesh.TriangleIndicesItr())
				{
					HighestGroupID = FMath::Max(HighestGroupID, GetGroupID(TriangleID));
				}
			}
			return true;
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			if (!SelectionContext->Mesh.IsTriangle(ElementID)) { return false; }
			const int32 GroupID = GetGroupID(ElementID);
			const bool bMatches = SelectionMode == EPCGUtilsDynMeshPolygroupSelectionMode::HighestGroupID
				? HighestGroupID >= 0 && GroupID == HighestGroupID : GroupIDs.Contains(GroupID);
			return bInvertSelection ? !bMatches : bMatches;
		}

	private:
		int32 GetGroupID(int32 TriangleID) const
		{
			return GroupAttribute ? GroupAttribute->GetValue(TriangleID) : SelectionContext->Mesh.GetTriangleGroup(TriangleID);
		}

		FGeometryScriptGroupLayer GroupLayer;
		EPCGUtilsDynMeshPolygroupSelectionMode SelectionMode;
		bool bInvertSelection;
		TSet<int32> GroupIDs;
		const UE::Geometry::FDynamicMeshPolygroupAttribute* GroupAttribute = nullptr;
		int32 HighestGroupID = INDEX_NONE;
	};
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation> UPCGDynMeshPolygroupSelectionFactoryData::CreateNativeOperationInternal() const
{
	return MakeShared<FPolygroupSelectionOperation>(*this);
}

void UPCGDynMeshPolygroupSelectionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		bool bDefaultLayer = GroupLayer.bDefaultLayer;
		int32 LayerIndex = GroupLayer.ExtendedLayerIndex;
		uint8 Mode = static_cast<uint8>(SelectionMode);
		TArray<int32> IDs = GroupIDs;
		bool bInvert = bInvertSelection;
		Ar << bDefaultLayer << LayerIndex << Mode << IDs << bInvert;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshPolygroupSelectionFactoryProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select by PolyGroup");
}

TArray<FText> UPCGDynMeshPolygroupSelectionFactoryProviderSettings::GetNodeTitleAliases() const
{
	return { LOCTEXT("Alias", "PolyGroup Selector"), LOCTEXT("DynMeshAlias", "DynMesh PolyGroup Selector") };
}

FText UPCGDynMeshPolygroupSelectionFactoryProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Creates a reusable PolyGroup Selector for explicit group IDs or the highest ID used by triangles in a layer. Selects faces and automatically converts to vertices or edges when requested. Connect to Build DynMesh Selection or a process node's Selector input. Highest ID is not a guarantee of boolean operand provenance.");
}
#endif

FName UPCGDynMeshPolygroupSelectionFactoryProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshSelectionFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGDynMeshPolygroupSelectionFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId();
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshPolygroupSelectionFactoryProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	auto* Factory = InFactory ? Cast<UPCGDynMeshPolygroupSelectionFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGDynMeshPolygroupSelectionFactoryData>(InContext);
	if (!Factory) { return nullptr; }
	Factory->Priority = Priority;
	Factory->GroupLayer = GroupLayer;
	Factory->SelectionMode = SelectionMode;
	Factory->GroupIDs = GroupIDs;
	Factory->bInvertSelection = bInvertSelection;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
