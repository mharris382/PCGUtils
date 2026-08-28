// Copyright Max Harris

#include "Elements/Deform/PCGTransformDynMesh.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshTransformFunctions.h"
#include "UDynamicMesh.h"

#define LOCTEXT_NAMESPACE "PCGTransformDynMesh"

FTransform FPCGUtilsDynMeshTransformOperation::ResolveSpaceFrame(
	const FPCGUtilsDynMeshProcessInvocation& Invocation) const
{
	switch (Space)
	{
	case EPCGUtilsDynMeshTransformSpace::World:
	{
		// Mesh coordinates are target-actor-local, so world -> mesh is the inverse of the actor's transform.
		const FTransform ActorTransform = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
			Invocation.Context, Invocation.MeshData, /*bConvertToLocalSpace=*/true);
		return ActorTransform.Inverse();
	}

	case EPCGUtilsDynMeshTransformSpace::BuilderLocal:
		if (Invocation.bHasBuilderFrame)
		{
			return Invocation.BuilderFrame;
		}
		// Concrete DynMesh data has no Builder frame; fall through to the mesh's own bounds centre, which is
		// the closest equivalent and keeps the node usable in immediate mode.
		[[fallthrough]];

	case EPCGUtilsDynMeshTransformSpace::DynMeshLocal:
	{
		const UDynamicMesh* MeshObject = Invocation.MeshData ? Invocation.MeshData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* Mesh = MeshObject ? MeshObject->GetMeshPtr() : nullptr;
		if (!Mesh || Mesh->VertexCount() == 0)
		{
			return FTransform::Identity;
		}
		return FTransform(FVector(Mesh->GetBounds().Center()));
	}

	case EPCGUtilsDynMeshTransformSpace::ActorLocal:
	default:
		return FTransform::Identity;
	}
}

bool FPCGUtilsDynMeshTransformOperation::Execute(
	const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	using namespace UE::Geometry;

	if (!Invocation.MeshData)
	{
		return false;
	}
	UDynamicMesh* TargetMesh = Invocation.MeshData->GetMutableDynamicMesh();
	if (!TargetMesh)
	{
		return false;
	}

	// Transform never changes topology, so any selection that was valid on the way in is still valid on the
	// way out. Do not clear it merely because vertex positions moved.
	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Preserve;

	if (Transform.Equals(FTransform::Identity))
	{
		return true;
	}

	// Every space reduces to one conjugation: step out of the mesh's coordinates into the chosen frame, apply
	// the transform there, step back. UE composes left-to-right, so this reads in application order.
	const FTransform SpaceFrame = ResolveSpaceFrame(Invocation);
	const FTransform EffectiveTransform = SpaceFrame.Inverse() * Transform * SpaceFrame;

	if (!Invocation.SelectionData)
	{
		// The whole shape moved, so the Builder frame should move with it.
		OutOutcome.GeometryTransform = EffectiveTransform;
		UGeometryScriptLibrary_MeshTransformFunctions::TransformMesh(
			TargetMesh, EffectiveTransform, bFixOrientationForNegativeScale);
		return true;
	}

	// Only part of the mesh moves below, which says nothing about where the subtree as a whole now sits - so
	// the Builder frame is deliberately left where it is.

	FGeometryScriptMeshSelection Selection;
	Selection.SetSelection(Invocation.SelectionData->GetSelection());
	UGeometryScriptLibrary_MeshTransformFunctions::TransformMeshSelection(
		TargetMesh, Selection, EffectiveTransform);

	if (bRecomputeSelectionNormals)
	{
		TargetMesh->EditMesh([&Selection](FDynamicMesh3& Mesh)
		{
			if (!Mesh.HasAttributes() || !Mesh.Attributes()->PrimaryNormals())
			{
				return;
			}
			TSet<int32> AffectedTriangles;
			Selection.ProcessByVertexID(Mesh, [&Mesh, &AffectedTriangles](int32 VertexID)
			{
				for (const int32 TriangleID : Mesh.VtxTrianglesItr(VertexID)) { AffectedTriangles.Add(TriangleID); }
			}, false);
			FMeshNormals::RecomputeOverlayTriNormals(
				Mesh, AffectedTriangles.Array(), /*bAreaWeighted=*/true, /*bAngleWeighted=*/true);
		});
	}
	return true;
}

#if WITH_EDITOR
FText UPCGTransformDynMeshSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Transform DynMesh");
}

FText UPCGTransformDynMeshSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Applies an offset, rotation, and scale to a DynMesh, or to only the vertices covered by a DynMesh "
		"Selection or connected Selector, in the chosen Space. Connect a DynMesh Builder instead and this "
		"node produces a Builder: the transform is recorded and applied per seed when the Builder is "
		"realized, and Builder Local space then transforms each shape about its own placement pivot.");
}
#endif

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGTransformDynMeshSettings::CreateProcessOperation(
	FPCGContext* InContext) const
{
	// Every setting the algorithm needs is copied here, while this node is the one executing. Nothing about
	// the operation may be looked up again from whatever context later evaluates it.
	TSharedPtr<FPCGUtilsDynMeshTransformOperation> Operation = MakeShared<FPCGUtilsDynMeshTransformOperation>();
	Operation->Transform = Transform;
	Operation->Space = Space;
	Operation->bFixOrientationForNegativeScale = bFixOrientationForNegativeScale;
	Operation->bRecomputeSelectionNormals = bRecomputeSelectionNormals;
	return Operation;
}

FPCGElementPtr UPCGTransformDynMeshSettings::CreateElement() const
{
	return MakeShared<FPCGTransformDynMeshElement>();
}

#undef LOCTEXT_NAMESPACE
