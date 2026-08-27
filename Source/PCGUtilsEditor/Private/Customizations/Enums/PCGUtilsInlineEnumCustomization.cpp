// Copyright Max Harris
// Inline enum radio-group customization adapted from PCGExtendedToolkit's PCGExInlineEnumCustomization,
// Copyright 2026 Timothe Lapetite and contributors (MIT). Renamed and trimmed to the radio-group path
// used by the PCGUtils fitting/alignment structures.

#include "Customizations/Enums/PCGUtilsInlineEnumCustomization.h"

#include "PCGUtilsEditorStyle.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "Styling/ISlateStyle.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Class.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"

namespace PCGUtilsInlineEnum
{
	/**
	 * Build skip indices from the property's engine-standard ValidEnumValues / InvalidEnumValues
	 * metadata, so per-property gating works through the inline customization the same way it
	 * does through the default enum combobox.
	 */
	static TSet<int32> GetMetaSkipIndices(const TSharedPtr<IPropertyHandle>& PropertyHandle, const UEnum* Enum)
	{
		TSet<int32> SkipIndices;
		if (!PropertyHandle || !Enum)
		{
			return SkipIndices;
		}

		auto ParseIndices = [Enum](const FString& MetaString, TSet<int32>& OutIndices)
		{
			TArray<FString> Names;
			MetaString.ParseIntoArray(Names, TEXT(","), true);
			for (FString& Name : Names)
			{
				Name.TrimStartAndEndInline();
				const int32 Idx = Enum->GetIndexByNameString(Name);
				if (Idx != INDEX_NONE)
				{
					OutIndices.Add(Idx);
				}
			}
		};

		const FString& InvalidMeta = PropertyHandle->GetMetaData(TEXT("InvalidEnumValues"));
		if (!InvalidMeta.IsEmpty())
		{
			ParseIndices(InvalidMeta, SkipIndices);
		}

		const FString& ValidMeta = PropertyHandle->GetMetaData(TEXT("ValidEnumValues"));
		if (!ValidMeta.IsEmpty())
		{
			TSet<int32> ValidIndices;
			ParseIndices(ValidMeta, ValidIndices);
			if (!ValidIndices.IsEmpty())
			{
				for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
				{
					if (!ValidIndices.Contains(i))
					{
						SkipIndices.Add(i);
					}
				}
			}
		}

		return SkipIndices;
	}

	TArray<int32> GetEnumDisplayOrder(const UEnum* Enum)
	{
		const int32 NumValues = Enum ? Enum->NumEnums() - 1 : 0;

		TArray<int32> Order;
		if (NumValues <= 0)
		{
			return Order;
		}

		Order.Reserve(NumValues);

		const FString OrderMeta = Enum->GetMetaData(TEXT("PCGUtilsDisplayOrder"));
		if (!OrderMeta.IsEmpty())
		{
			TArray<FString> Names;
			OrderMeta.ParseIntoArray(Names, TEXT(","), true);
			for (FString& Name : Names)
			{
				Name.TrimStartAndEndInline();
				const int32 Index = Enum->GetIndexByNameString(Name);
				// NumValues excludes the always-appended _MAX, which is never a presentable entry.
				if (Index != INDEX_NONE && Index < NumValues)
				{
					Order.AddUnique(Index);
				}
			}
		}

		// Declaration order for whatever the meta left out, so a partial list promotes its own
		// entries to the front and can never hide the rest.
		for (int32 i = 0; i < NumValues; ++i)
		{
			Order.AddUnique(i);
		}

		return Order;
	}

	TSharedRef<SWidget> CreateRadioGroup(const TSharedPtr<IPropertyHandle>& PropertyHandle, UEnum* Enum, const TSet<int32>& SkipIndices)
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

		if (!Enum)
		{
			return Box;
		}

		const ISlateStyle& Style = FPCGUtilsEditorStyle::Get();

		for (const int32 i : GetEnumDisplayOrder(Enum))
		{
			if (Enum->HasMetaData(TEXT("Hidden"), i) || SkipIndices.Contains(i))
			{
				continue;
			}
			const FString KeyName = Enum->GetNameStringByIndex(i);

			FString IconName = Enum->GetMetaData(TEXT("ActionIcon"), i);
			if (IconName.IsEmpty())
			{
				Box->AddSlot().AutoWidth().Padding(2, 2)
				[
					SNew(SButton)
					.Text(Enum->GetDisplayNameTextByIndex(i))
					.ToolTipText(Enum->GetToolTipTextByIndex(i))
					.ButtonColorAndOpacity_Lambda(
						[PropertyHandle, KeyName]
						{
							FString CurrentValue;
							PropertyHandle->GetValueAsFormattedString(CurrentValue);
							return CurrentValue == KeyName ? FLinearColor(0.005f, 0.005f, 0.005f, 0.8f) : FLinearColor::Transparent;
						})
					.OnClicked_Lambda(
						[PropertyHandle, KeyName]()
						{
							PropertyHandle->SetValueFromFormattedString(KeyName);
							return FReply::Handled();
						})
				];
			}
			else
			{
				IconName = TEXT("PCGUtils.ActionIcon.") + IconName;
				Box->AddSlot().AutoWidth().Padding(2, 2)
				[
					SNew(SButton)
					.ToolTipText(Enum->GetToolTipTextByIndex(i))
					.ButtonStyle(&Style, "PCGUtils.ActionIcon")
					.ButtonColorAndOpacity_Lambda(
						[PropertyHandle, KeyName]
						{
							FString CurrentValue;
							PropertyHandle->GetValueAsFormattedString(CurrentValue);
							return CurrentValue == KeyName ? FLinearColor(0.005f, 0.005f, 0.005f, 0.8f) : FLinearColor::Transparent;
						})
					.OnClicked_Lambda(
						[PropertyHandle, KeyName]()
						{
							PropertyHandle->SetValueFromFormattedString(KeyName);
							return FReply::Handled();
						})
					[
						SNew(SImage)
						.Image(Style.GetBrush(*IconName))
						.ColorAndOpacity_Lambda(
							[PropertyHandle, Enum, i]
							{
								FString CurrentValue;
								PropertyHandle->GetValueAsFormattedString(CurrentValue);
								const FString KeyName = Enum->GetNameStringByIndex(i);
								return (CurrentValue == KeyName)
									? FLinearColor::White
									: FLinearColor::Gray;
							})
					]
				];
			}
		}

		return Box;
	}

	TSharedRef<SWidget> CreateRadioGroup(const TSharedPtr<IPropertyHandle>& PropertyHandle, UEnum* Enum)
	{
		return CreateRadioGroup(PropertyHandle, Enum, GetMetaSkipIndices(PropertyHandle, Enum));
	}
}

FPCGUtilsInlineEnumCustomization::FPCGUtilsInlineEnumCustomization(const FString& InEnumName)
	: EnumName(InEnumName)
{
}

void FPCGUtilsInlineEnumCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	UEnum* Enum = FindFirstObjectSafe<UEnum>(*EnumName);
	if (!Enum)
	{
		return;
	}

	HeaderRow.NameContent()[PropertyHandle->CreatePropertyNameWidget()]
		.ValueContent()
		.MaxDesiredWidth(400)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SUniformGridPanel)
				+ SUniformGridPanel::Slot(0, 0)
				[
					PCGUtilsInlineEnum::CreateRadioGroup(PropertyHandle, Enum)
				]
			]
		];
}

void FPCGUtilsInlineEnumCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}
