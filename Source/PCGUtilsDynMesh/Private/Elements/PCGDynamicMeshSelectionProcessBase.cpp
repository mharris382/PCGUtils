#include "Elements/PCGDynamicMeshSelectionProcessBase.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Materials/MaterialInterface.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynamicMeshSelectionProcessBase"

TArray<FPCGPinProperties> UPCGDynamicMeshSelectionProcessBaseSettings::InputPinProperties() const
{
	FPCGDataTypeIdentifier InputTypes(EPCGDataType::DynamicMesh);
	InputTypes |= FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass());
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGDynamicMeshSelectionProcessConstants::InputPin, MoveTemp(InputTypes), true, true).SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGDynamicMeshSelectionProcessBaseSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGDynamicMeshSelectionProcessConstants::OutputPin, EPCGDataType::DynamicMesh, true, true)};
}

bool FPCGDynamicMeshSelectionProcessBaseElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGDynamicMeshSelectionProcessConstants::InputPin))
	{
		const UPCGDynamicMeshSelectionData* SelectionData = Cast<const UPCGDynamicMeshSelectionData>(Input.Data);
		const UPCGDynamicMeshData* SourceData = SelectionData
			? SelectionData->GetSourceMeshData() : Cast<const UPCGDynamicMeshData>(Input.Data);
		const UDynamicMesh* SourceObject = SourceData ? SourceData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
		if (!SourceMesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidInput", "Dynamic Mesh processor skipped an input with no valid source mesh."), Context);
			continue;
		}

		TArray<UMaterialInterface*> Materials;
		Materials.Reserve(SourceData->GetMaterials().Num());
		for (UMaterialInterface* Material : SourceData->GetMaterials())
		{
			Materials.Add(Material);
		}

		UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
		OutputData->Initialize(UE::Geometry::FDynamicMesh3(*SourceMesh), MoveTemp(Materials));
		if (!ProcessMesh(OutputData, SelectionData, Context))
		{
			continue;
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGDynamicMeshSelectionProcessConstants::OutputPin;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
