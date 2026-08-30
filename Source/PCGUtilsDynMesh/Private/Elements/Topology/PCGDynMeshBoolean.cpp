// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshBoolean.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshBoolean"

namespace
{
	class FDynMeshBooleanOperation final : public FPCGUtilsDynMeshProcessOperation
	{
	public:
		EGeometryScriptBooleanOperation BooleanOperation;
		FGeometryScriptMeshBooleanOptions BooleanOptions;
		FGeometryScriptMeshSelfUnionOptions SelfUnionOptions;
		bool bAssignOperandPolygroup = false;
		bool bSelfUnionOperand = false;

		virtual bool Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation,
			FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override
		{
			UDynamicMesh* Target = Invocation.MeshData ? Invocation.MeshData->GetMutableDynamicMesh() : nullptr;
			if (!Target) { return false; }
			if (!Invocation.OperandMeshData) { return true; }
			const UDynamicMesh* SourceOperand = Invocation.OperandMeshData->GetDynamicMesh();
			if (!SourceOperand) { return false; }
			const bool bGroupOperand = bAssignOperandPolygroup &&
				(BooleanOperation == EGeometryScriptBooleanOperation::Union || BooleanOperation == EGeometryScriptBooleanOperation::Subtract);

			// The Geometry Script boolean API only reads its tool, despite taking a non-const pointer.
			UDynamicMesh* Operand = const_cast<UDynamicMesh*>(SourceOperand);
			if (bGroupOperand || bSelfUnionOperand)
			{
				Operand = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Invocation.Context);
				Operand->SetMesh(UE::Geometry::FDynamicMesh3(*SourceOperand->GetMeshPtr()));
				if (bSelfUnionOperand)
				{
					UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshSelfUnion(Operand, SelfUnionOptions);
				}
				if (bGroupOperand)
				{
					// Group after self-union so any repair faces also belong to this operand. The engine boolean
					// remaps the tool's group to a fresh target group, retaining the primary's existing groups.
					Operand->EditMesh([](UE::Geometry::FDynamicMesh3& Mesh)
					{
						if (!Mesh.HasTriangleGroups()) { Mesh.EnableTriangleGroups(0); }
						for (int32 TriangleID : Mesh.TriangleIndicesItr()) { Mesh.SetTriangleGroup(TriangleID, 0); }
					});
					Target->EditMesh([](UE::Geometry::FDynamicMesh3& Mesh)
					{
						if (!Mesh.HasTriangleGroups()) { Mesh.EnableTriangleGroups(0); }
					});
				}
			}

			UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(Target, FTransform::Identity,
				Operand, FTransform::Identity, BooleanOperation, BooleanOptions);
			OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Clear;
			return true;
		}
	};
}

#if WITH_EDITOR
FText UPCGDynMeshBooleanSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh Boolean");
}

FText UPCGDynMeshBooleanSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Boolean operation on whole DynMeshes or deferred Builders. InA passes through unchanged when InB provides no data. Both inputs and the output use the same type; selections are not supported. Union and Subtract can place operand faces in a fresh PolyGroup. Both meshes must already share the same coordinate space.");
}
#endif

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshBooleanSettings::CreateProcessOperation(FPCGContext*) const
{
	TSharedPtr<FDynMeshBooleanOperation> Operation = MakeShared<FDynMeshBooleanOperation>();
	Operation->BooleanOperation = BooleanOperation;
	Operation->BooleanOptions = BooleanOperationOptions;
	Operation->SelfUnionOptions = OperandSelfUnionOptions;
	Operation->bAssignOperandPolygroup = bAssignOperandPolygroup;
	Operation->bSelfUnionOperand = bSelfUnionOperand;
	return Operation;
}

#undef LOCTEXT_NAMESPACE
