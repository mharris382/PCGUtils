// Copyright Max Harris

#include "Elements/Selections/PCGUtilsDynMeshSelectionSource.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"
#include "Factories/PCGUtilsDynMeshFactoryData.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "PCGContext.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshSelectionSource"

namespace
{
	UE::Geometry::EGeometryElementType ToGeometryElementType(
		EPCGUtilsDynMeshSelectionElementType ElementType)
	{
		switch (ElementType)
		{
		case EPCGUtilsDynMeshSelectionElementType::Vertex:
			return UE::Geometry::EGeometryElementType::Vertex;
		case EPCGUtilsDynMeshSelectionElementType::Edge:
			return UE::Geometry::EGeometryElementType::Edge;
		case EPCGUtilsDynMeshSelectionElementType::Triangle:
		default:
			return UE::Geometry::EGeometryElementType::Face;
		}
	}
}

FName UPCGUtilsDynMeshSelectionSourceSettings::GetMainOutputPin() const
{
	return Representation == EPCGUtilsDynMeshSelectionRepresentation::Selector
		? PCGUtilsDynMeshSelectionFactoryConstants::OutputPin
		: PCGUtilsDynMeshSelectionSourceConstants::SelectionPin;
}

UPCGUtilsDynMeshFactoryData* UPCGUtilsDynMeshSelectionSourceSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	UPCGUtilsDynMeshSelectionFactoryData* Selector =
		Cast<UPCGUtilsDynMeshSelectionFactoryData>(InFactory);
	if (!Selector)
	{
		return nullptr;
	}
	Selector->bInvertSelection = bInvertSelection;
	return Super::CreateFactory(InContext, Selector);
}

#if WITH_EDITOR
TArray<FPCGPreConfiguredSettingsInfo> UPCGUtilsDynMeshSelectionSourceSettings::MakeRepresentationPresets(
	const FText& DisplayName, int32 SelectorIndex, int32 SelectionIndex)
{
	// SelectionIndex stays part of the contract - ApplyPreconfiguredSettings still honours it for graphs
	// created before the alias was withdrawn - it simply no longer earns a palette entry of its own.
	(void)SelectionIndex;
	return {
		{SelectorIndex,
			DisplayName,
			LOCTEXT("SelectorPresetTooltip",
				"Authors the selection as a deferred, composable Selector. Switch Representation on the placed "
				"node to materialize a mesh-bound Selection instead."),
			LOCTEXT("SelectorPresetSearchHints", "select selector selection deferred materialized DynMesh")}
	};
}

bool UPCGUtilsDynMeshSelectionSourceSettings::ApplyRepresentationPreset(
	int32 PreconfiguredIndex, int32 SelectorIndex, int32 SelectionIndex,
	EPCGUtilsDynMeshSelectionRepresentation& OutRepresentation)
{
	if (PreconfiguredIndex == SelectorIndex)
	{
		OutRepresentation = EPCGUtilsDynMeshSelectionRepresentation::Selector;
		return true;
	}
	if (PreconfiguredIndex == SelectionIndex)
	{
		OutRepresentation = EPCGUtilsDynMeshSelectionRepresentation::Selection;
		return true;
	}
	return false;
}

TArray<FPCGPreConfiguredSettingsInfo> UPCGUtilsDynMeshSelectionSourceSettings::GetPreconfiguredInfo() const
{
	return MakeRepresentationPresets(GetDefaultNodeTitle(),
		PCGUtilsDynMeshSelectionSourceConstants::SelectorPreconfiguredIndex,
		PCGUtilsDynMeshSelectionSourceConstants::SelectionPreconfiguredIndex);
}

void UPCGUtilsDynMeshSelectionSourceSettings::ApplyPreconfiguredSettings(
	const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	Super::ApplyPreconfiguredSettings(PreconfiguredInfo);
	ApplyRepresentationPreset(PreconfiguredInfo.PreconfiguredIndex,
		PCGUtilsDynMeshSelectionSourceConstants::SelectorPreconfiguredIndex,
		PCGUtilsDynMeshSelectionSourceConstants::SelectionPreconfiguredIndex, Representation);
}
#endif

const FPCGDataTypeBaseId& UPCGUtilsDynMeshSelectionSourceSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId();
}

void UPCGUtilsDynMeshSelectionSourceSettings::ApplyDeprecationBeforeUpdatePins(
	UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins,
	TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	Super::ApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
	if (InOutNode)
	{
		InOutNode->RenameInputPin(TEXT("In"), PCGUtilsDynMeshSelectionSourceConstants::CandidatesPin);
		InOutNode->RenameInputPin(TEXT("Factories"), TEXT("Selectors"));
		InOutNode->RenameInputPin(TEXT("Mesh"), PCGUtilsDynMeshSelectionSourceConstants::CandidatesPin);
		InOutNode->RenameOutputPin(TEXT("Selection Factory"), PCGUtilsDynMeshSelectionFactoryConstants::OutputPin);
	}
}

