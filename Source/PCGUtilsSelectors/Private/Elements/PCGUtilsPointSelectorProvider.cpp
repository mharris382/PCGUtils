// Copyright Max Harris

#include "Elements/PCGUtilsPointSelectorProvider.h"

#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "Factories/PCGUtilsGeometryCollectionSelectionFactory.h"
#include "PCGContext.h"
#include "PCGUtilsSettingsCategories.h"
#include "Selectors/PCGUtilsPointSelector.h"
#include "Selectors/PCGUtilsPointSelectorWrappers.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsPointSelectorProvider"

namespace PCGUtilsPointSelectorProviderPrivate
{
	/** The domain as written in titles and palette entries. */
	FText DomainText(EPCGUtilsSelectorDomain Domain)
	{
		switch (Domain)
		{
		case EPCGUtilsSelectorDomain::DynMesh: return LOCTEXT("DomainDynMesh", "DynMesh");
		case EPCGUtilsSelectorDomain::GC: return LOCTEXT("DomainGC", "GC");
		default: return LOCTEXT("DomainVoxel", "Voxel");
		}
	}

	/** False, with the exact failed invariant logged, when a match fraction in use is outside 0 to 1. */
	bool CheckMatchFraction(FPCGContext* Context, const FText& NodeTitle, const FText& ConversionName, EPCGUtilsElementPointMatch Match, double Fraction)
	{
		if (Match != EPCGUtilsElementPointMatch::AtLeastFraction || (Fraction >= 0.0 && Fraction <= 1.0))
		{
			return true;
		}
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("MatchFraction", "{0}: {1} > Match Fraction is {2}; expected a value from 0 to 1."),
			NodeTitle, ConversionName, FText::AsNumber(Fraction)), Context);
		return false;
	}
}

#if WITH_EDITOR
EPCGSettingsType UPCGUtilsPointSelectorProviderSettings::GetType() const
{
	return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::Selectors);
}

FLinearColor UPCGUtilsPointSelectorProviderSettings::GetNodeTitleColor() const
{
	// A neutral light slate: these nodes belong to no one domain.
	return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("8A94A6FF")));
}

TArray<FPCGPreConfiguredSettingsInfo> UPCGUtilsPointSelectorProviderSettings::GetPreconfiguredInfo() const
{
	using namespace PCGUtilsPointSelectorProviderPrivate;

	// One palette entry per domain. The index is the Domain value, which is saved in graphs: append, never reorder.
	TArray<FPCGPreConfiguredSettingsInfo> Infos;
	for (const EPCGUtilsSelectorDomain Each : { EPCGUtilsSelectorDomain::Voxel, EPCGUtilsSelectorDomain::DynMesh, EPCGUtilsSelectorDomain::GC })
	{
		Infos.Emplace(
			static_cast<int32>(Each),
			FText::Format(LOCTEXT("DomainEntry", "{0} ({1})"), GetDefaultNodeTitle(), DomainText(Each)),
			FText::Format(LOCTEXT("DomainEntryTooltip", "{0}\n\nPlaced with Domain set to {1}; Domain can be changed on the node."), GetNodeTooltipText(), DomainText(Each)));
	}
	return Infos;
}

void UPCGUtilsPointSelectorProviderSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	const UEnum* DomainEnum = StaticEnum<EPCGUtilsSelectorDomain>();
	if (DomainEnum && DomainEnum->IsValidEnumValue(PreconfiguredInfo.PreconfiguredIndex))
	{
		Domain = static_cast<EPCGUtilsSelectorDomain>(PreconfiguredInfo.PreconfiguredIndex);
	}
}

EPCGChangeType UPCGUtilsPointSelectorProviderSettings::GetChangeTypeForProperty(const FName& InPropertyName) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(InPropertyName);
	if (InPropertyName == GET_MEMBER_NAME_CHECKED(UPCGUtilsPointSelectorProviderSettings, Domain))
	{
		// The output pin's name and type follow Domain.
		ChangeType |= EPCGChangeType::Structural;
	}
	return ChangeType;
}
#endif

