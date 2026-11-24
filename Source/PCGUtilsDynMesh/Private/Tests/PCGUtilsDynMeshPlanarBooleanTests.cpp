// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Curve/GeneralPolygon2.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Topology/PCGDynMeshPlanarBoolean.h"
#include "Geometry/PCGUtilsDynMeshPlanarBoolean.h"
#include "Materials/Material.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshPlanarBooleanTests
{
	using UE::Geometry::FDynamicMesh3;
	using UE::Geometry::FIndex3i;
	using UE::Geometry::FGeneralPolygon2d;
	using UE::Geometry::FPolygon2d;

	/**
	 * A grid of square cells facing +Z (clockwise from above, the GeometryCore convention). KeepCell drops cells
	 * (holes). Optional attributes: UV = (x, y) / 100, colour = (x / 100, y / 100, 0, 1), normals +Z, material 0
	 * left of MaterialSplitX and 1 right of it; PolyGroup = row index.
	 */
	FDynamicMesh3 PlanarTestGrid(double X0, double Y0, double Cell, int32 NX, int32 NY,
		TFunctionRef<double(double, double)> Z, TFunctionRef<bool(int32, int32)> KeepCell, bool bAttributes, double MaterialSplitX = 1.0e9)
	{
		FDynamicMesh3 M;
		M.EnableTriangleGroups();
		if (bAttributes)
		{
			M.EnableAttributes();
			M.Attributes()->EnableMaterialID();
			M.Attributes()->EnablePrimaryColors();
		}
		auto VI = [NX](int32 I, int32 J) { return J * (NX + 1) + I; };
		for (int32 J = 0; J <= NY; ++J)
		{
			for (int32 I = 0; I <= NX; ++I)
			{
				const double X = X0 + I * Cell, Y = Y0 + J * Cell;
				M.AppendVertex(FVector3d(X, Y, Z(X, Y)));
			}
		}
		for (int32 J = 0; J < NY; ++J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				if (!KeepCell(I, J)) { continue; }
				const int32 A = VI(I, J), B = VI(I + 1, J), C = VI(I + 1, J + 1), D = VI(I, J + 1);
				M.AppendTriangle(A, C, B, J);
				M.AppendTriangle(A, D, C, J);
			}
		}
		if (bAttributes)
		{
			UE::Geometry::FDynamicMeshUVOverlay* UV = M.Attributes()->PrimaryUV();
			UE::Geometry::FDynamicMeshNormalOverlay* N = M.Attributes()->PrimaryNormals();
			UE::Geometry::FDynamicMeshColorOverlay* Col = M.Attributes()->PrimaryColors();
			TArray<int32> UVE, NE, CE;
			for (int32 Vid = 0; Vid < M.MaxVertexID(); ++Vid)
			{
				const FVector3d P = M.GetVertex(Vid);
				UVE.Add(UV->AppendElement(FVector2f(P.X / 100.0, P.Y / 100.0)));
				NE.Add(N->AppendElement(FVector3f(0.0f, 0.0f, 1.0f)));
				CE.Add(Col->AppendElement(FVector4f(P.X / 100.0, P.Y / 100.0, 0.0f, 1.0f)));
			}
			for (const int32 Tid : M.TriangleIndicesItr())
			{
				const FIndex3i T = M.GetTriangle(Tid);
				UV->SetTriangle(Tid, FIndex3i(UVE[T.A], UVE[T.B], UVE[T.C]));
				N->SetTriangle(Tid, FIndex3i(NE[T.A], NE[T.B], NE[T.C]));
				Col->SetTriangle(Tid, FIndex3i(CE[T.A], CE[T.B], CE[T.C]));
				FVector3d A, B, C; M.GetTriVertices(Tid, A, B, C);
				M.Attributes()->GetMaterialID()->SetValue(Tid, ((A + B + C) / 3.0).X < MaterialSplitX ? 0 : 1);
			}
		}
		return M;
	}

	double PlanarTestFlat(double, double) { return 0.0; }
	bool PlanarTestAllCells(int32, int32) { return true; }

	/** Square of Size (cells of 10) centred at C, with an optional centred square hole of HoleCells cells. */
	FDynamicMesh3 PlanarTestSquare(FVector2d C, int32 Cells, int32 HoleCells = 0, bool bAttributes = false, double Z = 0.0)
	{
		const int32 H0 = (Cells - HoleCells) / 2, H1 = H0 + HoleCells;
		return PlanarTestGrid(C.X - Cells * 5.0, C.Y - Cells * 5.0, 10.0, Cells, Cells,
			[Z](double, double) { return Z; },
			[H0, H1, HoleCells](int32 I, int32 J) { return HoleCells == 0 || !(I >= H0 && I < H1 && J >= H0 && J < H1); }, bAttributes);
	}

	FGeneralPolygon2d PlanarTestSquarePolygon(FVector2d C, double Size, double HoleSize)
	{
		FPolygon2d Outer = FPolygon2d::MakeRectangle(C, Size, Size);
		if (Outer.IsClockwise()) { Outer.Reverse(); }
		FGeneralPolygon2d G(Outer);
		if (HoleSize > 0.0)
		{
			FPolygon2d Hole = FPolygon2d::MakeRectangle(C, HoleSize, HoleSize);
			if (!Hole.IsClockwise()) { Hole.Reverse(); }
			G.AddHole(Hole, false, false);
		}
		return G;
	}

	double PlanarTestPolygonArea(const TArray<FGeneralPolygon2d>& Polys)
	{
		double Sum = 0.0;
		for (const FGeneralPolygon2d& P : Polys)
		{
			Sum += FMath::Abs(P.GetOuter().SignedArea());
			for (const FPolygon2d& H : P.GetHoles()) { Sum -= FMath::Abs(H.SignedArea()); }
		}
		return Sum;
	}

	PCGUtilsPlanarBoolean::FPlanarFrame PlanarTestXY()
	{
		return PCGUtilsPlanarBoolean::FPlanarFrame();
	}

	UPCGDynamicMeshData* PlanarTestData(FDynamicMesh3&& Mesh, const TArray<UMaterialInterface*>& Materials = {})
	{
		auto* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(MoveTemp(Mesh), Materials);
		return Data;
	}

	FPCGTaggedData PlanarTestTagged(const UPCGData* Data, const TCHAR* Tag)
	{
		FPCGTaggedData Result;
		Result.Data = Data;
		if (Tag) { Result.Tags.Add(Tag); }
		return Result;
	}

	TArray<FPCGTaggedData> PlanarTestRun(UPCGDynMeshPlanarBooleanSettings* Settings, TArray<FPCGTaggedData> A, TArray<FPCGTaggedData> B)
	{
		FPCGDataCollection Input;
		for (FPCGTaggedData& Data : A) { Data.Pin = TEXT("InA"); Input.TaggedData.Add(Data); }
		for (FPCGTaggedData& Data : B) { Data.Pin = TEXT("InB"); Input.TaggedData.Add(Data); }
		Input.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			if (Element->Execute(Context.Get())) { return Context->OutputData.TaggedData; }
		}
		return {};
	}

	const FDynamicMesh3& PlanarTestMesh(const FPCGTaggedData& Tagged)
	{
		return *CastChecked<UPCGDynamicMeshData>(Tagged.Data)->GetDynamicMesh()->GetMeshPtr();
	}

	/** Height of the source surface under (X, Y): the triangle containing the point, interpolated. */
	bool PlanarTestSurfaceZ(const FDynamicMesh3& M, double X, double Y, double& OutZ)
	{
		for (const int32 Tid : M.TriangleIndicesItr())
		{
			FVector3d A, B, C; M.GetTriVertices(Tid, A, B, C);
			const FVector2d P(X, Y), PA(A.X, A.Y), PB(B.X, B.Y), PC(C.X, C.Y);
			auto Cross = [](const FVector2d& U, const FVector2d& V) { return U.X * V.Y - U.Y * V.X; };
			const double D = Cross(PB - PA, PC - PA);
			if (FMath::Abs(D) < 1.0e-12) { continue; }
			const double WA = Cross(PB - P, PC - P) / D, WB = Cross(PC - P, PA - P) / D, WC = 1.0 - WA - WB;
			if (WA >= -1.0e-6 && WB >= -1.0e-6 && WC >= -1.0e-6) { OutZ = A.Z * WA + B.Z * WB + C.Z * WC; return true; }
		}
		return false;
	}

	/** Boundary edges whose midpoint is not on the rectangle [X0,X1] x [Y0,Y1]'s edge. */
	int32 PlanarTestInteriorBoundaryEdges(const FDynamicMesh3& M, double X0, double Y0, double X1, double Y1, FVector3d* OutFirst = nullptr)
	{
		int32 Count = 0;
		for (const int32 Eid : M.EdgeIndicesItr())
		{
			if (!M.IsBoundaryEdge(Eid)) { continue; }
			const UE::Geometry::FIndex2i EV = M.GetEdgeV(Eid);
			const FVector3d Mid = (M.GetVertex(EV.A) + M.GetVertex(EV.B)) * 0.5;
			const bool bOnRect = FMath::IsNearlyEqual(Mid.X, X0, 1.0e-3) || FMath::IsNearlyEqual(Mid.X, X1, 1.0e-3) ||
				FMath::IsNearlyEqual(Mid.Y, Y0, 1.0e-3) || FMath::IsNearlyEqual(Mid.Y, Y1, 1.0e-3);
			if (!bOnRect) { if (Count == 0 && OutFirst) { *OutFirst = Mid; } ++Count; }
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshPlanarBooleanCoplanarAreasTest,
	"PCGUtils.DynMesh.PlanarBoolean.CoplanarAreasMatchClipper", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshPlanarBooleanCoplanarAreasTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPlanarBooleanTests;
	using namespace UE::Geometry;
	const FVector2d CA(50.0, 50.0), CB(100.0, 80.0);
	const TArray<FGeneralPolygon2d> PA = { PlanarTestSquarePolygon(CA, 100.0, 40.0) };
	const TArray<FGeneralPolygon2d> PB = { PlanarTestSquarePolygon(CB, 100.0, 40.0) };
	double Expected[4];
	{
		TArray<FGeneralPolygon2d> R;
		PolygonsUnion({ PA[0], PB[0] }, R, false); Expected[0] = PlanarTestPolygonArea(R); R.Reset();
		PolygonsIntersection(PA, PB, R); Expected[1] = PlanarTestPolygonArea(R); R.Reset();
		PolygonsDifference(PA, PB, R); Expected[2] = PlanarTestPolygonArea(R); R.Reset();
		PolygonsExclusiveOr(PA, PB, R); Expected[3] = PlanarTestPolygonArea(R);
	}
	TestTrue(TEXT("Clipper reference areas are sensible"), Expected[0] > Expected[2] && Expected[2] > Expected[1] && Expected[1] > 0.0);

	const EPCGDynMeshPlanarBooleanOperation Ops[4] = { EPCGDynMeshPlanarBooleanOperation::Union, EPCGDynMeshPlanarBooleanOperation::Intersection,
		EPCGDynMeshPlanarBooleanOperation::Difference, EPCGDynMeshPlanarBooleanOperation::ExclusiveOr };
	for (int32 Tri = 0; Tri < 2; ++Tri)
	{
		for (int32 Overlap = 0; Overlap < 2; ++Overlap)
		{
			for (int32 OpIndex = 0; OpIndex < 4; ++OpIndex)
			{
				auto* Settings = NewObject<UPCGDynMeshPlanarBooleanSettings>();
				Settings->Operation = Ops[OpIndex];
				Settings->Triangulation = Tri == 0 ? EPCGDynMeshPlanarBooleanTriangulation::Delaunay : EPCGDynMeshPlanarBooleanTriangulation::KeepSourceEdges;
				Settings->OverlapSource = Overlap == 0 ? EPCGDynMeshPlanarBooleanOverlap::A : EPCGDynMeshPlanarBooleanOverlap::B;
				const auto Out = PlanarTestRun(Settings, { PlanarTestTagged(PlanarTestData(PlanarTestSquare(CA, 10, 4)), TEXT("A")) },
					{ PlanarTestTagged(PlanarTestData(PlanarTestSquare(CB, 10, 4)), TEXT("B")) });
				const FString What = FString::Printf(TEXT("%s, %s, overlap from %s"), *UEnum::GetDisplayValueAsText(Ops[OpIndex]).ToString(),
					Tri == 0 ? TEXT("Delaunay") : TEXT("Keep Source Edges"), Overlap == 0 ? TEXT("A") : TEXT("B"));
				if (!TestEqual(*FString::Printf(TEXT("%s: one output"), *What), Out.Num(), 1)) { continue; }
				const FDynamicMesh3& M = PlanarTestMesh(Out[0]);
				const double Area = PCGUtilsPlanarBoolean::ProjectedArea(M, PlanarTestXY());
				TestTrue(*FString::Printf(TEXT("%s: area %.3f matches Clipper %.3f"), *What, Area, Expected[OpIndex]),
					FMath::IsNearlyEqual(Area, Expected[OpIndex], Expected[OpIndex] * 1.0e-6 + 1.0e-3));
				double MaxZ = 0.0;
				for (const int32 Vid : M.VertexIndicesItr()) { MaxZ = FMath::Max(MaxZ, FMath::Abs(M.GetVertex(Vid).Z)); }
				TestTrue(*FString::Printf(TEXT("%s: stays on the plane"), *What), MaxZ < 1.0e-6);
				// Union and Exclusive weld A and B into one mesh, so the only open edges are the result's outline.
				// A crack or a T-junction leaves an outline vertex with an odd number of outline edges (Exclusive
				// legitimately pinches to four where the outlines cross).
				TMap<int32, int32> BoundaryValence;
				for (const int32 Eid : M.EdgeIndicesItr())
				{
					if (!M.IsBoundaryEdge(Eid)) { continue; }
					const UE::Geometry::FIndex2i EV = M.GetEdgeV(Eid);
					BoundaryValence.FindOrAdd(EV.A)++; BoundaryValence.FindOrAdd(EV.B)++;
				}
				int32 Odd = 0;
				for (const TPair<int32, int32>& V : BoundaryValence) { if (V.Value % 2 != 0) { ++Odd; } }
				TestEqual(*FString::Printf(TEXT("%s: every outline vertex joins an even number of outline edges (no cracks or T-junctions)"), *What), Odd, 0);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshPlanarBooleanAttributesTest,
	"PCGUtils.DynMesh.PlanarBoolean.AttributesAcrossCut", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshPlanarBooleanAttributesTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPlanarBooleanTests;
	UMaterialInterface* MatA0 = NewObject<UMaterial>();
	UMaterialInterface* MatA1 = NewObject<UMaterial>();
	UMaterialInterface* MatB = NewObject<UMaterial>();

	for (int32 Tri = 0; Tri < 2; ++Tri)
	{
		for (int32 Mode = 0; Mode < 3; ++Mode)
		{
			const FString What = FString::Printf(TEXT("%s, inheritance %d"), Tri == 0 ? TEXT("Delaunay") : TEXT("Keep Source Edges"), Mode);
			UPCGDynamicMeshData* A = PlanarTestData(PlanarTestGrid(0.0, 0.0, 10.0, 10, 10, PlanarTestFlat, PlanarTestAllCells, true, 50.0), { MatA0, MatA1 });
			A->MutableMetadata()->FindOrCreateAttribute<int32>(FPCGAttributeIdentifier(TEXT("Foo"), PCGMetadataDomainID::Data), 7, false, true);
			UPCGDynamicMeshData* B = PlanarTestData(PlanarTestGrid(78.0, 33.0, 10.0, 4, 3, PlanarTestFlat, PlanarTestAllCells, true), { MatB });

			auto* Settings = NewObject<UPCGDynMeshPlanarBooleanSettings>();
			Settings->Operation = EPCGDynMeshPlanarBooleanOperation::Union;
			Settings->Triangulation = Tri == 0 ? EPCGDynMeshPlanarBooleanTriangulation::Delaunay : EPCGDynMeshPlanarBooleanTriangulation::KeepSourceEdges;
			Settings->VertexAttributeInheritance = static_cast<EPCGDynMeshPlanarBooleanInheritance>(Mode);
			const auto Out = PlanarTestRun(Settings, { PlanarTestTagged(A, TEXT("Primary")) }, { PlanarTestTagged(B, TEXT("Operand")) });
			if (!TestEqual(*FString::Printf(TEXT("%s: one output"), *What), Out.Num(), 1)) { continue; }
			const UPCGDynamicMeshData* OutData = CastChecked<UPCGDynamicMeshData>(Out[0].Data);
			TestTrue(*FString::Printf(TEXT("%s: output is a copy, not InA itself"), *What), OutData != A);
			TestTrue(*FString::Printf(TEXT("%s: keeps InA's tag"), *What), Out[0].Tags.Contains(TEXT("Primary")));
			const FPCGMetadataAttributeBase* Foo = OutData->ConstMetadata()->GetConstAttribute(FPCGAttributeIdentifier(TEXT("Foo"), PCGMetadataDomainID::Data));
			TestTrue(*FString::Printf(TEXT("%s: keeps InA's @Data attribute"), *What), Foo != nullptr &&
				static_cast<const FPCGMetadataAttribute<int32>*>(Foo)->GetValueFromItemKey(PCGFirstEntryKey) == 7);
			TestEqual(*FString::Printf(TEXT("%s: B's material slot is appended to A's"), *What), OutData->GetMaterials().Num(), 3);
			if (OutData->GetMaterials().Num() == 3) { TestTrue(TEXT("Slot order: A's first, then B's"), OutData->GetMaterials()[0] == MatA0 && OutData->GetMaterials()[2] == MatB); }

			const FDynamicMesh3& M = *OutData->GetDynamicMesh()->GetMeshPtr();
			const auto* Attr = M.Attributes();
			if (!TestNotNull(*FString::Printf(TEXT("%s: attributes kept"), *What), Attr)) { continue; }
			int32 BadMaterial = 0, BadGroup = 0, BadUV = 0, BadColor = 0, BadNormal = 0, BTris = 0;
			for (const int32 Tid : M.TriangleIndicesItr())
			{
				FVector3d P0, P1, P2; M.GetTriVertices(Tid, P0, P1, P2);
				const FVector3d C = (P0 + P1 + P2) / 3.0;
				const bool bFromB = C.X > 100.0; // A wins the overlap; B only adds what is right of A.
				if (bFromB)
				{
					++BTris;
					BadMaterial += Attr->GetMaterialID()->GetValue(Tid) != 2;
					BadGroup += M.GetTriangleGroup(Tid) != 10 + FMath::FloorToInt32((C.Y - 33.0) / 10.0);
				}
				else
				{
					BadMaterial += Attr->GetMaterialID()->GetValue(Tid) != (C.X < 50.0 ? 0 : 1);
					BadGroup += M.GetTriangleGroup(Tid) != FMath::FloorToInt32(C.Y / 10.0);
				}
				const FIndex3i TV = M.GetTriangle(Tid);
				for (int32 K = 0; K < 3; ++K)
				{
					const FVector3d P = M.GetVertex(TV[K]);
					FVector2f UV; Attr->PrimaryUV()->GetTriElement(Tid, K, UV);
					FVector4f Col; Attr->PrimaryColors()->GetTriElement(Tid, K, Col);
					FVector3f N; Attr->PrimaryNormals()->GetTriElement(Tid, K, N);
					if (Mode == 0)
					{
						BadUV += !FVector2f(P.X / 100.0, P.Y / 100.0).Equals(UV, 1.0e-4f);
						BadColor += !FMath::IsNearlyEqual(Col.X, float(P.X / 100.0), 1.0e-4f);
					}
					else if (Mode == 1)
					{
						// Nearest copies a source corner: every value is a grid vertex's (a multiple of 0.1, or B's offset grid).
						const float U = UV.X * 10.0f, UB = (UV.X - 0.78f) * 10.0f;
						BadUV += !(FMath::IsNearlyEqual(U, FMath::RoundToFloat(U), 1.0e-3f) || FMath::IsNearlyEqual(UB, FMath::RoundToFloat(UB), 1.0e-3f));
					}
					else
					{
						BadUV += !UV.IsNearlyZero(1.0e-6f);
					}
					BadNormal += N.Z < 0.999f;
				}
			}
			TestTrue(*FString::Printf(TEXT("%s: B contributes triangles right of A"), *What), BTris > 0);
			TestEqual(*FString::Printf(TEXT("%s: every triangle has its source triangle's material (B's remapped to slot 2)"), *What), BadMaterial, 0);
			TestEqual(*FString::Printf(TEXT("%s: every triangle has its source PolyGroup (B's offset past A's)"), *What), BadGroup, 0);
			TestEqual(*FString::Printf(TEXT("%s: UVs follow the inheritance mode"), *What), BadUV, 0);
			TestEqual(*FString::Printf(TEXT("%s: colours interpolate"), *What), BadColor, 0);
			TestEqual(*FString::Printf(TEXT("%s: normals stay up"), *What), BadNormal, 0);
		}
	}

	// Difference keeps A's untouched triangles exactly and never takes anything from B.
	{
		FDynamicMesh3 AMesh = PlanarTestGrid(0.0, 0.0, 10.0, 10, 10, PlanarTestFlat, PlanarTestAllCells, true, 50.0);
		FDynamicMesh3 BMesh = PlanarTestGrid(52.0, 47.0, 10.0, 3, 3, PlanarTestFlat, PlanarTestAllCells, true);
		PCGUtilsPlanarBoolean::FOptions Options;
		Options.Operation = PCGUtilsPlanarBoolean::EOperation::Difference;
		PCGUtilsPlanarBoolean::FResult Result;
		TestTrue(TEXT("Difference succeeds"), PCGUtilsPlanarBoolean::Compute(AMesh, BMesh, PlanarTestXY(), Options, Result));
		TestTrue(TEXT("Difference cuts some of A"), Result.CutTrianglesA > 0 && Result.CutTrianglesA < AMesh.TriangleCount());
		const double DiffArea = PCGUtilsPlanarBoolean::ProjectedArea(Result.Mesh, PlanarTestXY());
		TestTrue(*FString::Printf(TEXT("Difference area %.3f is 9100"), DiffArea), FMath::IsNearlyEqual(DiffArea, 10000.0 - 900.0, 1.0e-3));
		int32 Kept = 0;
		TSet<FVector3d> Out;
		for (const int32 Tid : Result.Mesh.TriangleIndicesItr()) { FVector3d A, B, C; Result.Mesh.GetTriVertices(Tid, A, B, C); Out.Add((A + B + C) / 3.0); }
		for (const int32 Tid : AMesh.TriangleIndicesItr()) { FVector3d A, B, C; AMesh.GetTriVertices(Tid, A, B, C); Kept += Out.Contains((A + B + C) / 3.0); }
		TestEqual(TEXT("Every triangle of A not crossing B's outline is kept exactly"), Kept,
			AMesh.TriangleCount() - Result.CutTrianglesA - 2 * 2 * 2 /* the four cells wholly inside B are removed */);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshPlanarBooleanMultiDatasetTest,
	"PCGUtils.DynMesh.PlanarBoolean.MultiDatasetModes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshPlanarBooleanMultiDatasetTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPlanarBooleanTests;
	auto Areas = [this](const TArray<FPCGTaggedData>& Out)
	{
		TArray<double> Result;
		for (const FPCGTaggedData& D : Out) { Result.Add(FMath::RoundToDouble(PCGUtilsPlanarBoolean::ProjectedArea(PlanarTestMesh(D), PlanarTestXY()))); }
		return Result;
	};
	auto Run = [&](EPCGBooleanOperationMode Mode)
	{
		auto* Settings = NewObject<UPCGDynMeshPlanarBooleanSettings>();
		Settings->Operation = EPCGDynMeshPlanarBooleanOperation::Difference;
		Settings->Mode = Mode;
		return PlanarTestRun(Settings,
			{ PlanarTestTagged(PlanarTestData(PlanarTestSquare(FVector2d(50.0, 50.0), 10)), TEXT("A1")), PlanarTestTagged(PlanarTestData(PlanarTestSquare(FVector2d(300.0, 50.0), 10)), TEXT("A2")) },
			{ PlanarTestTagged(PlanarTestData(PlanarTestSquare(FVector2d(100.0, 50.0), 4)), TEXT("B1")), PlanarTestTagged(PlanarTestData(PlanarTestSquare(FVector2d(0.0, 50.0), 4)), TEXT("B2")) });
	};
	TestEqual(TEXT("Each A with each B pairs A1-B1 and A2-B2"), Areas(Run(EPCGBooleanOperationMode::EachAWithEachB)), TArray<double>({ 9200.0, 10000.0 }));
	TestEqual(TEXT("Sequentially subtracts both Bs from each A"), Areas(Run(EPCGBooleanOperationMode::EachAWithEachBSequentially)), TArray<double>({ 8400.0, 10000.0 }));
	TestEqual(TEXT("Every B gives the Cartesian product"), Areas(Run(EPCGBooleanOperationMode::EachAWithEveryB)), TArray<double>({ 9200.0, 9200.0, 10000.0, 10000.0 }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshPlanarBooleanProjectedTest,
	"PCGUtils.DynMesh.PlanarBoolean.ProjectedTrimOnCurvedSurface", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshPlanarBooleanProjectedTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPlanarBooleanTests;
	auto Surface = [](double X, double Y) { return 0.15 * X + 6.0 * FMath::Sin(Y / 25.0); };
	const FDynamicMesh3 AMesh = PlanarTestGrid(0.0, 0.0, 10.0, 20, 20, Surface, PlanarTestAllCells, true);

	for (int32 Keep = 0; Keep < 2; ++Keep)
	{
		for (int32 Tri = 0; Tri < 2; ++Tri)
		{
			auto* Settings = NewObject<UPCGDynMeshPlanarBooleanSettings>();
			Settings->Space = EPCGDynMeshPlanarBooleanSpace::Projected;
			Settings->TrimMode = Keep ? EPCGDynMeshPlanarBooleanTrim::KeepInside : EPCGDynMeshPlanarBooleanTrim::RemoveInside;
			Settings->Triangulation = Tri == 0 ? EPCGDynMeshPlanarBooleanTriangulation::Delaunay : EPCGDynMeshPlanarBooleanTriangulation::KeepSourceEdges;
			// B floats far above A and is a rotated square: only its footprint matters.
			FDynamicMesh3 BMesh = PlanarTestGrid(-30.0, -30.0, 10.0, 6, 6, [](double, double) { return 0.0; }, PlanarTestAllCells, false);
			UE::Geometry::FDynamicMesh3 Turned = BMesh;
			for (const int32 Vid : Turned.VertexIndicesItr())
			{
				const FVector3d P = Turned.GetVertex(Vid);
				const double Ang = FMath::DegreesToRadians(20.0);
				Turned.SetVertex(Vid, FVector3d(103.0 + P.X * FMath::Cos(Ang) - P.Y * FMath::Sin(Ang), 97.0 + P.X * FMath::Sin(Ang) + P.Y * FMath::Cos(Ang), 500.0 + P.X * 0.3));
			}
			const auto Out = PlanarTestRun(Settings, { PlanarTestTagged(PlanarTestData(FDynamicMesh3(AMesh)), TEXT("A")) }, { PlanarTestTagged(PlanarTestData(MoveTemp(Turned)), TEXT("B")) });
			const FString What = FString::Printf(TEXT("%s, %s"), Keep ? TEXT("Keep Inside") : TEXT("Remove Inside"), Tri == 0 ? TEXT("Delaunay") : TEXT("Keep Source Edges"));
			if (!TestEqual(*FString::Printf(TEXT("%s: one output"), *What), Out.Num(), 1)) { continue; }
			const FDynamicMesh3& M = PlanarTestMesh(Out[0]);
			const double Area = PCGUtilsPlanarBoolean::ProjectedArea(M, PlanarTestXY());
			const double Expected = Keep ? 3600.0 : 40000.0 - 3600.0;
			TestTrue(*FString::Printf(TEXT("%s: projected area %.3f is %.3f"), *What, Area, Expected), FMath::IsNearlyEqual(Area, Expected, 1.0e-2));
			int32 OffSurface = 0;
			double Worst = 0.0;
			for (const int32 Vid : M.VertexIndicesItr())
			{
				const FVector3d P = M.GetVertex(Vid);
				double Z;
				if (!PlanarTestSurfaceZ(AMesh, P.X, P.Y, Z) || FMath::Abs(Z - P.Z) > 1.0e-4) { ++OffSurface; Worst = FMath::Max(Worst, FMath::Abs(Z - P.Z)); }
			}
			TestEqual(*FString::Printf(TEXT("%s: every vertex lies on A's own surface (worst %.6f)"), *What, Worst), OffSurface, 0);
		}
	}

	// Stitch onto the curved surface: B is a patch of the same surface, so its outline lies on A within the
	// chord error; every seam vertex must be welded (shared), leaving no open edge inside A.
	{
		FDynamicMesh3 BMesh = PlanarTestGrid(72.5, 67.5, 7.5, 8, 8, Surface, PlanarTestAllCells, true);
		PCGUtilsPlanarBoolean::FPlanarFrame Frame;
		PCGUtilsPlanarBoolean::FPlanarFrame::FromNormal(FVector3d::ZeroVector, FVector3d::UnitZ(), Frame);
		PCGUtilsPlanarBoolean::FOptions Options;
		Options.Operation = PCGUtilsPlanarBoolean::EOperation::Difference;
		Options.OperandMode = PCGUtilsPlanarBoolean::EOperandMode::Whole;
		Options.bCheckCoplanar = false;
		Options.SeamTolerance = 0.5;
		PCGUtilsPlanarBoolean::FResult Result;
		TestTrue(TEXT("Curved stitch succeeds"), PCGUtilsPlanarBoolean::Compute(AMesh, BMesh, Frame, Options, Result));
		TestTrue(TEXT("Curved stitch welds seam vertices"), Result.SeamWelds > 0);
		TestEqual(TEXT("Curved stitch needs no walls"), Result.WallTriangles, 0);
		FVector3d First;
		const int32 Open = PlanarTestInteriorBoundaryEdges(Result.Mesh, 0.0, 0.0, 200.0, 200.0, &First);
		TestEqual(*FString::Printf(TEXT("Curved stitch leaves no open edge on the seam (first at %s)"), *First.ToString()), Open, 0);
		TestTrue(TEXT("Curved stitch keeps all of B"), FMath::IsNearlyEqual(PCGUtilsPlanarBoolean::ProjectedArea(Result.Mesh, Frame), 40000.0, 1.0e-2));
		// Keep B moves A's seam vertices onto B's chords: reported briefly, per run, never as an error.
		int32 MoveWarnings = 0, Errors = 0;
		for (const PCGUtilsPlanarBoolean::FMessage& Message : Result.Messages)
		{
			MoveWarnings += !Message.bError && Message.Text.Contains(TEXT("moved up to"));
			Errors += Message.bError;
			AddInfo(Message.Text);
		}
		TestTrue(TEXT("Curved stitch moves some green vertices to meet B"), Result.MovedSeamVertices > 0 && Result.MaxSeamMove > 0.0 && Result.MaxSeamMove <= Options.SeamTolerance);
		TestTrue(*FString::Printf(TEXT("Moves are reported per run, not per vertex (%d warnings for %d moved vertices)"), MoveWarnings, Result.MovedSeamVertices),
			MoveWarnings >= 1 && MoveWarnings < Result.MovedSeamVertices);
		TestEqual(TEXT("Moving green vertices is never an error"), Errors, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshPlanarBooleanErrorsTest,
	"PCGUtils.DynMesh.PlanarBoolean.SpecificErrors", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshPlanarBooleanErrorsTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPlanarBooleanTests;
	// A fold in B: one triangle flipped, so B covers that spot twice along the projection.
	{
		FDynamicMesh3 BMesh = PlanarTestSquare(FVector2d(50.0, 50.0), 3, 0, false, 100.0);
		BMesh.ReverseTriOrientation(4);
		auto* Settings = NewObject<UPCGDynMeshPlanarBooleanSettings>();
		Settings->Space = EPCGDynMeshPlanarBooleanSpace::Projected;
		AddExpectedError(TEXT("InB[0] folds along the projection: triangle 4"), EAutomationExpectedErrorFlags::Contains, 1, false);
		const auto Out = PlanarTestRun(Settings, { PlanarTestTagged(PlanarTestData(PlanarTestSquare(FVector2d(50.0, 50.0), 10)), TEXT("A")) },
			{ PlanarTestTagged(PlanarTestData(MoveTemp(BMesh)), TEXT("B")) });
		TestEqual(TEXT("A fold drops the result"), Out.Num(), 0);
	}
	// A fold in A over the trimmed area.
	{
		FDynamicMesh3 AMesh = PlanarTestSquare(FVector2d(50.0, 50.0), 10);
		AMesh.ReverseTriOrientation(90);
		auto* Settings = NewObject<UPCGDynMeshPlanarBooleanSettings>();
		Settings->Space = EPCGDynMeshPlanarBooleanSpace::Projected;
		AddExpectedError(TEXT("InA[0] folds along the projection: triangle 90"), EAutomationExpectedErrorFlags::Contains, 1, false);
		PlanarTestRun(Settings, { PlanarTestTagged(PlanarTestData(MoveTemp(AMesh)), TEXT("A")) },
			{ PlanarTestTagged(PlanarTestData(PlanarTestSquare(FVector2d(50.0, 50.0), 4, 0, false, 30.0)), TEXT("B")) });
	}
	// Off the plane, named by vertex and distance.
	{
		auto* Settings = NewObject<UPCGDynMeshPlanarBooleanSettings>();
		AddExpectedError(TEXT("InB[0] is not on the plane: vertex"), EAutomationExpectedErrorFlags::Contains, 1, false);
		PlanarTestRun(Settings, { PlanarTestTagged(PlanarTestData(PlanarTestSquare(FVector2d(50.0, 50.0), 10)), TEXT("A")) },
			{ PlanarTestTagged(PlanarTestData(PlanarTestSquare(FVector2d(80.0, 50.0), 4, 0, false, 5.0)), TEXT("B")) });
	}
	// A zero projection direction.
	{
		auto* Settings = NewObject<UPCGDynMeshPlanarBooleanSettings>();
		Settings->Space = EPCGDynMeshPlanarBooleanSpace::Projected;
		Settings->ProjectionDirection.Constant = FVector::ZeroVector;
		AddExpectedError(TEXT("Projection Direction for InA[0] is (0, 0, 0)"), EAutomationExpectedErrorFlags::Contains, 1, false);
		PlanarTestRun(Settings, { PlanarTestTagged(PlanarTestData(PlanarTestSquare(FVector2d(50.0, 50.0), 10)), TEXT("A")) },
			{ PlanarTestTagged(PlanarTestData(PlanarTestSquare(FVector2d(80.0, 50.0), 4)), TEXT("B")) });
	}
	// A missing @Data plane attribute names the setting and the attribute.
	{
		auto* Settings = NewObject<UPCGDynMeshPlanarBooleanSettings>();
		Settings->PlaneSource = EPCGDynMeshPlanarBooleanPlane::Explicit;
		Settings->PlaneNormal.Input = EPCGUtilsInputValueType::Attribute;
		AddExpectedError(TEXT("Plane Normal (input 0)"), EAutomationExpectedErrorFlags::Contains, 1, false);
		PlanarTestRun(Settings, { PlanarTestTagged(PlanarTestData(PlanarTestSquare(FVector2d(50.0, 50.0), 10)), TEXT("A")) },
			{ PlanarTestTagged(PlanarTestData(PlanarTestSquare(FVector2d(80.0, 50.0), 4)), TEXT("B")) });
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshPlanarBooleanGreenRampTest,
	"PCGUtils.DynMesh.PlanarBoolean.GreenRampStitchIsWatertight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshPlanarBooleanGreenRampTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPlanarBooleanTests;
	// A green, flat, 400 x 300, and a ramp surface that rests on it for its first 30 cm and then climbs 30 cm.
	const FDynamicMesh3 Green = PlanarTestGrid(0.0, 0.0, 10.0, 40, 30, PlanarTestFlat, PlanarTestAllCells, true);
	auto RampZ = [](double X, double) { return X <= 130.0 ? 0.0 : (X - 130.0) / 120.0 * 30.0; };
	const FDynamicMesh3 Ramp = PlanarTestGrid(100.0, 104.0, 10.0, 15, 6, RampZ, PlanarTestAllCells, true);

	for (int32 Tri = 0; Tri < 2; ++Tri)
	{
		PCGUtilsPlanarBoolean::FOptions Options;
		Options.Operation = PCGUtilsPlanarBoolean::EOperation::Difference;
		Options.OperandMode = PCGUtilsPlanarBoolean::EOperandMode::Whole;
		Options.bCheckCoplanar = false;
		Options.SeamTolerance = 0.1;
		Options.Triangulation = Tri == 0 ? PCGUtilsPlanarBoolean::ETriangulation::Delaunay : PCGUtilsPlanarBoolean::ETriangulation::KeepSourceEdges;
		PCGUtilsPlanarBoolean::FResult Result;
		const FString What = Tri == 0 ? TEXT("Delaunay") : TEXT("Keep Source Edges");
		if (!TestTrue(*FString::Printf(TEXT("%s: stitch succeeds"), *What), PCGUtilsPlanarBoolean::Compute(Green, Ramp, PlanarTestXY(), Options, Result))) { continue; }
		TestTrue(*FString::Printf(TEXT("%s: the flat foot is welded"), *What), Result.SeamWelds > 0);
		TestTrue(*FString::Printf(TEXT("%s: the raised sides and end get walls"), *What), Result.WallTriangles > 0);
		int32 Unadded = 0;
		for (const PCGUtilsPlanarBoolean::FMessage& Message : Result.Messages)
		{
			Unadded += Message.Text.Contains(TEXT("could not be added"));
			AddInfo(Message.Text);
		}
		TestEqual(*FString::Printf(TEXT("%s: every output triangle was added (no non-manifold edges)"), *What), Unadded, 0);
		FVector3d First;
		const int32 Open = PlanarTestInteriorBoundaryEdges(Result.Mesh, 0.0, 0.0, 400.0, 300.0, &First);
		TestEqual(*FString::Printf(TEXT("%s: no open edge left along the seam (first at %s)"), *What, *First.ToString()), Open, 0);
		TestTrue(*FString::Printf(TEXT("%s: the result covers the green once"), *What),
			FMath::IsNearlyEqual(PCGUtilsPlanarBoolean::ProjectedArea(Result.Mesh, PlanarTestXY()), 120000.0, 1.0e-2));
		// Ramp triangles are kept exactly.
		TestTrue(*FString::Printf(TEXT("%s: the ramp is in the output"), *What), Result.Mesh.TriangleCount() > Ramp.TriangleCount());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshPlanarBooleanSkirtedRampTest,
	"PCGUtils.DynMesh.PlanarBoolean.SkirtedRampStitchIsWatertight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshPlanarBooleanSkirtedRampTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshPlanarBooleanTests;
	// The green-ramp case, but the ramp has a skirt all round its outline, flaring slightly outward (85 degrees
	// steep) and hanging 5 cm below the green. Stitch must leave the skirt out, never refuse, and stitch the top.
	const FDynamicMesh3 Green = PlanarTestGrid(0.0, 0.0, 10.0, 40, 30, PlanarTestFlat, PlanarTestAllCells, true);
	auto RampZ = [](double X, double) { return X <= 130.0 ? 0.0 : (X - 130.0) / 120.0 * 30.0; };
	FDynamicMesh3 Ramp = PlanarTestGrid(100.0, 104.0, 10.0, 15, 6, RampZ, PlanarTestAllCells, true);
	const int32 SurfaceTriangles = Ramp.TriangleCount();
	const FVector3d Centre(175.0, 134.0, 0.0);
	TMap<int32, int32> Below;
	TArray<UE::Geometry::FIndex2i> Outline;
	for (const int32 Eid : Ramp.BoundaryEdgeIndicesItr())
	{
		const int32 Tid = Ramp.GetEdgeT(Eid).A;
		const FIndex3i T = Ramp.GetTriangle(Tid);
		const UE::Geometry::FIndex2i EV = Ramp.GetEdgeV(Eid);
		// Boundary edge in the triangle's own direction (A -> B).
		const int32 K = T.IndexOf(EV.A);
		Outline.Add(T[(K + 1) % 3] == EV.B ? EV : UE::Geometry::FIndex2i(EV.B, EV.A));
	}
	for (const UE::Geometry::FIndex2i& E : Outline)
	{
		for (const int32 V : { E.A, E.B })
		{
			if (Below.Contains(V)) { continue; }
			const FVector3d P = Ramp.GetVertex(V);
			const FVector3d Out = FVector3d(P.X - Centre.X, P.Y - Centre.Y, 0.0).GetSafeNormal();
			Below.Add(V, Ramp.AppendVertex(FVector3d(P.X, P.Y, -5.0) + Out * ((P.Z + 5.0) * 0.08)));
		}
	}
	int32 SkirtTriangles = 0;
	for (const UE::Geometry::FIndex2i& E : Outline)
	{
		SkirtTriangles += Ramp.AppendTriangle(E.B, E.A, Below[E.A]) >= 0;
		SkirtTriangles += Ramp.AppendTriangle(E.B, Below[E.A], Below[E.B]) >= 0;
	}
	TestEqual(TEXT("Fixture: the skirt is closed all round"), SkirtTriangles, Outline.Num() * 2);

	PCGUtilsPlanarBoolean::FOptions Options;
	Options.Operation = PCGUtilsPlanarBoolean::EOperation::Difference;
	Options.OperandMode = PCGUtilsPlanarBoolean::EOperandMode::Whole;
	Options.bCheckCoplanar = false;
	Options.SeamTolerance = 0.1;
	PCGUtilsPlanarBoolean::FResult Result;
	if (!TestTrue(TEXT("Skirted stitch succeeds (no refusal)"), PCGUtilsPlanarBoolean::Compute(Green, Ramp, PlanarTestXY(), Options, Result))) { return true; }
	TestEqual(TEXT("Every skirt face is left out"), Result.DroppedOperandWalls, SkirtTriangles);
	int32 Notes = 0, Errors = 0, Unadded = 0;
	for (const PCGUtilsPlanarBoolean::FMessage& Message : Result.Messages)
	{
		Notes += Message.bNote;
		Errors += Message.bError;
		Unadded += Message.Text.Contains(TEXT("could not be added"));
		AddInfo(Message.Text);
	}
	TestEqual(TEXT("One note says the skirt was left out"), Notes, 1);
	TestEqual(TEXT("No errors"), Errors, 0);
	TestEqual(TEXT("Every output triangle was added"), Unadded, 0);
	TestTrue(TEXT("The ramp's foot is welded"), Result.SeamWelds > 0);
	FVector3d First;
	const int32 Open = PlanarTestInteriorBoundaryEdges(Result.Mesh, 0.0, 0.0, 400.0, 300.0, &First);
	TestEqual(*FString::Printf(TEXT("No open edge left along the seam (first at %s)"), *First.ToString()), Open, 0);
	TestTrue(TEXT("The result covers the green once"), FMath::IsNearlyEqual(PCGUtilsPlanarBoolean::ProjectedArea(Result.Mesh, PlanarTestXY()), 120000.0, 1.0e-2));
	double MinZ = 0.0;
	for (const int32 Vid : Result.Mesh.VertexIndicesItr()) { MinZ = FMath::Min(MinZ, Result.Mesh.GetVertex(Vid).Z); }
	TestTrue(TEXT("Nothing of the skirt is left below the green"), MinZ > -1.0e-6);
	TestTrue(TEXT("The ramp's top surface is in the output"), Result.Mesh.TriangleCount() > SurfaceTriangles);

	// Through the node: Stitch with a skirted B logs no error and gives one output.
	auto* Settings = NewObject<UPCGDynMeshPlanarBooleanSettings>();
	Settings->Space = EPCGDynMeshPlanarBooleanSpace::Projected;
	Settings->bStitch = true;
	const auto Out = PlanarTestRun(Settings, { PlanarTestTagged(PlanarTestData(FDynamicMesh3(Green)), TEXT("A")) }, { PlanarTestTagged(PlanarTestData(MoveTemp(Ramp)), TEXT("B")) });
	if (TestEqual(TEXT("The node stitches a skirted ramp"), Out.Num(), 1))
	{
		TestEqual(TEXT("The node's result has no open seam"), PlanarTestInteriorBoundaryEdges(PlanarTestMesh(Out[0]), 0.0, 0.0, 400.0, 300.0), 0);
	}
	return true;
}

#endif
