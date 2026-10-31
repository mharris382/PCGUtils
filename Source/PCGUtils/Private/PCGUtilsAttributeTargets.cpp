// Copyright Max Harris

#include "PCGUtilsAttributeTargets.h"

#include "Metadata/PCGAttributePropertySelector.h"

#define LOCTEXT_NAMESPACE "PCGUtilsAttributeTargets"

namespace PCGUtilsAttributeTargets
{
	bool Resolve(UPCGData& Data, FName SettingValue, const FString& SettingLabel, FResolvedTarget& OutTarget, FText& OutError)
	{
		const FString Text = SettingValue.ToString();
		FPCGAttributePropertySelector Selector;
		Selector.Update(Text);

		if (Selector.GetSelection() != EPCGAttributePropertySelection::Attribute)
		{
			OutError = FText::Format(LOCTEXT("NotAttribute",
				"{0} is '{1}', which names a point property; expected an attribute name such as 'Mesh' or '@Data.Mesh'."),
				FText::FromString(SettingLabel), FText::FromString(Text));
			return false;
		}
		if (!Selector.GetExtraNames().IsEmpty())
		{
			OutError = FText::Format(LOCTEXT("ExtraNames",
				"{0} is '{1}', which has an accessor after the name ('.{2}'); expected a plain attribute name, optionally prefixed with '@Data.'."),
				FText::FromString(SettingLabel), FText::FromString(Text), FText::FromString(FString::Join(Selector.GetExtraNames(), TEXT("."))));
			return false;
		}
		if (Selector.GetAttributeName().IsNone())
		{
			OutError = FText::Format(LOCTEXT("NoName", "{0} is '{1}', which has no attribute name."),
				FText::FromString(SettingLabel), FText::FromString(Text));
			return false;
		}

		UPCGMetadata* Metadata = Data.MutableMetadata();
		const FPCGMetadataDomainID DomainID = Data.GetMetadataDomainIDFromSelector(Selector);
		FPCGMetadataDomain* Domain = Metadata && DomainID.IsValid() ? Metadata->GetMetadataDomain(DomainID) : nullptr;
		if (!Domain)
		{
			OutError = FText::Format(LOCTEXT("NoDomain",
				"{0} is '{1}', but {2} has no metadata domain '{3}'; use a plain name for each point or '@Data.' for the data set."),
				FText::FromString(SettingLabel), FText::FromString(Text), FText::FromString(Data.GetClass()->GetName()),
				FText::FromName(Selector.GetDomainName()));
			return false;
		}

		OutTarget.Domain = Domain;
		OutTarget.DomainID = DomainID;
		OutTarget.AttributeName = Selector.GetAttributeName();
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
