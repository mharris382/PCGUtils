// Copyright Max Harris

#include "Elements/PCGUtilsDynMeshTopologyProcessBase.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Selections/PCGDynMeshPolygroupSelectionFactory.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshTopologyProcessBase"

bool FPCGUtilsDynMeshTopologyOperation::Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	using namespace UE::Geometry;
	UDynamicMesh* Target = Invocation.MeshData ? Invocation.MeshData->GetMutableDynamicMesh() : nullptr;
	if (!Target) { return false; }
	const FGeometrySelection* Selection = Invocation.SelectionData ? &Invocation.SelectionData->GetSelection() : nullptr;
	FGeometrySelection Result;
	Result.InitializeTypes(EGeometryElementType::Face, EGeometryTopologyType::Triangle);
	bool bSuccess = true;
	Target->EditMesh([&](FDynamicMesh3& Mesh)
	{
		TArray<int32> ResultTriangles;
		// Geometry Script commonly interprets an empty selection as "all". Here it always means no work.
		if (!Selection || !Selection->Selection.IsEmpty())
		{
			bSuccess = Apply(Mesh, Selection, ResultTriangles);
			if (!bSuccess) { return; }
		}
		for (int32 TriangleID : ResultTriangles)
		{
			if (Mesh.IsTriangle(TriangleID)) { Result.Selection.Add(FGeoSelectionID::MeshTriangle(TriangleID).Encoded()); }
		}
		if (ResultOptions.bAssignPolygroup)
		{
			if (!Mesh.HasTriangleGroups()) { Mesh.EnableTriangleGroups(0); }
			const int32 GroupID = Result.Selection.IsEmpty() ? INDEX_NONE : Mesh.AllocateTriangleGroup();
			if (!Mesh.HasAttributes()) { Mesh.EnableAttributes(); }
			FDynamicMeshPolygroupAttribute* Layer = nullptr;
			for (int32 Index = 0; Index < Mesh.Attributes()->NumPolygroupLayers(); ++Index)
			{
				auto* Candidate = Mesh.Attributes()->GetPolygroupLayer(Index);
				if (Candidate->GetName() == ResultOptions.PolygroupName) { Layer = Candidate; break; }
			}
			if (!Layer)
			{
				const int32 Index = Mesh.Attributes()->NumPolygroupLayers();
				Mesh.Attributes()->SetNumPolygroupLayers(Index + 1);
				Layer = Mesh.Attributes()->GetPolygroupLayer(Index);
				Layer->SetName(ResultOptions.PolygroupName);
			}
			// A reused name replaces the region, even when this execution produced nothing.
			for (int32 TriangleID : Mesh.TriangleIndicesItr()) { Layer->SetValue(TriangleID, 0); }
			for (uint64 EncodedID : Result.Selection)
			{
				const int32 TriangleID = FGeoSelectionID(EncodedID).GeometryID;
				Mesh.SetTriangleGroup(TriangleID, GroupID);
				Layer->SetValue(TriangleID, 1);
			}
		}
	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);
	if (!bSuccess)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("TopologyFailed", "DynMesh topology operation failed; this input was skipped."), Invocation.Context);
		return false;
	}
	auto* ResultData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshSelectionData>(Invocation.Context);
	ResultData->Initialize(Invocation.MeshData, MoveTemp(Result));
	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Replace;
	OutOutcome.NewSelectionData = ResultData;
	return true;
}

UPCGUtilsDynMeshTopologyProcessBaseSettings::UPCGUtilsDynMeshTopologyProcessBaseSettings()
{
	bRequireSelection = true;
}

FName UPCGUtilsDynMeshTopologyProcessBaseSettings::GetResultPolygroupName() const
{
	if (!ResultPolygroupName.IsNone()) { return ResultPolygroupName; }
	// Property overrides duplicate settings into the transient package. Always derive identity from the authoring node.
	const UPCGSettings* Source = OriginalSettings ? OriginalSettings : this;
	return FName(*(TEXT("PCGUtils.Result.") + Source->GetPathName()));
}

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGUtilsDynMeshTopologyProcessBaseSettings::CreateProcessOperation(FPCGContext* Context) const
{
	TSharedPtr<FPCGUtilsDynMeshTopologyOperation> Operation = CreateTopologyOperation(Context);
	if (Operation)
	{
		Operation->ResultOptions.bAssignPolygroup = bAssignResultPolygroup || bOutputResultSelector;
		Operation->ResultOptions.PolygroupName = GetResultPolygroupName();
	}
	return Operation;
}

void UPCGUtilsDynMeshTopologyProcessBaseSettings::AddProcessOperationToCrc(FArchiveCrc32& Ar) const
{
	Super::AddProcessOperationToCrc(Ar);
	if (bAssignResultPolygroup || bOutputResultSelector)
	{
		FName Name = GetResultPolygroupName();
		Ar << Name;
	}
}

TArray<FPCGPinProperties> UPCGUtilsDynMeshTopologyProcessBaseSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::OutputPinProperties();
	// Always declared so a PCG property override can enable output without changing graph wiring.
	Pins.Emplace(PCGUtilsDynMeshTopologyProcessConstants::ResultSelectorPin,
		FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(), false, false);
	return Pins;
}

void UPCGUtilsDynMeshTopologyProcessBaseSettings::EmitAdditionalOutputs(FPCGContext* Context) const
{
	if (!bOutputResultSelector || Context->OutputData.GetInputsByPin(GetMainOutputPinLabel()).IsEmpty()) { return; }
	auto* Selector = FPCGContext::NewObject_AnyThread<UPCGDynMeshPolygroupSelectionFactoryData>(Context);
	Selector->GroupLayerName = GetResultPolygroupName();
	Selector->GroupIDs = { 1 };
	Selector->bAllowMissingNamedLayer = true;
	// One descriptor serves every result mesh and every seed. It owns no mesh and contains no allocated group ID.
	FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
	Output.Data = Selector;
	Output.Pin = PCGUtilsDynMeshTopologyProcessConstants::ResultSelectorPin;
}

FPCGElementPtr UPCGUtilsDynMeshTopologyProcessBaseSettings::CreateElement() const
{
	return MakeShared<FPCGUtilsDynMeshProcessBaseElement>();
}

#undef LOCTEXT_NAMESPACE
