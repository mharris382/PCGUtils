// Copyright Max Harris

#include "Elements/Creation/PrimitiveBuilder/PCGPrimitiveBuilderFactory.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Creation/CreatePrimitive/PCGCreatePrimitiveSettingsBase.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGPrimitiveBuilderFactory"

namespace
{
	void AddFittingToCrc(FArchiveCrc32& Ar, const FPCGUtilsFittingDetails& Fitting)
	{
		uint8 FitMode = static_cast<uint8>(Fitting.ScaleToFit.ScaleToFitMode);
		uint8 FitUniform = static_cast<uint8>(Fitting.ScaleToFit.ScaleToFit);
		uint8 FitX = static_cast<uint8>(Fitting.ScaleToFit.ScaleToFitX);
		uint8 FitY = static_cast<uint8>(Fitting.ScaleToFit.ScaleToFitY);
		uint8 FitZ = static_cast<uint8>(Fitting.ScaleToFit.ScaleToFitZ);
		Ar << FitMode << FitUniform << FitX << FitY << FitZ;

		auto AddAxis = [&Ar](const FPCGUtilsSingleJustifyDetails& Axis, bool bEnabled)
		{
			uint8 From = static_cast<uint8>(Axis.From);
			uint8 To = static_cast<uint8>(Axis.To);
			double FromValue = Axis.FromValue;
			double ToValue = Axis.ToValue;
			Ar << bEnabled << From << To << FromValue << ToValue;
		};
		AddAxis(Fitting.Justification.JustifyX, Fitting.Justification.bDoJustifyX);
		AddAxis(Fitting.Justification.JustifyY, Fitting.Justification.bDoJustifyY);
		AddAxis(Fitting.Justification.JustifyZ, Fitting.Justification.bDoJustifyZ);

		FVector Padding = Fitting.Padding;
		FVector LocalLocation = Fitting.LocalTransform.GetLocation();
		FQuat LocalRotation = Fitting.LocalTransform.GetRotation();
		FVector LocalScale = Fitting.LocalTransform.GetScale3D();
		Ar << Padding << LocalLocation << LocalRotation << LocalScale;
	}

	class FPrimitiveBuilderLeafOperation final : public FPCGUtilsDynMeshPrimitiveOperation
	{
	public:
		explicit FPrimitiveBuilderLeafOperation(const UPCGPrimitiveBuilderFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual UDynamicMesh* BuildMesh(const FTransform& SeedTransform, const FBox& SeedLocalBounds) const override
		{
			UDynamicMesh* LeafMesh = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
			if (!Factory || !Factory->Primitive)
			{
				return LeafMesh;
			}

			const UE::Geometry::FAxisAlignedBox3d NativeBounds = GetNativeBounds();
			const FBox CandidateBounds(FVector(NativeBounds.Min), FVector(NativeBounds.Max));

			FTransform PlacementTransform;
			Factory->Fitting.ComputeLocalTransform(SeedTransform, SeedLocalBounds, CandidateBounds, PlacementTransform);

			Factory->Primitive->AppendPrimitive(LeafMesh, PlacementTransform);
			return LeafMesh;
		}

	private:
		/** Generates the primitive once at identity to measure its native bounds; the config is static per node, so the reference mesh is cached across every seed this operation is evaluated for. */
		UE::Geometry::FAxisAlignedBox3d GetNativeBounds() const
		{
			if (!ReferenceMesh)
			{
				ReferenceMesh = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
				Factory->Primitive->AppendPrimitive(ReferenceMesh, FTransform::Identity);
			}
			if (const UE::Geometry::FDynamicMesh3* MeshPtr = ReferenceMesh->GetMeshPtr())
			{
				return MeshPtr->GetBounds();
			}
			return UE::Geometry::FAxisAlignedBox3d();
		}

		TObjectPtr<const UPCGPrimitiveBuilderFactoryData> Factory;
		mutable TObjectPtr<UDynamicMesh> ReferenceMesh;
	};
}

TSharedPtr<FPCGUtilsDynMeshPrimitiveOperation> UPCGPrimitiveBuilderFactoryData::CreateOperationInternal() const
{
	return MakeShared<FPrimitiveBuilderLeafOperation>(this);
}

void UPCGPrimitiveBuilderFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	uint32 PrimitiveClassHash = Primitive ? GetTypeHash(Primitive->GetClass()) : 0;
	Ar << PrimitiveClassHash;
	if (Primitive)
	{
		// Folds every reflected primitive parameter (Radius, Steps, Origin, ...) into the CRC so tweaking them
		// invalidates downstream caches even though the UObject's identity doesn't change.
		const_cast<UPCGCreatePrimitiveSettingsBase*>(Primitive.Get())->Serialize(Ar);
	}

	AddFittingToCrc(Ar, Fitting);
}

UPCGPrimitiveBuilderFactoryProviderSettings::UPCGPrimitiveBuilderFactoryProviderSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Primitive = CreateDefaultSubobject<UPCGCreatePrimitiveBoxSettings>(TEXT("DefaultPrimitive"));
}

#if WITH_EDITOR
FText UPCGPrimitiveBuilderFactoryProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Primitive Builder");
}

FText UPCGPrimitiveBuilderFactoryProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Builds a reusable Primitive Builder: one Geometry Script primitive plus how it fits, aligns, and "
		"offsets into each seed's bounds. Feed the output into Create Primitive's Builder pin.");
}
#endif

FName UPCGPrimitiveBuilderFactoryProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshPrimitiveFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGPrimitiveBuilderFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshPrimitiveFactoryDataTypeInfo::AsId();
}

UPCGUtilsDynMeshFactoryData* UPCGPrimitiveBuilderFactoryProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	if (!Primitive)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("NoPrimitiveConfigured", "Primitive Builder has no primitive type configured."), InContext);
		return nullptr;
	}

	UPCGPrimitiveBuilderFactoryData* Factory = InFactory
		? Cast<UPCGPrimitiveBuilderFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGPrimitiveBuilderFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Primitive = Primitive;
	Factory->Fitting = Fitting;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
