// Copyright Max Harris

#include "Elements/Selections/PCGDynMeshConstantSelectionFactory.h"

#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshConstantSelectionFactory"

namespace
{
	constexpr int32 PreconfiguredAlwaysPass = 0;
	constexpr int32 PreconfiguredAlwaysFail = 1;

	class FConstantSelectionOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FConstantSelectionOperation(bool bInAlwaysPass) : bAlwaysPass(bInAlwaysPass)
		{
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			return bAlwaysPass;
		}

	private:
		bool bAlwaysPass;
	};
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation> UPCGDynMeshConstantSelectionFactoryData::CreateNativeOperationInternal() const
{
	return MakeShared<FConstantSelectionOperation>(bAlwaysPass);
}

void UPCGDynMeshConstantSelectionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		bool bLocalAlwaysPass = bAlwaysPass;
		Ar << bLocalAlwaysPass;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshConstantSelectionFactoryProviderSettings::GetDefaultNodeTitle() const
{
	return bAlwaysPass
		? LOCTEXT("AlwaysPassTitle", "DynMesh | Selection | Constants | Always Pass")
		: LOCTEXT("AlwaysFailTitle", "DynMesh | Selection | Constants | Always Fail");
}

FText UPCGDynMeshConstantSelectionFactoryProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"A fixed DynMesh Selector that always selects every element, or none, in whatever domain it is "
		"evaluated. Connect to Build DynMesh Selection or a process node's Selector input.");
}

TArray<FPCGPreConfiguredSettingsInfo> UPCGDynMeshConstantSelectionFactoryProviderSettings::GetPreconfiguredInfo() const
{
	return {
		FPCGPreConfiguredSettingsInfo(PreconfiguredAlwaysPass,
			LOCTEXT("AlwaysPassTitle", "DynMesh | Selection | Constants | Always Pass")),
		FPCGPreConfiguredSettingsInfo(PreconfiguredAlwaysFail,
			LOCTEXT("AlwaysFailTitle", "DynMesh | Selection | Constants | Always Fail")),
	};
}

void UPCGDynMeshConstantSelectionFactoryProviderSettings::ApplyPreconfiguredSettings(
	const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	switch (PreconfiguredInfo.PreconfiguredIndex)
	{
	case PreconfiguredAlwaysPass:
		bAlwaysPass = true;
		break;
	case PreconfiguredAlwaysFail:
		bAlwaysPass = false;
		break;
	default:
		ensureMsgf(false, TEXT("Unknown DynMesh Selection Constant preconfiguration index: %d"),
			PreconfiguredInfo.PreconfiguredIndex);
		break;
	}
}
#endif

const FPCGDataTypeBaseId& UPCGDynMeshConstantSelectionFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId();
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshConstantSelectionFactoryProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	auto* Factory = InFactory ? Cast<UPCGDynMeshConstantSelectionFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGDynMeshConstantSelectionFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}
	Factory->Priority = Priority;
	Factory->bAlwaysPass = bAlwaysPass;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
