#include "FunctionLibraries/PCGUtilsDynMeshHelpers.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "UDynamicMesh.h"

namespace
{
	double GetAxisValue(const FVector3d& Vector, ESplineMeshAxis::Type Axis)
	{
		switch (Axis)
		{
		case ESplineMeshAxis::Y: return Vector.Y;
		case ESplineMeshAxis::Z: return Vector.Z;
		default: return Vector.X;
		}
	}

	FVector3d RemoveAxisValue(FVector3d Vector, ESplineMeshAxis::Type Axis)
	{
		switch (Axis)
		{
		case ESplineMeshAxis::Y: Vector.Y = 0.0; break;
		case ESplineMeshAxis::Z: Vector.Z = 0.0; break;
		default: Vector.X = 0.0; break;
		}
		return Vector;
	}

	FVector2D GetTransverseScale(const FVector& Scale, ESplineMeshAxis::Type Axis)
	{
		switch (Axis)
		{
		case ESplineMeshAxis::Y: return FVector2D(Scale.X, Scale.Z);
		case ESplineMeshAxis::Z: return FVector2D(Scale.X, Scale.Y);
		default: return FVector2D(Scale.Y, Scale.Z);
		}
	}
}

UDynamicMesh* UPCGUtilsDynMeshHelpers::CreateDynamicMeshFromSplineMeshParams(const FPCGUtilsSplineMeshBakeParams& Params)
{
	if (!IsValid(Params.StaticMesh))
	{
		return nullptr;
	}

	UDynamicMesh* Result = NewObject<UDynamicMesh>(GetTransientPackage());
	if (!Result)
	{
		return nullptr;
	}

	FGeometryScriptCopyMeshFromAssetOptions AssetOptions;
	AssetOptions.bRequestTangents = true;
	FGeometryScriptMeshReadLOD RequestedLOD;
	RequestedLOD.LODType = EGeometryScriptLODType::RenderData;
	RequestedLOD.LODIndex = FMath::Max(0, Params.RenderLODIndex);
	EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
	UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMeshV2(
		Params.StaticMesh, Result, AssetOptions, RequestedLOD, Outcome, true);
	if (Outcome != EGeometryScriptOutcomePins::Success)
	{
		return nullptr;
	}

	// A transient, unregistered, unattached USplineMeshComponent is used purely as an implementation detail to
	// reuse Unreal's own CalcSliceTransform() deformation math. It is never attached to any actor, never
	// registered into the world, and never affects any scene state - it exists only long enough to compute
	// per-vertex slice transforms below.
	USplineMeshComponent* SplineMesh = NewObject<USplineMeshComponent>(GetTransientPackage());
	if (!SplineMesh)
	{
		return nullptr;
	}

	SplineMesh->SetStaticMesh(Params.StaticMesh);
	SplineMesh->SetForwardAxis(Params.ForwardAxis, false);
	SplineMesh->SetSplineUpDir(Params.SplineUpDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector), false);
	SplineMesh->SetStartAndEnd(
		Params.StartPos, Params.StartTangent,
		Params.EndPos, Params.EndTangent, false);
	SplineMesh->SetStartScale(Params.StartScale, false);
	SplineMesh->SetEndScale(Params.EndScale, false);
	SplineMesh->SetStartRoll(Params.StartRollRadians, false);
	SplineMesh->SetEndRoll(Params.EndRollRadians, false);
	SplineMesh->SetStartOffset(Params.StartOffset, false);
	SplineMesh->SetEndOffset(Params.EndOffset, false);
	SplineMesh->SetBoundaryMin(Params.SplineBoundaryMin, false);
	SplineMesh->SetBoundaryMax(Params.SplineBoundaryMax, false);
	SplineMesh->bSmoothInterpRollScale = Params.bSmoothInterpRollScale;

	const ESplineMeshAxis::Type Axis = Params.ForwardAxis.GetValue();
	Result->EditMesh([SplineMesh, Axis](UE::Geometry::FDynamicMesh3& Mesh)
	{
		TArray<FVector3d> OriginalPositions;
		OriginalPositions.SetNum(Mesh.MaxVertexID());
		for (int32 VertexID : Mesh.VertexIndicesItr())
		{
			OriginalPositions[VertexID] = Mesh.GetVertex(VertexID);
		}

		if (Mesh.HasAttributes() && Mesh.Attributes()->PrimaryNormals())
		{
			UE::Geometry::FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
			for (int32 ElementID : Normals->ElementIndicesItr())
			{
				const int32 ParentVertexID = Normals->GetParentVertex(ElementID);
				if (!OriginalPositions.IsValidIndex(ParentVertexID))
				{
					continue;
				}
				const FTransform Slice = SplineMesh->CalcSliceTransform(
					static_cast<float>(GetAxisValue(OriginalPositions[ParentVertexID], Axis)));
				const FVector DeformedNormal = Slice.TransformVector(FVector(Normals->GetElement(ElementID))).GetSafeNormal();
				Normals->SetElement(ElementID, FVector3f(DeformedNormal));
			}
		}

		for (int32 VertexID : Mesh.VertexIndicesItr())
		{
			const FVector3d OriginalPosition = OriginalPositions[VertexID];
			const FTransform Slice = SplineMesh->CalcSliceTransform(
				static_cast<float>(GetAxisValue(OriginalPosition, Axis)));
			Mesh.SetVertex(VertexID, FVector3d(Slice.TransformPosition(FVector(RemoveAxisValue(OriginalPosition, Axis)))));
		}
	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, true);

	return Result;
}

UDynamicMesh* UPCGUtilsDynMeshHelpers::CreateDynamicMeshFromSplineMesh(
	UStaticMesh* StaticMesh,
	const FTransform& StartTransform,
	FVector StartTangent,
	const FTransform& EndTransform,
	FVector EndTangent,
	TEnumAsByte<ESplineMeshAxis::Type> ForwardAxis,
	FVector SplineUpDirection,
	bool bSmoothInterpRollScale,
	int32 RenderLODIndex)
{
	FPCGUtilsSplineMeshBakeParams Params;
	Params.StaticMesh = StaticMesh;
	Params.StartPos = StartTransform.GetLocation();
	Params.StartTangent = StartTangent;
	Params.EndPos = EndTransform.GetLocation();
	Params.EndTangent = EndTangent;
	Params.StartScale = GetTransverseScale(StartTransform.GetScale3D(), ForwardAxis);
	Params.EndScale = GetTransverseScale(EndTransform.GetScale3D(), ForwardAxis);
	Params.StartRollRadians = FMath::DegreesToRadians(StartTransform.Rotator().Roll);
	Params.EndRollRadians = FMath::DegreesToRadians(EndTransform.Rotator().Roll);
	// Procedural PCG spline segments have no offset/boundary concept - leave them at their
	// USplineMeshComponent-default values (zero offset, zero boundary meaning "use bounding box").
	Params.StartOffset = FVector2D::ZeroVector;
	Params.EndOffset = FVector2D::ZeroVector;
	Params.SplineBoundaryMin = 0.0f;
	Params.SplineBoundaryMax = 0.0f;
	Params.ForwardAxis = ForwardAxis;
	Params.SplineUpDirection = SplineUpDirection;
	Params.bSmoothInterpRollScale = bSmoothInterpRollScale;
	Params.RenderLODIndex = RenderLODIndex;

	return CreateDynamicMeshFromSplineMeshParams(Params);
}
