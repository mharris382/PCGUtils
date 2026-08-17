#include "Elements/Creation/CreatePrimitive/PCGCreatePrimitiveSettingsBase.h"

void UPCGCreatePrimitiveBoxSettings::AppendPrimitive(UDynamicMesh* TargetMesh, const FTransform& Transform) const
{
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
		TargetMesh, PrimitiveOptions, Transform, DimensionX, DimensionY, DimensionZ, StepsX, StepsY, StepsZ, Origin);
}

void UPCGCreatePrimitiveSphereSettings::AppendPrimitive(UDynamicMesh* TargetMesh, const FTransform& Transform) const
{
	if (Topology == EPCGCreatePrimitiveSphereTopology::LatLong)
	{
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(
			TargetMesh, PrimitiveOptions, Transform, Radius, StepsPhi, StepsTheta, Origin);
	}
	else
	{
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereBox(
			TargetMesh, PrimitiveOptions, Transform, Radius, StepsX, StepsY, StepsZ, Origin);
	}
}

void UPCGCreatePrimitiveCapsuleSettings::AppendPrimitive(UDynamicMesh* TargetMesh, const FTransform& Transform) const
{
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCapsule(
		TargetMesh, PrimitiveOptions, Transform, Radius, LineLength, HemisphereSteps, CircleSteps, SegmentSteps, Origin);
}

void UPCGCreatePrimitiveCylinderSettings::AppendPrimitive(UDynamicMesh* TargetMesh, const FTransform& Transform) const
{
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(
		TargetMesh, PrimitiveOptions, Transform, Radius, Height, RadialSteps, HeightSteps, bCapped, Origin);
}

void UPCGCreatePrimitiveConeSettings::AppendPrimitive(UDynamicMesh* TargetMesh, const FTransform& Transform) const
{
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCone(
		TargetMesh, PrimitiveOptions, Transform, BaseRadius, TopRadius, Height, RadialSteps, HeightSteps, bCapped, Origin);
}

void UPCGCreatePrimitiveTorusSettings::AppendPrimitive(UDynamicMesh* TargetMesh, const FTransform& Transform) const
{
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendTorus(
		TargetMesh, PrimitiveOptions, Transform, RevolveOptions, MajorRadius, MinorRadius, MajorSteps, MinorSteps, Origin);
}

void UPCGCreatePrimitiveRectangleSettings::AppendPrimitive(UDynamicMesh* TargetMesh, const FTransform& Transform) const
{
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendRectangleXY(
		TargetMesh, PrimitiveOptions, Transform, DimensionX, DimensionY, StepsWidth, StepsHeight);
}

void UPCGCreatePrimitiveRoundedRectangleSettings::AppendPrimitive(UDynamicMesh* TargetMesh, const FTransform& Transform) const
{
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendRoundRectangleXY(
		TargetMesh, PrimitiveOptions, Transform, DimensionX, DimensionY, CornerRadius, StepsWidth, StepsHeight, StepsRound);
}

void UPCGCreatePrimitiveDiscSettings::AppendPrimitive(UDynamicMesh* TargetMesh, const FTransform& Transform) const
{
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendDisc(
		TargetMesh, PrimitiveOptions, Transform, Radius, AngleSteps, SpokeSteps, StartAngle, EndAngle, HoleRadius);
}

void UPCGCreatePrimitiveLinearStairsSettings::AppendPrimitive(UDynamicMesh* TargetMesh, const FTransform& Transform) const
{
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendLinearStairs(
		TargetMesh, PrimitiveOptions, Transform, StepWidth, StepHeight, StepDepth, NumSteps, bFloating);
}

void UPCGCreatePrimitiveCurvedStairsSettings::AppendPrimitive(UDynamicMesh* TargetMesh, const FTransform& Transform) const
{
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCurvedStairs(
		TargetMesh, PrimitiveOptions, Transform, StepWidth, StepHeight, InnerRadius, CurveAngle, NumSteps, bFloating);
}