TArray<FPCGPinProperties> UPCGUtilsDynMeshSelectionSourceSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	if (Representation == EPCGUtilsDynMeshSelectionRepresentation::Selection)
	{
		FPCGDataTypeIdentifier CandidateTypes(EPCGDataType::DynamicMesh);
		CandidateTypes |= FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass());
		Pins.Emplace_GetRef(PCGUtilsDynMeshSelectionSourceConstants::CandidatesPin,
			MoveTemp(CandidateTypes), true, true).SetRequiredPin();
	}
	Pins.Append(SourceInputPinProperties());
	return Pins;
}

TArray<FPCGPinProperties> UPCGUtilsDynMeshSelectionSourceSettings::OutputPinProperties() const
{
	if (Representation == EPCGUtilsDynMeshSelectionRepresentation::Selector)
	{
		return Super::OutputPinProperties();
	}

	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGUtilsDynMeshSelectionSourceConstants::SelectionPin,
		FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass()), true, true).SetRequiredPin();
	return Pins;
}

FPCGElementPtr UPCGUtilsDynMeshSelectionSourceSettings::CreateElement() const
{
	return MakeShared<FPCGUtilsDynMeshSelectionSourceElement>();
}

UE::Geometry::EGeometryElementType
UPCGUtilsDynMeshSelectionSourceSettings::GetMaterializedElementType() const
{
	return ToGeometryElementType(SelectionElementType);
}

bool FPCGUtilsDynMeshSelectionSourceElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGUtilsDynMeshSelectionSourceSettings* Settings =
		Context->GetInputSettings<UPCGUtilsDynMeshSelectionSourceSettings>();
	check(Settings);

	UPCGUtilsDynMeshSelectionFactoryData* Selector =
		Cast<UPCGUtilsDynMeshSelectionFactoryData>(Settings->CreateFactory(Context));
	if (!Selector)
	{
		return true;
	}

	if (!Selector->Prepare(Context))
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("SelectorPreparationFailed", "Selector preparation failed."), Context);
		return true;
	}

	for (const FPCGPinProperties& InputPin : Settings->SourceInputPinProperties())
	{
		for (const FPCGTaggedData& TaggedData : Context->InputData.GetInputsByPin(InputPin.Label))
		{
			Selector->AddDataDependency(TaggedData.Data);
		}
	}

	if (Settings->Representation == EPCGUtilsDynMeshSelectionRepresentation::Selector)
	{
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
		Output.Data = Selector;
		Output.Pin = PCGUtilsDynMeshSelectionFactoryConstants::OutputPin;
		return true;
	}

	FPCGUtilsDynMeshSelectionDomain Domain;
	Domain.ElementType = Settings->GetMaterializedElementType();
	Domain.TopologyType = UE::Geometry::EGeometryTopologyType::Triangle;

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(
		PCGUtilsDynMeshSelectionSourceConstants::CandidatesPin))
	{
		const UPCGDynamicMeshSelectionData* IncomingSelectionData =
			Cast<const UPCGDynamicMeshSelectionData>(Input.Data);
		const UPCGDynamicMeshData* MeshData = IncomingSelectionData
			? IncomingSelectionData->GetSourceMeshData()
			: Cast<const UPCGDynamicMeshData>(Input.Data);
		const UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
		if (!MeshData || !Mesh)
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("InvalidCandidates", "Selection query skipped invalid Candidates or a missing source mesh."), Context);
			continue;
		}

		FPCGUtilsDynMeshSelectionEvaluationContext EvaluationContext(MeshData, *Mesh, Domain);
		UE::Geometry::FGeometrySelection ResultSelection;
		if (!PCGUtilsDynMeshSelectionFactories::EvaluateFactory(
			Selector, EvaluationContext, Context, ResultSelection))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("SelectorEvaluationFailed", "Selector could not be materialized in the requested element domain."), Context);
			continue;
		}

		if (IncomingSelectionData)
		{
			UE::Geometry::FGeometrySelection ConvertedCandidates;
			if (!PCGUtilsDynMeshSelectionDomains::ConvertSelection(
				MeshData, *Mesh, IncomingSelectionData->GetSelection(), Domain.ElementType,
				true, ConvertedCandidates))
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("CandidateConversionFailed", "Incoming Selection could not be converted to the query's element domain."), Context);
				continue;
			}
			ResultSelection.Selection = ConvertedCandidates.Selection.Intersect(ResultSelection.Selection);
		}

		UPCGDynamicMeshSelectionData* OutputData =
			FPCGContext::NewObject_AnyThread<UPCGDynamicMeshSelectionData>(Context);
		OutputData->Initialize(MeshData, MoveTemp(ResultSelection));
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGUtilsDynMeshSelectionSourceConstants::SelectionPin;
	}

	return true;
}

void FPCGUtilsDynMeshSelectionSourceElement::DisabledPassThroughData(FPCGContext* Context) const
{
	if (Context)
	{
		Context->OutputData.TaggedData.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
