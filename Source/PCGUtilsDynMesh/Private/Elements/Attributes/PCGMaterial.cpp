#include "Elements/Attributes/PCGMaterial.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGMaterial"

#if WITH_EDITOR
FText UPCGMaterialSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Material");
}

FText UPCGMaterialSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Assigns one material to a Dynamic Mesh, or appends a material and assigns it only to a Dynamic Mesh Selection.");
}
#endif

FPCGElementPtr UPCGMaterialSettings::CreateElement() const
{
	return MakeShared<FPCGMaterialElement>();
}

bool FPCGMaterialElement::ProcessMesh(UPCGDynamicMeshData* MeshData,
	const UPCGDynamicMeshSelectionData* SelectionData, FPCGContext* Context) const
{
	const UPCGMaterialSettings* Settings = Context->GetInputSettings<UPCGMaterialSettings>();
	check(Settings && MeshData);
	UMaterialInterface* AssignedMaterial = Settings->Material.LoadSynchronous();
	if (!AssignedMaterial)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("MissingMaterial", "Material has no valid material to assign."), Context);
		return false;
	}

	UDynamicMesh* DynamicMesh = MeshData->GetMutableDynamicMesh();
	UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
	if (!Mesh)
	{
		return false;
	}

	if (!Mesh->HasAttributes())
	{
		Mesh->EnableAttributes();
	}
	const bool bHadMaterialIDs = Mesh->Attributes()->HasMaterialID();
	if (!bHadMaterialIDs)
	{
		Mesh->Attributes()->EnableMaterialID();
	}
	UE::Geometry::FDynamicMeshMaterialAttribute* MaterialIDs = Mesh->Attributes()->GetMaterialID();

	if (!SelectionData)
	{
		for (const int32 TriangleID : Mesh->TriangleIndicesItr())
		{
			MaterialIDs->SetValue(TriangleID, 0);
		}
		MeshData->SetMaterials({AssignedMaterial});
		return true;
	}

	TArray<UMaterialInterface*> Materials;
	for (UMaterialInterface* ExistingMaterial : MeshData->GetMaterials())
	{
		Materials.Add(ExistingMaterial);
	}

	int32 SelectionMaterialID = 1;
	if (bHadMaterialIDs && !Materials.IsEmpty())
	{
		SelectionMaterialID = Materials.Num();
		Materials.Add(AssignedMaterial);
	}
	else
	{
		UMaterialInterface* BaseMaterial = Settings->DefaultMaterial.LoadSynchronous();
		Materials = {BaseMaterial, AssignedMaterial};
		for (const int32 TriangleID : Mesh->TriangleIndicesItr())
		{
			MaterialIDs->SetValue(TriangleID, 0);
		}
		if (!BaseMaterial)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("MissingDefaultMaterial", "Material created slot zero without a valid Default Material."), Context);
		}
	}

	FGeometryScriptMeshSelection Selection;
	Selection.SetSelection(SelectionData->GetSelection());
	TArray<int32> TriangleIDs;
	Selection.ConvertToMeshIndexArray(*Mesh, TriangleIDs, EGeometryScriptIndexType::Triangle);
	int32 InvalidTriangleCount = 0;
	for (const int32 TriangleID : TriangleIDs)
	{
		if (Mesh->IsTriangle(TriangleID))
		{
			MaterialIDs->SetValue(TriangleID, SelectionMaterialID);
		}
		else
		{
			++InvalidTriangleCount;
		}
	}
	if (InvalidTriangleCount > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("InvalidTriangles", "Material ignored {0} invalid or stale selected triangles."),
			FText::AsNumber(InvalidTriangleCount)), Context);
	}
	MeshData->SetMaterials(Materials);
	return true;
}

#undef LOCTEXT_NAMESPACE
