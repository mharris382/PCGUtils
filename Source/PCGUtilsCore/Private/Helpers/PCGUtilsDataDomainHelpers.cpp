// Copyright Max Harris

#include "Helpers/PCGUtilsDataDomainHelpers.h"

#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadata.h"
#include "PCGData.h"

namespace PCGUtilsDataDomain
{
	FPCGMetadataDomain* GetMutableDataDomain(UPCGData* Data)
	{
		UPCGMetadata* Metadata = Data ? Data->MutableMetadata() : nullptr;
		return Metadata ? Metadata->GetMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
	}

	void CopyDataAttributes(const UPCGData* Source, FPCGMetadataDomain& OutDomain, TConstArrayView<FName> Excluded)
	{
		const UPCGMetadata* SourceMetadata = Source ? Source->ConstMetadata() : nullptr;
		const FPCGMetadataDomain* SourceDomain = SourceMetadata ? SourceMetadata->GetConstMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		if (!SourceDomain || SourceDomain == &OutDomain)
		{
			return;
		}
		TArray<FName> Names;
		TArray<EPCGMetadataTypes> Types;
		SourceDomain->GetAttributes(Names, Types);
		const PCGMetadataEntryKey SourceKey = SourceDomain->GetItemCountForChild() > 0 ? PCGFirstEntryKey : PCGInvalidEntryKey;
		for (const FName& Name : Names)
		{
			if (Excluded.Contains(Name))
			{
				continue;
			}
			const FPCGMetadataAttributeBase* SourceAttribute = SourceDomain->GetConstAttribute(Name);
			if (!SourceAttribute)
			{
				continue;
			}
			PCGMetadataAttribute::CallbackWithRightType(SourceAttribute->GetTypeId(), [&](auto Dummy)
			{
				using T = decltype(Dummy);
				const FPCGMetadataAttribute<T>* Typed = static_cast<const FPCGMetadataAttribute<T>*>(SourceAttribute);
				WriteValue<T>(OutDomain, Name, Typed->GetValueFromItemKey(SourceKey));
			});
		}
	}

	int32 CopyElementAttributes(const UPCGData* Source, FName DomainName, int32 ElementIndex, FPCGMetadataDomain& OutDomain)
	{
		const UPCGMetadata* SourceMetadata = Source ? Source->ConstMetadata() : nullptr;
		if (!SourceMetadata)
		{
			return 0;
		}
		FPCGAttributePropertyInputSelector DomainSelector;
		DomainSelector.SetDomainName(DomainName);
		const FPCGMetadataDomain* SourceDomain = SourceMetadata->GetConstMetadataDomain(Source->GetMetadataDomainIDFromSelector(DomainSelector));
		if (!SourceDomain || SourceDomain == &OutDomain)
		{
			return 0;
		}

		TArray<FName> Names;
		TArray<EPCGMetadataTypes> Types;
		SourceDomain->GetAttributes(Names, Types);
		int32 NumCopied = 0;
		for (const FName& Name : Names)
		{
			// Through accessors, so the element index means the same for points (their metadata entries) and for
			// attribute set entries.
			const FPCGAttributePropertyInputSelector Selector =
				FPCGAttributePropertySelector::CreateAttributeSelector<FPCGAttributePropertyInputSelector>(Name, DomainName);
			const TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Source, Selector, /*bQuiet=*/true);
			const TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(Source, Selector);
			if (!Accessor || !Keys || ElementIndex < 0 || ElementIndex >= Keys->GetNum())
			{
				continue;
			}
			PCGMetadataAttribute::CallbackWithRightType(Accessor->GetUnderlyingType(), [&](auto Dummy)
			{
				using T = decltype(Dummy);
				T Value{};
				if (Accessor->Get<T>(Value, ElementIndex, *Keys))
				{
					WriteValue<T>(OutDomain, Name, Value);
					++NumCopied;
				}
			});
		}
		return NumCopied;
	}
}
