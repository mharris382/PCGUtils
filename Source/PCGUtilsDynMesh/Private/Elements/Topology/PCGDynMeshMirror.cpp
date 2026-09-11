// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshMirror.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#if WITH_AUTOMATION_TESTS
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Misc/AutomationTest.h"
#endif

#define LOCTEXT_NAMESPACE "PCGDynMeshMirror"

namespace
{
	FTransform ResolveMirrorSpaceFrame(
		const FPCGUtilsDynMeshProcessInvocation& Invocation,
		EPCGUtilsDynMeshTransformSpace Space)
	{
		switch (Space)
		{
		case EPCGUtilsDynMeshTransformSpace::World:
			return PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
				Invocation.Context, Invocation.MeshData, true).Inverse();

		case EPCGUtilsDynMeshTransformSpace::BuilderLocal:
			if (Invocation.bHasBuilderFrame)
			{
				return Invocation.BuilderFrame;
			}
			[[fallthrough]];

		case EPCGUtilsDynMeshTransformSpace::DynMeshLocal:
		{
			const UDynamicMesh* MeshObject = Invocation.MeshData ? Invocation.MeshData->GetDynamicMesh() : nullptr;
			const UE::Geometry::FDynamicMesh3* Mesh = MeshObject ? MeshObject->GetMeshPtr() : nullptr;
			return Mesh && Mesh->VertexCount() > 0
				? FTransform(FVector(Mesh->GetBounds().Center()))
				: FTransform::Identity;
		}

		case EPCGUtilsDynMeshTransformSpace::ActorLocal:
		default:
			return FTransform::Identity;
		}
	}
}

#if WITH_EDITOR
FText UPCGDynMeshMirrorSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "DynMesh | Mirror");
}

FText UPCGDynMeshMirrorSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Mirrors a DynMesh across a plane, with optional cutting and seam welding. The plane may use actor, world, "
		"DynMesh-local, or Builder-local coordinates. Selection inputs mirror only the selected triangle region; "
		"Builder inputs defer the operation until realization.");
}
#endif

bool UPCGDynMeshMirrorSettings::GetRequiredSelectionDomain(
	UE::Geometry::EGeometryElementType& OutElementType) const
{
	OutElementType = UE::Geometry::EGeometryElementType::Face;
	return true;
}

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshMirrorSettings::CreateProcessOperation(
	FPCGContext* InContext) const
{
	TSharedPtr<FPCGUtilsDynMeshMirrorOperation> Operation = MakeShared<FPCGUtilsDynMeshMirrorOperation>();
	Operation->MirrorPlane = MirrorPlane;
	Operation->Space = Space;
	Operation->Options = Options;
	return Operation;
}

FPCGElementPtr UPCGDynMeshMirrorSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshMirrorElement>();
}

bool FPCGUtilsDynMeshMirrorOperation::Execute(
	const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	if (MirrorPlane.ContainsNaN())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("InvalidMirrorPlane", "DynMesh Mirror requires a finite Mirror Plane transform; the input was left unchanged."),
			Invocation.Context);
		return true;
	}

	FPCGUtilsMeshTargetHandle Handle = FPCGUtilsMeshTargetFunctions::CreateTargetInPlace(
		Invocation, EPCGUtilsMeshTargetPreparation::Region);
	if (!Handle.IsValid())
	{
		return false;
	}

	if (!Handle.IsEmptySelectionNoOp())
	{
		const FTransform SpaceFrame = ResolveMirrorSpaceFrame(Invocation, Space);
		const FTransform EffectivePlane = MirrorPlane * SpaceFrame;
		UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshMirror(
			Handle.GetTargetMesh(), EffectivePlane, Options);
	}

	if (!FPCGUtilsMeshTargetFunctions::RestoreRegion(Handle))
	{
		return false;
	}

	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Clear;
	return true;
}

#undef LOCTEXT_NAMESPACE

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshMirrorGeometryTest,
	"PCGUtils.DynMesh.Mirror.Geometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshMirrorGeometryTest::RunTest(const FString&)
{
	UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
		Data->GetMutableDynamicMesh(), FGeometryScriptPrimitiveOptions(), FTransform(FVector(0.0, 0.0, 10.0)),
		20.0, 20.0, 20.0, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Center);

	FPCGUtilsDynMeshMirrorOperation Operation;
	Operation.Space = EPCGUtilsDynMeshTransformSpace::ActorLocal;
	Operation.MirrorPlane = FTransform::Identity;
	FPCGUtilsDynMeshProcessInvocation Invocation;
	Invocation.MeshData = Data;
	FPCGUtilsDynMeshProcessOutcome Outcome;
	TestTrue(TEXT("Mirror executes"), Operation.Execute(Invocation, Outcome));

	const UE::Geometry::FDynamicMesh3* Mesh = Data->GetDynamicMesh()->GetMeshPtr();
	TestNotNull(TEXT("Mirror retains a valid mesh"), Mesh);
	if (Mesh)
	{
		const UE::Geometry::FAxisAlignedBox3d Bounds = Mesh->GetBounds();
		TestTrue(TEXT("Mirror extends equally across the Z=0 plane"),
			FMath::IsNearlyEqual(Bounds.Min.Z, -20.0, 0.01)
			&& FMath::IsNearlyEqual(Bounds.Max.Z, 20.0, 0.01));
	}
	TestEqual(TEXT("Mirror invalidates the old topology selection"), Outcome.SelectionOutcome,
		EPCGUtilsDynMeshProcessSelectionOutcome::Clear);
	return true;
}

#endif
