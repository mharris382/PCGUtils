// Copyright Max Harris

#include "Elements/Creation/PrimitiveBuilder/PCGPrimitiveBuilderFactory.h"

#include "Data/PCGDynamicMeshData.h"
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

		FVector PaddingMin = Fitting.PaddingMin;
		FVector PaddingMax = Fitting.PaddingMax;
		FVector LocalLocation = Fitting.LocalTransform.GetLocation();
		FQuat LocalRotation = Fitting.LocalTransform.GetRotation();
		FVector LocalScale = Fitting.LocalTransform.GetScale3D();
		Ar << PaddingMin << PaddingMax << LocalLocation << LocalRotation << LocalScale;
	}

	class FPrimitiveBuilderLeafOperation final : public FPCGUtilsDynMeshBuilderOperation
	{
	public:
		explicit FPrimitiveBuilderLeafOperation(const UPCGPrimitiveBuilderFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Build(
			const FPCGUtilsDynMeshBuildContext& BuildContext, FPCGUtilsDynMeshBuildResult& OutResult) const override
		{
			FPCGContext* EvaluationContext = BuildContext.Context ? BuildContext.Context : Context;
			UDynamicMesh* LeafMesh = FPCGContext::NewObject_AnyThread<UDynamicMesh>(EvaluationContext);
			if (!Factory || !Factory->Primitive)
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("LeafNoPrimitive", "Primitive Builder has no primitive type configured."), EvaluationContext);
				return false;
			}

			const UE::Geometry::FAxisAlignedBox3d NativeBounds = GetNativeBounds(EvaluationContext);
			const FBox CandidateBounds(FVector(NativeBounds.Min), FVector(NativeBounds.Max));

			FTransform PlacementTransform;
			Factory->Fitting.ComputeLocalTransform(
				BuildContext.SeedTransform, BuildContext.SeedLocalBounds, CandidateBounds, PlacementTransform);

			Factory->Primitive->AppendPrimitive(LeafMesh, PlacementTransform);

			// The leaf owns this mesh outright; every decorator above is free to mutate it in place.
			UPCGDynamicMeshData* MeshData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(EvaluationContext);
			MeshData->Initialize(LeafMesh, /*bCanTakeOwnership=*/true, TArray<UMaterialInterface*>{});
			OutResult.SetMeshData(MeshData);

			// Record where this primitive actually landed, so decorators above can transform in the shape's
			// own space rather than the actor's.
			OutResult.SetBuilderFrame(PlacementTransform);
			return true;
		}

	private:
		/** Generates the primitive once at identity to measure its native bounds; the config is static per node, so the reference mesh is cached across every seed this operation is evaluated for. */
		UE::Geometry::FAxisAlignedBox3d GetNativeBounds(FPCGContext* EvaluationContext) const
		{
			if (!ReferenceMesh)
			{
				ReferenceMesh = FPCGContext::NewObject_AnyThread<UDynamicMesh>(EvaluationContext);
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

TSharedPtr<FPCGUtilsDynMeshBuilderOperation> UPCGPrimitiveBuilderFactoryData::CreateOperationInternal() const
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

FName UPCGPrimitiveBuilderProviderSettingsBase::GetMainOutputPin() const
{
	return PCGUtilsDynMeshBuilderFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGPrimitiveBuilderProviderSettingsBase::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshBuilderFactoryDataTypeInfo::AsId();
}

UPCGUtilsDynMeshFactoryData* UPCGPrimitiveBuilderProviderSettingsBase::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	// Built from this node's own properties, so any PCG override applied to them is already reflected here.
	UPCGCreatePrimitiveSettingsBase* PrimitiveSettings = CreatePrimitiveSettings(InContext);
	if (!PrimitiveSettings)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("NoPrimitiveSettings", "This Builder could not construct its primitive options."), InContext);
		return nullptr;
	}

	UPCGPrimitiveBuilderFactoryData* Factory = InFactory
		? Cast<UPCGPrimitiveBuilderFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGPrimitiveBuilderFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Primitive = PrimitiveSettings;
	Factory->Fitting = Fitting;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
