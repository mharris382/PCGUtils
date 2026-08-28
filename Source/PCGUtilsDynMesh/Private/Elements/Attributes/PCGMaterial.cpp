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

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGMaterialSettings::CreateProcessOperation(
	FPCGContext* InContext) const
{
	// Soft references are resolved here, while this node executes. A deferred operation runs from a
	// materializer's context and must not perform a synchronous load at that point.
	TSharedPtr<FPCGUtilsDynMeshMaterialOperation> Operation = MakeShared<FPCGUtilsDynMeshMaterialOperation>();
	Operation->AssignedMaterial = Material.LoadSynchronous();
	Operation->DefaultMaterial = DefaultMaterial.LoadSynchronous();
	return Operation;
}

bool FPCGUtilsDynMeshMaterialOperation::Execute(
	const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	UPCGDynamicMeshData* MeshData = Invocation.MeshData;
	if (!MeshData)
	{
		return false;
	}
	if (!AssignedMaterial)
	{
		PCGLog::LogWarningOnGraph(
			LOCTEXT("MissingMaterial", "Material has no valid material to assign."), Invocation.Context);
		return false;
	}

	UDynamicMesh* DynamicMesh = MeshData->GetMutableDynamicMesh();
	UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
	if (!Mesh)
	{
		return false;
	}

	// Only material IDs and the material array change; vertices and triangles are untouched.
	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Preserve;

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

	if (!Invocation.SelectionData)
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
		Materials = {DefaultMaterial, AssignedMaterial};
		for (const int32 TriangleID : Mesh->TriangleIndicesItr())
		{
			MaterialIDs->SetValue(TriangleID, 0);
		}
		if (!DefaultMaterial)
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("MissingDefaultMaterial", "Material created slot zero without a valid Default Material."),
				Invocation.Context);
		}
	}

	FGeometryScriptMeshSelection Selection;
	Selection.SetSelection(Invocation.SelectionData->GetSelection());
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
			FText::AsNumber(InvalidTriangleCount)), Invocation.Context);
	}
	MeshData->SetMaterials(Materials);
	return true;
}

#undef LOCTEXT_NAMESPACE
