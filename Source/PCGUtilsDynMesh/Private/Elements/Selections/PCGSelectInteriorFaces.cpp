// Copyright Max Harris

#include "Elements/Selections/PCGSelectInteriorFaces.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"

#define LOCTEXT_NAMESPACE "PCGSelectInteriorFaces"

namespace
{
	struct FInteriorFacesEdgeKey
	{
		int32 A = INDEX_NONE;
		int32 B = INDEX_NONE;

		FInteriorFacesEdgeKey() = default;
		FInteriorFacesEdgeKey(int32 InA, int32 InB)
			: A(FMath::Min(InA, InB)), B(FMath::Max(InA, InB))
		{
		}

		friend bool operator==(const FInteriorFacesEdgeKey& L, const FInteriorFacesEdgeKey& R)
		{
			return L.A == R.A && L.B == R.B;
		}
		friend uint32 GetTypeHash(const FInteriorFacesEdgeKey& Key)
		{
			return HashCombineFast(::GetTypeHash(Key.A), ::GetTypeHash(Key.B));
		}
	};

	class FInteriorFacesVertexClusters
	{
	public:
		explicit FInteriorFacesVertexClusters(int32 MaxVertexID)
		{
			Parent.SetNumUninitialized(MaxVertexID);
			for (int32 Index = 0; Index < MaxVertexID; ++Index) { Parent[Index] = Index; }
		}

		int32 Find(int32 VertexID)
		{
			while (Parent[VertexID] != VertexID)
			{
				Parent[VertexID] = Parent[Parent[VertexID]];
				VertexID = Parent[VertexID];
			}
			return VertexID;
		}

		void Join(int32 A, int32 B)
		{
			A = Find(A);
			B = Find(B);
			if (A != B) { Parent[FMath::Max(A, B)] = FMath::Min(A, B); }
		}

	private:
		TArray<int32> Parent;
	};

	class FInteriorFacesSelectionOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FInteriorFacesSelectionOperation(const UPCGSelectInteriorFacesFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) || !Factory)
			{
				return false;
			}

			const UE::Geometry::FDynamicMesh3& Mesh = InSelectionContext.Mesh;
			FInteriorFacesVertexClusters Clusters(Mesh.MaxVertexID());
			const double Tolerance = FMath::Max(Factory->CoincidentVertexTolerance, UE_DOUBLE_KINDA_SMALL_NUMBER);
			const double ToleranceSquared = Tolerance * Tolerance;
			TMap<FIntVector, TArray<int32>> Grid;

			auto CellFor = [Tolerance](const FVector3d& Position)
			{
				return FIntVector(
					FMath::FloorToInt(Position.X / Tolerance),
					FMath::FloorToInt(Position.Y / Tolerance),
					FMath::FloorToInt(Position.Z / Tolerance));
			};

			for (const int32 VertexID : Mesh.VertexIndicesItr())
			{
				const FVector3d Position = Mesh.GetVertex(VertexID);
				const FIntVector Cell = CellFor(Position);
				for (int32 X = -1; X <= 1; ++X)
				for (int32 Y = -1; Y <= 1; ++Y)
				for (int32 Z = -1; Z <= 1; ++Z)
				{
					if (const TArray<int32>* Neighbors = Grid.Find(Cell + FIntVector(X, Y, Z)))
					{
						for (const int32 OtherVertexID : *Neighbors)
						{
							if (FVector3d::DistSquared(Position, Mesh.GetVertex(OtherVertexID)) <= ToleranceSquared)
							{
								Clusters.Join(VertexID, OtherVertexID);
							}
						}
					}
				}
				Grid.FindOrAdd(Cell).Add(VertexID);
			}

			for (const int32 TriangleID : Mesh.TriangleIndicesItr())
			{
				const UE::Geometry::FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
				const int32 A = Clusters.Find(Triangle.A);
				const int32 B = Clusters.Find(Triangle.B);
				const int32 C = Clusters.Find(Triangle.C);
				++GeometricEdgeUseCount.FindOrAdd(FInteriorFacesEdgeKey(A, B));
				++GeometricEdgeUseCount.FindOrAdd(FInteriorFacesEdgeKey(B, C));
				++GeometricEdgeUseCount.FindOrAdd(FInteriorFacesEdgeKey(C, A));
				TriangleClusters.Add(TriangleID, UE::Geometry::FIndex3i(A, B, C));
			}
			return true;
		}

		virtual bool TestElement(int32 TriangleID) const override
		{
			const UE::Geometry::FIndex3i* Triangle = TriangleClusters.Find(TriangleID);
			if (!Triangle) { return false; }
			const int32* AB = GeometricEdgeUseCount.Find(FInteriorFacesEdgeKey(Triangle->A, Triangle->B));
			const int32* BC = GeometricEdgeUseCount.Find(FInteriorFacesEdgeKey(Triangle->B, Triangle->C));
			const int32* CA = GeometricEdgeUseCount.Find(FInteriorFacesEdgeKey(Triangle->C, Triangle->A));
			return AB && BC && CA && *AB > 2 && *BC > 2 && *CA > 2;
		}

	private:
		TObjectPtr<const UPCGSelectInteriorFacesFactoryData> Factory;
		TMap<FInteriorFacesEdgeKey, int32> GeometricEdgeUseCount;
		TMap<int32, UE::Geometry::FIndex3i> TriangleClusters;
	};
}

#if WITH_EDITOR
FText UPCGSelectInteriorFacesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select | Interior Faces");
}

FText UPCGSelectInteriorFacesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Blender Select by Trait: Interior Faces. Selects faces for which every geometric edge has more than two "
		"face users. Coincident split vertices are treated as shared so the predicate works with DynMesh's "
		"strictly manifold topological-edge representation.");
}
#endif

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGSelectInteriorFacesFactoryData::CreateNativeOperationInternal() const
{
	return MakeShared<FInteriorFacesSelectionOperation>(this);
}

void UPCGSelectInteriorFacesFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		double Tolerance = CoincidentVertexTolerance;
		Ar << Tolerance;
	}
}

UPCGUtilsDynMeshFactoryData* UPCGSelectInteriorFacesSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	UPCGSelectInteriorFacesFactoryData* Factory = InFactory
		? Cast<UPCGSelectInteriorFacesFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGSelectInteriorFacesFactoryData>(InContext);
	if (!Factory) { return nullptr; }
	Factory->Priority = Priority;
	Factory->CoincidentVertexTolerance = CoincidentVertexTolerance;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