FString UPCGUtilsPointSelectorProviderSettings::GetAdditionalTitleInformation() const
{
	return PCGUtilsPointSelectorProviderPrivate::DomainText(Domain).ToString();
}

FName UPCGUtilsPointSelectorProviderSettings::GetMainOutputPin() const
{
	switch (Domain)
	{
	case EPCGUtilsSelectorDomain::DynMesh: return PCGUtilsDynMeshSelectionFactoryConstants::OutputPin;
	case EPCGUtilsSelectorDomain::GC: return PCGUtilsGeometryCollectionSelectionFactoryConstants::OutputPin;
	default: return PCGVoxelSelectorPins::SelectorLabel;
	}
}

const FPCGDataTypeBaseId& UPCGUtilsPointSelectorProviderSettings::GetFactoryTypeId() const
{
	switch (Domain)
	{
	case EPCGUtilsSelectorDomain::DynMesh: return FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId();
	case EPCGUtilsSelectorDomain::GC: return FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId();
	default: return FPCGVoxelSelectorDataTypeInfo::AsId();
	}
}

UPCGUtilsFactoryData* UPCGUtilsPointSelectorProviderSettings::CreateFactoryBase(FPCGContext* InContext) const
{
	using namespace PCGUtilsPointSelectorProviderPrivate;
	check(InContext);

#if WITH_EDITOR
	const FText NodeTitle = GetDefaultNodeTitle();
#else
	const FText NodeTitle = FText::FromString(GetClass()->GetName());
#endif

	// Checked here, on the node that owns the setting, before anything downstream evaluates the selector.
	if (Domain == EPCGUtilsSelectorDomain::DynMesh
		&& !CheckMatchFraction(InContext, NodeTitle, LOCTEXT("DynMeshConversionName", "DynMesh Conversion"), DynMeshConversion.Match, DynMeshConversion.MatchFraction))
	{
		return nullptr;
	}
	if (Domain == EPCGUtilsSelectorDomain::GC
		&& !CheckMatchFraction(InContext, NodeTitle, LOCTEXT("GCConversionName", "GC Conversion"), GCConversion.Match, GCConversion.MatchFraction))
	{
		return nullptr;
	}

	UPCGUtilsPointSelectorData* PointSelector = CreatePointSelector(InContext);
	if (!PointSelector)
	{
		return nullptr;
	}
	PointSelector->Priority = Priority;
	if (!PointSelector->Prepare(InContext))
	{
		return nullptr;
	}

	// The domain's own selector, holding the point selector. Invert goes into the domain's own invert flag so the
	// domain's machinery applies it once; Priority is the domain's own Priority.
	switch (Domain)
	{
	case EPCGUtilsSelectorDomain::DynMesh:
		{
			UPCGUtilsDynMeshPointSelectorData* Wrapper = FPCGContext::NewObject_AnyThread<UPCGUtilsDynMeshPointSelectorData>(InContext);
			Wrapper->PointSelector = PointSelector;
			Wrapper->Conversion = DynMeshConversion;
			Wrapper->bInvertSelection = bInvert;
			Wrapper->Priority = Priority;
			return Wrapper;
		}
	case EPCGUtilsSelectorDomain::GC:
		{
			UPCGUtilsGeometryCollectionPointSelectorData* Wrapper = FPCGContext::NewObject_AnyThread<UPCGUtilsGeometryCollectionPointSelectorData>(InContext);
			Wrapper->PointSelector = PointSelector;
			Wrapper->Conversion = GCConversion;
			Wrapper->bInvertSelection = bInvert;
			Wrapper->Priority = Priority;
			return Wrapper;
		}
	default:
		{
			UPCGVoxelPointSelectorData* Wrapper = FPCGContext::NewObject_AnyThread<UPCGVoxelPointSelectorData>(InContext);
			Wrapper->PointSelector = PointSelector;
			Wrapper->Conversion = VoxelConversion;
			Wrapper->bInvert = bInvert;
			Wrapper->Priority = Priority;
			return Wrapper;
		}
	}
}

#undef LOCTEXT_NAMESPACE
