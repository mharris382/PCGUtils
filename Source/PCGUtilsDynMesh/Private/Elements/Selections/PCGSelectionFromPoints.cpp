#include "Elements/Selections/PCGSelectionFromPoints.h"

#include "Data/PCGBasePointData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Metadata/PCGMetadata.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSelectionFromPoints"

#if WITH_EDITOR
FText UPCGSelectionFromPointsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DEPRECATED: Selection From Points");
}

FText UPCGSelectionFromPointsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Deprecated materialized-selection node. Use the Select from Points selector with Build DynMesh Selection.");
}
#endif

TArray<FPCGPinProperties> UPCGSelectionFromPointsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();
	Pins.Emplace_GetRef(PCGSelectionFromPointsConstants::PointsInputPin, EPCGDataType::Point, true, true).SetRequiredPin();
	return Pins;
}

FPCGElementPtr UPCGSelectionFromPointsSettings::CreateElement() const
{
	return MakeShared<FPCGSelectionFromPointsElement>();
}

bool FPCGSelectionFromPointsElement::CreateSelection(const UPCGDynamicMeshData*,
	const UE::Geometry::FDynamicMesh3& Mesh, const FPCGDynamicMeshSelectionCandidates& Candidates,
	FPCGContext* Context,
	UE::Geometry::FGeometrySelection& OutSelection) const
{
	const UPCGSelectionFromPointsSettings* Settings = Context->GetInputSettings<UPCGSelectionFromPointsSettings>();
	check(Settings);
	OutSelection.InitializeTypes(UE::Geometry::EGeometryElementType::Vertex, UE::Geometry::EGeometryTopologyType::Triangle);

	if (Settings->VertexIndexAttribute.IsNone())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("EmptyAttribute", "Selection From Points requires a Vertex Index Attribute name."), Context);
		return false;
	}

	TSet<int32> RestrictedVertexIDs;
	if (Candidates.IsRestricted())
	{
		Candidates.ProcessVertices([&RestrictedVertexIDs](int32 VertexID)
		{
			RestrictedVertexIDs.Add(VertexID);
		});
	}

	int32 InvalidIndexCount = 0;
	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGSelectionFromPointsConstants::PointsInputPin))
	{
		const UPCGBasePointData* PointData = Cast<const UPCGBasePointData>(Input.Data);
		const UPCGMetadata* Metadata = PointData ? PointData->ConstMetadata() : nullptr;
		const FPCGMetadataDomain* ElementsDomain = Metadata ? Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Elements) : nullptr;
		const FPCGMetadataAttribute<int32>* Attribute = ElementsDomain
			? ElementsDomain->GetConstTypedAttribute<int32>(Settings->VertexIndexAttribute) : nullptr;
		if (!PointData || !Attribute)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("MissingAttribute", "Selection From Points skipped point data without the integer attribute '{0}'."),
				FText::FromName(Settings->VertexIndexAttribute)), Context);
			continue;
		}

		const auto Entries = PointData->GetConstMetadataEntryValueRange();
		for (const int64 Entry : Entries)
		{
			const int32 VertexID = Attribute->GetValueFromItemKey(Entry);
			if (Mesh.IsVertex(VertexID))
			{
				if (!Candidates.IsRestricted() || RestrictedVertexIDs.Contains(VertexID))
				{
					OutSelection.Selection.Add(UE::Geometry::FGeoSelectionID::MeshVertex(VertexID).Encoded());
				}
			}
			else
			{
				++InvalidIndexCount;
			}
		}
	}

	if (InvalidIndexCount > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("InvalidIndices", "Selection From Points ignored {0} invalid or stale vertex indices."),
			FText::AsNumber(InvalidIndexCount)), Context);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
