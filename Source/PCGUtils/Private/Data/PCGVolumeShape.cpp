// Copyright Max Harris

#include "Data/PCGVolumeShape.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshTransforms.h"
#include "Generators/MinimalBoxMeshGenerator.h"

namespace PCGVolumeShapePrivate
{
	using UE::Geometry::FDynamicMesh3;

	double SignedVolumeTimesSix(const FDynamicMesh3& Mesh)
	{
		double Sum = 0.0;
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Mesh.GetTriVertices(TriangleID, A, B, C);
			Sum += A.Dot(B.Cross(C));
		}
		return Sum;
	}

	void BuildBox(const FVector& Extent, FDynamicMesh3& OutMesh)
	{
		UE::Geometry::FMinimalBoxMeshGenerator Generator;
		Generator.Box = UE::Geometry::FOrientedBox3d(FVector3d::Zero(), FVector3d(Extent));
		Generator.Generate();
		OutMesh = FDynamicMesh3(&Generator);
	}

	/** The sign of an engine-built box's signed volume: what outward-facing means for the meshes built here. */
	double OutwardSign()
	{
		static const double Sign = []
		{
			FDynamicMesh3 Box;
			BuildBox(FVector(1.0), Box);
			return SignedVolumeTimesSix(Box) >= 0.0 ? 1.0 : -1.0;
		}();
		return Sign;
	}

	/**
	 * Revolves a profile around Z. Profile entries are (radius, z) from the bottom to the top; the first and last
	 * have radius 0 and become single pole vertices, every other entry becomes a ring of Sides vertices.
	 */
	void BuildLathe(TConstArrayView<FVector2d> Profile, int32 Sides, FDynamicMesh3& OutMesh)
	{
		OutMesh = FDynamicMesh3();
		const int32 NumRings = Profile.Num() - 2;
		if (NumRings < 1 || Sides < 3)
		{
			return;
		}

		const int32 Bottom = OutMesh.AppendVertex(FVector3d(0.0, 0.0, Profile[0].Y));
		TArray<int32> RingStart;
		for (int32 Ring = 0; Ring < NumRings; ++Ring)
		{
			const FVector2d RZ = Profile[Ring + 1];
			RingStart.Add(OutMesh.MaxVertexID());
			for (int32 Side = 0; Side < Sides; ++Side)
			{
				const double Angle = UE_DOUBLE_TWO_PI * Side / Sides;
				OutMesh.AppendVertex(FVector3d(RZ.X * FMath::Cos(Angle), RZ.X * FMath::Sin(Angle), RZ.Y));
			}
		}
		const int32 Top = OutMesh.AppendVertex(FVector3d(0.0, 0.0, Profile.Last().Y));

		auto RingVertex = [&](int32 Ring, int32 Side) { return RingStart[Ring] + (Side % Sides); };
		for (int32 Side = 0; Side < Sides; ++Side)
		{
			OutMesh.AppendTriangle(Bottom, RingVertex(0, Side + 1), RingVertex(0, Side));
			for (int32 Ring = 0; Ring + 1 < NumRings; ++Ring)
			{
				OutMesh.AppendTriangle(RingVertex(Ring, Side), RingVertex(Ring, Side + 1), RingVertex(Ring + 1, Side + 1));
				OutMesh.AppendTriangle(RingVertex(Ring, Side), RingVertex(Ring + 1, Side + 1), RingVertex(Ring + 1, Side));
			}
			OutMesh.AppendTriangle(Top, RingVertex(NumRings - 1, Side), RingVertex(NumRings - 1, Side + 1));
		}

		if (SignedVolumeTimesSix(OutMesh) * OutwardSign() < 0.0)
		{
			OutMesh.ReverseOrientation();
		}
	}

	/** A quarter arc from the side (angle 0) towards a pole, as (radius, z) around a centre at CenterZ. */
	void AppendArc(TArray<FVector2d>& Profile, double Radius, double CenterZ, int32 Steps, bool bUpwards)
	{
		for (int32 Step = 1; Step < Steps; ++Step)
		{
			const double Angle = UE_DOUBLE_HALF_PI * Step / Steps;
			Profile.Add(FVector2d(Radius * FMath::Cos(Angle), CenterZ + (bUpwards ? 1.0 : -1.0) * Radius * FMath::Sin(Angle)));
		}
	}
}

bool FPCGVolumeShapeDesc::ContainsLocal(const FVector& P) const
{
	switch (Shape)
	{
	case EPCGVolumeShape::Box:
		return FMath::Abs(P.X) <= BoxExtent.X && FMath::Abs(P.Y) <= BoxExtent.Y && FMath::Abs(P.Z) <= BoxExtent.Z;
	case EPCGVolumeShape::Sphere:
		return P.SizeSquared() <= Radius * Radius;
	case EPCGVolumeShape::Cylinder:
		return FMath::Abs(P.Z) <= HalfHeight && P.SizeSquared2D() <= Radius * Radius;
	case EPCGVolumeShape::Cone:
	{
		if (FMath::Abs(P.Z) > HalfHeight || HalfHeight <= 0.0)
		{
			return false;
		}
		// Full radius at the base (-HalfHeight), none at the tip (+HalfHeight).
		const double RadiusAtZ = Radius * (HalfHeight - P.Z) / (2.0 * HalfHeight);
		return P.SizeSquared2D() <= RadiusAtZ * RadiusAtZ;
	}
	case EPCGVolumeShape::Capsule:
	{
		const double SegmentHalf = FMath::Max(0.0, HalfHeight - Radius);
		const FVector Nearest(0.0, 0.0, FMath::Clamp(P.Z, -SegmentHalf, SegmentHalf));
		return FVector::DistSquared(P, Nearest) <= Radius * Radius;
	}
	default:
		return false;
	}
}

