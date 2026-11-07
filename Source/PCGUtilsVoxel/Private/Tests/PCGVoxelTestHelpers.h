// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGPointArrayData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Generators/MinimalBoxMeshGenerator.h"
#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"

namespace PCGVoxelTestHelpers
{
	/** Point data with one point per cell (or chunk, when Spans is given), @Data.VoxelSize set, and an int32 'Id' attribute (point index). */
	inline UPCGPointArrayData* MakeVoxels(const TArray<FIntVector>& Firsts, double VoxelSize = 100.0, const TArray<FIntVector>* Spans = nullptr)
	{
		UPCGPointArrayData* Points = NewObject<UPCGPointArrayData>();
		Points->SetNumPoints(Firsts.Num());
		Points->AllocateProperties(EPCGPointNativeProperties::All);
		FPCGMetadataDomain* Elements = Points->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
		FPCGMetadataAttribute<int32>* Id = Elements->CreateAttribute<int32>(TEXT("Id"), -1, false);
		FPCGMetadataAttribute<FVector>* SpanAttribute = Spans ? Elements->CreateAttribute<FVector>(TEXT("VoxelSpan"), FVector(1.0), false) : nullptr;
		if (FPCGMetadataDomain* Data = PCGUtilsDataDomain::GetMutableDataDomain(Points))
		{
			PCGUtilsDataDomain::WriteValue(*Data, TEXT("VoxelSize"), FVector(VoxelSize));
		}

		auto Transforms = Points->GetTransformValueRange();
		auto Mins = Points->GetBoundsMinValueRange();
		auto Maxs = Points->GetBoundsMaxValueRange();
		auto Seeds = Points->GetSeedValueRange();
		auto Keys = Points->GetMetadataEntryValueRange();
		for (int32 I = 0; I < Firsts.Num(); ++I)
		{
			const FIntVector Span = Spans ? (*Spans)[I] : FIntVector(1);
			const FVector Center = (FVector(Firsts[I]) + (FVector(Span) - FVector(1.0)) * 0.5) * VoxelSize;
			Transforms[I] = FTransform(Center);
			Mins[I] = -FVector(Span) * VoxelSize * 0.5;
			Maxs[I] = FVector(Span) * VoxelSize * 0.5;
			Seeds[I] = 1000 + I;
			Keys[I] = Elements->AddEntry();
			Id->SetValue(Keys[I], I);
			if (SpanAttribute)
			{
				SpanAttribute->SetValue(Keys[I], FVector((*Spans)[I]));
			}
		}
		return Points;
	}

	/** Every cell of an X by Y by Z block starting at the origin. */
	inline TArray<FIntVector> Block(int32 X, int32 Y, int32 Z)
	{
		TArray<FIntVector> Cells;
		for (int32 K = 0; K < Z; ++K)
			for (int32 J = 0; J < Y; ++J)
				for (int32 I = 0; I < X; ++I)
					Cells.Add(FIntVector(I, J, K));
		return Cells;
	}

	/** Runs an element on settings plus (pin, data) inputs, and returns its outputs. */
	template <typename TElement>
	TArray<FPCGTaggedData> Run(UPCGSettings* Settings, const TArray<TPair<FName, const UPCGData*>>& Inputs)
	{
		struct FExposed : TElement
		{
			using TElement::ExecuteInternal;
		};
		FPCGContext Context;
		Context.InputData.TaggedData.Emplace_GetRef().Data = Settings;
		for (const TPair<FName, const UPCGData*>& Input : Inputs)
		{
			FPCGTaggedData& Tagged = Context.InputData.TaggedData.Emplace_GetRef();
			Tagged.Data = Input.Value;
			Tagged.Pin = Input.Key;
			Tagged.Tags.Add(TEXT("KeptTag"));
		}
		FExposed().ExecuteInternal(&Context);
		return Context.OutputData.TaggedData;
	}

	inline const UE::Geometry::FDynamicMesh3* MeshOf(const TArray<FPCGTaggedData>& Outputs, int32 Index = 0)
	{
		const UPCGDynamicMeshData* Data = Outputs.IsValidIndex(Index) ? Cast<UPCGDynamicMeshData>(Outputs[Index].Data) : nullptr;
		return (Data && Data->GetDynamicMesh()) ? &Data->GetDynamicMesh()->GetMeshRef() : nullptr;
	}

	/** +1 when A . (B x C) summed over a closed mesh is positive for outward faces in this engine, else -1. */
	inline double OutwardSign()
	{
		UE::Geometry::FMinimalBoxMeshGenerator Generator;
		Generator.Box = UE::Geometry::FOrientedBox3d(FVector3d::Zero(), FVector3d(1.0));
		Generator.Generate();
		const UE::Geometry::FDynamicMesh3 Box(&Generator);
		double Six = 0.0;
		for (const int32 TriangleID : Box.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Box.GetTriVertices(TriangleID, A, B, C);
			Six += A.Dot(B.Cross(C));
		}
		return Six > 0.0 ? 1.0 : -1.0;
	}

	/** Enclosed volume, positive when the faces point outward. */
	inline double Volume(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		double Six = 0.0;
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Mesh.GetTriVertices(TriangleID, A, B, C);
			Six += A.Dot(B.Cross(C));
		}
		return Six / 6.0 * OutwardSign();
	}

	inline int32 OpenEdges(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		int32 Count = 0;
		for (const int32 EdgeID : Mesh.BoundaryEdgeIndicesItr())
		{
			(void)EdgeID;
			++Count;
		}
		return Count;
	}

	/** Vertex positions rounded and sorted, to compare two meshes' geometry regardless of vertex order. */
	inline TArray<FIntVector> SortedPositions(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		TArray<FIntVector> Out;
		for (const int32 VertexID : Mesh.VertexIndicesItr())
		{
			const FVector3d P = Mesh.GetVertex(VertexID);
			Out.Add(FIntVector(FMath::RoundToInt(P.X * 10.0), FMath::RoundToInt(P.Y * 10.0), FMath::RoundToInt(P.Z * 10.0)));
		}
		Out.Sort([](const FIntVector& A, const FIntVector& B) { return A.X != B.X ? A.X < B.X : (A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z); });
		return Out;
	}
}

#endif
