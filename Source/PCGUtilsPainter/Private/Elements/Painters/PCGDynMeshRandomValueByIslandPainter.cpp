// Copyright Max Harris

#include "Elements/Painters/PCGDynMeshRandomValueByIslandPainter.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "Helpers/PCGHelpers.h"
#include "Math/RandomStream.h"
#include "PCGContext.h"
#include "Selections/MeshConnectedComponents.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshRandomValueByIslandPainter"

namespace
{
	class FRandomValueByIslandOperation final : public FPCGUtilsDynMeshPainterOperation
	{
	public:
		explicit FRandomValueByIslandOperation(const UPCGDynMeshRandomValueByIslandPainterFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override
		{
			if (!FPCGUtilsDynMeshPainterOperation::Initialize(InPainterContext) || !Factory)
			{
				return false;
			}
			if (!InPainterContext.Mesh)
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("NoMesh", "Random Value by Mesh Island requires a canonical Dynamic Mesh target."), Context);
				return false;
			}
			MinValue = FMath::Min(Factory->MinValue, Factory->MaxValue);
			MaxValue = FMath::Max(Factory->MinValue, Factory->MaxValue);
			return true;
		}

		virtual bool Prepare(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override
		{
			using namespace UE::Geometry;
			const FDynamicMesh3& Mesh = *InPainterContext.Mesh;

			VertexToValue.Init(MinValue, Mesh.MaxVertexID());
			if (Mesh.VertexCount() == 0)
			{
				return true;
			}

			auto IslandValueForKey = [this](int32 IslandKey) -> float
			{
				if (FMath::IsNearlyEqual(MinValue, MaxValue))
				{
					return MinValue;
				}
				const FRandomStream Stream(PCGHelpers::ComputeSeed(Factory->Seed, IslandKey));
				return static_cast<float>(Stream.FRandRange(MinValue, MaxValue));
			};

			TBitArray<> Assigned(false, Mesh.MaxVertexID());

			FMeshConnectedComponents Components(&Mesh);
			Components.FindConnectedVertices();
			for (const FMeshConnectedComponents::FComponent& Component : Components.Components)
			{
				if (Component.Indices.IsEmpty())
				{
					continue;
				}

				// Island key: the smallest valid canonical vertex ID in the component. Independent of component
				// discovery order, so inserting geometry elsewhere does not shift other islands' values.
				int32 IslandKey = Component.Indices[0];
				for (const int32 VertexID : Component.Indices)
				{
					IslandKey = FMath::Min(IslandKey, VertexID);
				}

				const float IslandValue = IslandValueForKey(IslandKey);
				for (const int32 VertexID : Component.Indices)
				{
					if (VertexToValue.IsValidIndex(VertexID))
					{
						VertexToValue[VertexID] = IslandValue;
						Assigned[VertexID] = true;
					}
				}
			}

			// Any valid vertex the connected-vertex search did not place in a component (an edge-isolated loose
			// vertex) is its own single-vertex island, keyed by its own ID — a real per-island value, not a
			// silent MinValue.
			for (const int32 VertexID : Mesh.VertexIndicesItr())
			{
				if (!Assigned[VertexID])
				{
					VertexToValue[VertexID] = IslandValueForKey(VertexID);
				}
			}
			return true;
		}

		virtual EPCGUtilsDynMeshPainterValueType GetOutputType() const override
		{
			return EPCGUtilsDynMeshPainterValueType::Scalar;
		}

		virtual FPCGUtilsDynMeshPainterValue Evaluate(
			const FPCGUtilsDynMeshPainterSample& Sample) const override
		{
			return FPCGUtilsDynMeshPainterValue::MakeScalar(
				VertexToValue.IsValidIndex(Sample.VertexID) ? VertexToValue[Sample.VertexID] : MinValue);
		}

	private:
		TObjectPtr<const UPCGDynMeshRandomValueByIslandPainterFactoryData> Factory;
		float MinValue = 0.0f;
		float MaxValue = 1.0f;
		TArray<float> VertexToValue;
	};
}

TSharedPtr<FPCGUtilsDynMeshPainterOperation>
UPCGDynMeshRandomValueByIslandPainterFactoryData::CreateOperationInternal() const
{
	return MakeShared<FRandomValueByIslandOperation>(this);
}

void UPCGDynMeshRandomValueByIslandPainterFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}
	int32 SeedValue = Seed;
	float MinV = MinValue;
	float MaxV = MaxValue;
	Ar << SeedValue;
	Ar << MinV;
	Ar << MaxV;
}

#if WITH_EDITOR
FText UPCGDynMeshRandomValueByIslandPainterProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Painter|Random Value By Mesh Island");
}

FText UPCGDynMeshRandomValueByIslandPainterProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Assigns one deterministic random scalar to every connected vertex component of the target mesh. Islands "
		"are the canonical base-topology components, so UV / normal / colour seams never split one. The outer "
		"Write Selection can limit which vertices are written but does not redefine islands.");
}
#endif

FName UPCGDynMeshRandomValueByIslandPainterProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshPainterConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGDynMeshRandomValueByIslandPainterProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId();
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshRandomValueByIslandPainterProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	UPCGDynMeshRandomValueByIslandPainterFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshRandomValueByIslandPainterFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGDynMeshRandomValueByIslandPainterFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->Seed = Seed;   // inherited UPCGSettings::Seed
	Factory->MinValue = MinValue;
	Factory->MaxValue = MaxValue;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