bool FPCGVolumeShapeDesc::Contains(const FVector& WorldPosition) const
{
	return ContainsLocal(ShapeToWorld.InverseTransformPosition(WorldPosition));
}

FBox FPCGVolumeShapeDesc::GetLocalBounds() const
{
	switch (Shape)
	{
	case EPCGVolumeShape::Box:
		return FBox(-BoxExtent, BoxExtent);
	case EPCGVolumeShape::Sphere:
		return FBox(FVector(-Radius), FVector(Radius));
	case EPCGVolumeShape::Cylinder:
	case EPCGVolumeShape::Cone:
		return FBox(FVector(-Radius, -Radius, -HalfHeight), FVector(Radius, Radius, HalfHeight));
	case EPCGVolumeShape::Capsule:
	{
		const double Half = FMath::Max(HalfHeight, Radius);
		return FBox(FVector(-Radius, -Radius, -Half), FVector(Radius, Radius, Half));
	}
	default:
		return FBox(ForceInit);
	}
}

FBox FPCGVolumeShapeDesc::GetWorldBounds() const
{
	const FBox Local = GetLocalBounds();
	if (!Local.IsValid)
	{
		return Local;
	}
	FBox World(ForceInit);
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector P((Corner & 1) ? Local.Max.X : Local.Min.X, (Corner & 2) ? Local.Max.Y : Local.Min.Y, (Corner & 4) ? Local.Max.Z : Local.Min.Z);
		World += ShapeToWorld.TransformPosition(P);
	}
	return World;
}

void FPCGVolumeShapeDesc::BuildMesh(UE::Geometry::FDynamicMesh3& OutMesh, bool bWorldSpace) const
{
	using namespace PCGVolumeShapePrivate;
	OutMesh = UE::Geometry::FDynamicMesh3();
	const int32 NumSides = FMath::Max(3, Sides);
	const int32 ArcSteps = FMath::Max(2, NumSides / 4);
	TArray<FVector2d> Profile;

	switch (Shape)
	{
	case EPCGVolumeShape::Box:
		BuildBox(BoxExtent, OutMesh);
		break;
	case EPCGVolumeShape::Sphere:
		Profile.Add(FVector2d(0.0, -Radius));
		for (int32 Step = 1; Step < 2 * ArcSteps; ++Step)
		{
			const double Angle = -UE_DOUBLE_HALF_PI + UE_DOUBLE_PI * Step / (2 * ArcSteps);
			Profile.Add(FVector2d(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle)));
		}
		Profile.Add(FVector2d(0.0, Radius));
		BuildLathe(Profile, NumSides, OutMesh);
		break;
	case EPCGVolumeShape::Cylinder:
		Profile = { FVector2d(0.0, -HalfHeight), FVector2d(Radius, -HalfHeight), FVector2d(Radius, HalfHeight), FVector2d(0.0, HalfHeight) };
		BuildLathe(Profile, NumSides, OutMesh);
		break;
	case EPCGVolumeShape::Cone:
		Profile = { FVector2d(0.0, -HalfHeight), FVector2d(Radius, -HalfHeight), FVector2d(0.0, HalfHeight) };
		BuildLathe(Profile, NumSides, OutMesh);
		break;
	case EPCGVolumeShape::Capsule:
	{
		const double SegmentHalf = FMath::Max(0.0, HalfHeight - Radius);
		Profile.Add(FVector2d(0.0, -SegmentHalf - Radius));
		TArray<FVector2d> LowerArc;
		AppendArc(LowerArc, Radius, -SegmentHalf, ArcSteps, /*bUpwards=*/false);
		for (int32 Index = LowerArc.Num() - 1; Index >= 0; --Index)
		{
			Profile.Add(LowerArc[Index]);
		}
		Profile.Add(FVector2d(Radius, -SegmentHalf));
		if (SegmentHalf > 0.0)
		{
			Profile.Add(FVector2d(Radius, SegmentHalf));
		}
		AppendArc(Profile, Radius, SegmentHalf, ArcSteps, /*bUpwards=*/true);
		Profile.Add(FVector2d(0.0, SegmentHalf + Radius));
		BuildLathe(Profile, NumSides, OutMesh);
		break;
	}
	default:
		return;
	}

	if (bWorldSpace && OutMesh.TriangleCount() > 0)
	{
		MeshTransforms::ApplyTransform(OutMesh, UE::Geometry::FTransformSRT3d(ShapeToWorld), /*bReverseOrientationIfNeeded=*/true);
	}
}
