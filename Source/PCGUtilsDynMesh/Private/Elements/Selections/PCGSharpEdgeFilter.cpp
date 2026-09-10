#include "Elements/Selections/PCGSharpEdgeFilter.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"

#define LOCTEXT_NAMESPACE "PCGSharpEdgeFilter"

namespace
{
	class FSharpEdgeSelectionOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FSharpEdgeSelectionOperation(const UPCGSharpEdgeSelectionFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) || !Factory)
			{
				return false;
			}
			CosThreshold = FMath::Cos(FMath::DegreesToRadians(
				static_cast<double>(FMath::Clamp(Factory->MinimumSharpAngleDegrees, 0.0f, 180.0f))));
			return true;
		}

		virtual bool TestElement(int32 EdgeID) const override
		{
			const UE::Geometry::FIndex2i EdgeTriangles = SelectionContext->Mesh.GetEdgeT(EdgeID);
			return EdgeTriangles.B != INDEX_NONE &&
				SelectionContext->Mesh.GetTriNormal(EdgeTriangles.A).Dot(
					SelectionContext->Mesh.GetTriNormal(EdgeTriangles.B)) <= CosThreshold;
		}

	private:
		TObjectPtr<const UPCGSharpEdgeSelectionFactoryData> Factory;
		double CosThreshold = 1.0;
	};
}

#if WITH_EDITOR
FText UPCGSharpEdgeFilterSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select | Sharp Edges");
}

FText UPCGSharpEdgeFilterSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Filters a Dynamic Mesh edge selection (or all mesh edges) down to edges where adjacent triangle normals differ by at least Minimum Sharp Angle.");
}
#endif

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGSharpEdgeSelectionFactoryData::CreateNativeOperationInternal() const
{
	return MakeShared<FSharpEdgeSelectionOperation>(this);
}

void UPCGSharpEdgeSelectionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		float Angle = MinimumSharpAngleDegrees;
		Ar << Angle;
	}
}

UPCGUtilsDynMeshFactoryData* UPCGSharpEdgeFilterSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	UPCGSharpEdgeSelectionFactoryData* Factory = InFactory
		? Cast<UPCGSharpEdgeSelectionFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGSharpEdgeSelectionFactoryData>(InContext);
	if (!Factory) return nullptr;
	Factory->Priority = Priority;
	Factory->MinimumSharpAngleDegrees = MinimumSharpAngleDegrees;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
