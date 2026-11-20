// Copyright Max Harris

#include "Geometry/PCGUtilsDynMeshFaceAngles.h"

#include "DynamicMesh/DynamicMesh3.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshFaceAngles"

FPCGUtilsDynMeshDotComparison PCGUtilsDynMeshFaceAngles::MakeDefaultComparison()
{
	FPCGUtilsDynMeshDotComparison Result;
	Result.Domain = EPCGUtilsDynMeshAngularDomain::Degrees;
	Result.Comparison = EPCGUtilsDynMeshDistanceComparison::EqualOrSmaller;
	Result.bUnsignedComparison = true;
	Result.DegreesConstant = 45.0;
	Result.DotConstant = FMath::Cos(FMath::DegreesToRadians(45.0));
	Result.ThresholdAttribute.Update(TEXT("@Data.FaceAngleThreshold"));
	return Result;
}

bool PCGUtilsDynMeshFaceAngles::ResolveComparison(const FPCGUtilsDynMeshDotComparison& Comparison,
	const UPCGData* MeshData, double& OutThresholdDot, FText& OutError)
{
	const bool bDegrees = Comparison.Domain == EPCGUtilsDynMeshAngularDomain::Degrees;
	if (Comparison.ThresholdInput == EPCGUtilsDynMeshInputValueType::Constant)
	{
		const double Value = bDegrees ? Comparison.DegreesConstant : Comparison.DotConstant;
		const double Minimum = bDegrees ? 0.0 : -1.0, Maximum = bDegrees ? 180.0 : 1.0;
		if (!(Value >= Minimum && Value <= Maximum))
		{
			OutError = FText::Format(LOCTEXT("BadConstant", "Face Normal Comparison {0} constant is {1}; expected a finite value from {2} to {3}."),
				bDegrees ? LOCTEXT("Degrees", "Degrees") : LOCTEXT("Scalar", "Scalar"),
				FText::AsNumber(Value), FText::AsNumber(Minimum), FText::AsNumber(Maximum));
			return false;
		}
	}
	if (Comparison.Comparison == EPCGUtilsDynMeshDistanceComparison::NearlyEqual ||
		Comparison.Comparison == EPCGUtilsDynMeshDistanceComparison::NearlyNotEqual)
	{
		const double Tolerance = bDegrees ? Comparison.DegreesTolerance : Comparison.DotTolerance;
		const double Maximum = bDegrees ? 180.0 : 1.0;
		if (!(Tolerance >= 0.0 && Tolerance <= Maximum))
		{
			OutError = FText::Format(LOCTEXT("BadTolerance", "Face Normal Comparison tolerance is {0}; expected a finite value from 0 to {1}."),
				FText::AsNumber(Tolerance), FText::AsNumber(Maximum));
			return false;
		}
	}
	TArray<double> Thresholds;
	if (!Comparison.ResolveThresholds(MeshData, 1, Thresholds, OutError)) { return false; }
	OutThresholdDot = Thresholds[0];
	return true;
}

bool PCGUtilsDynMeshFaceAngles::TestEdge(const UE::Geometry::FDynamicMesh3& Mesh, int32 EdgeID,
	const FPCGUtilsDynMeshDotComparison& Comparison, double ThresholdDot)
{
	if (!Mesh.IsEdge(EdgeID)) { return false; }
	const UE::Geometry::FIndex2i Faces = Mesh.GetEdgeT(EdgeID);
	if (!Mesh.IsTriangle(Faces.A) || !Mesh.IsTriangle(Faces.B)) { return false; }
	const FVector3d A = Mesh.GetTriNormal(Faces.A), B = Mesh.GetTriNormal(Faces.B);
	if (A.SquaredLength() < 0.5 || B.SquaredLength() < 0.5) { return false; }
	return Comparison.Test(FMath::Clamp(A.Dot(B), -1.0, 1.0), ThresholdDot);
}

#undef LOCTEXT_NAMESPACE
