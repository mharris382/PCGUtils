// Copyright Max Harris
// Adapted from PCGExtendedToolkit (FPCGExInputShorthandCustomization),
// Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Customizations/PCGUtilsInputShorthandCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Details/PCGUtilsPointShorthands.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "PCGUtilsEditorStyle.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "PCGUtilsInputShorthandCustomization"

namespace
{
	bool IsAttributeMode(const TSharedPtr<IPropertyHandle>& InputHandle)
	{
		uint8 Value = 0;
		return InputHandle.IsValid() && InputHandle->GetValue(Value) == FPropertyAccess::Success
			&& Value == static_cast<uint8>(EPCGUtilsInputValueType::Attribute);
	}

	/** Scalars use their own value widget; vector-like structs lay their components out in one row. */
	TSharedRef<SWidget> MakeConstantWidget(const TSharedPtr<IPropertyHandle>& ConstantHandle)
	{
		if (!ConstantHandle.IsValid())
		{
			return SNullWidget::NullWidget;
		}
		if (!CastField<FStructProperty>(ConstantHandle->GetProperty()))
		{
			return ConstantHandle->CreatePropertyValueWidget();
		}

		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		uint32 NumChildren = 0;
		ConstantHandle->GetNumChildren(NumChildren);
		for (uint32 Index = 0; Index < NumChildren; ++Index)
		{
			const TSharedPtr<IPropertyHandle> Child = ConstantHandle->GetChildHandle(Index);
			if (!Child.IsValid())
			{
				continue;
			}
			Row->AddSlot().FillWidth(1).Padding(0, 0, 2, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 2, 0)
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(Child->GetPropertyDisplayName())
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SHorizontalBox::Slot().FillWidth(1)
				[
					Child->CreatePropertyValueWidget()
				]
			];
		}
		return Row;
	}

	/** The mode enum of a shorthand: EPCGUtilsInputValueType (@Data) or EPCGUtilsPointInputValueType (per point). */
	const UEnum* ModeEnum(const TSharedPtr<IPropertyHandle>& InputHandle)
	{
		const FProperty* Property = InputHandle.IsValid() ? InputHandle->GetProperty() : nullptr;
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			return EnumProperty->GetEnum();
		}
		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			return ByteProperty->Enum;
		}
		return StaticEnum<EPCGUtilsInputValueType>();
	}

	/** Free-text selector field: '@Data.PathHeight', 'PathHeight', 'Size.Z'. */
	TSharedRef<SWidget> MakeAttributeWidget(const TSharedPtr<IPropertyHandle>& AttributeHandle, bool bPerPoint)
	{
		return SNew(SBox)
			.VAlign(VAlign_Center)
			.MaxDesiredHeight(22.0f)
			[
				SNew(SEditableTextBox)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ToolTipText(bPerPoint
					? LOCTEXT("PointAttributeTooltip", "Attribute to read: a point attribute (Amount) or property ($Density) gives each point its own value; @Data.Amount gives one value for the input. Extractors such as .Z are allowed.")
					: LOCTEXT("AttributeTooltip", "@Data attribute to read, e.g. PathHeight or @Data.PathHeight. Extractors such as .Z are allowed."))
				.Text_Lambda([AttributeHandle]()
				{
					TArray<void*> RawData;
					AttributeHandle->AccessRawData(RawData);
					const auto* Selector = RawData.Num() > 0 ? static_cast<const FPCGAttributePropertyInputSelector*>(RawData[0]) : nullptr;
					return Selector ? FText::FromString(Selector->ToString()) : FText::GetEmpty();
				})
				.OnTextCommitted_Lambda([AttributeHandle](const FText& NewText, ETextCommit::Type CommitType)
				{
					if (CommitType != ETextCommit::OnEnter && CommitType != ETextCommit::OnUserMovedFocus)
					{
						return;
					}
					AttributeHandle->NotifyPreChange();
					TArray<void*> RawData;
					AttributeHandle->AccessRawData(RawData);
					for (void* Ptr : RawData)
					{
						if (auto* Selector = static_cast<FPCGAttributePropertyInputSelector*>(Ptr))
						{
							Selector->Update(NewText.ToString());
						}
					}
					AttributeHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
				})
			];
	}

	TSharedRef<SWidget> MakeModeToggle(const TSharedPtr<IPropertyHandle>& InputHandle)
	{
		const ISlateStyle& Style = FPCGUtilsEditorStyle::Get();
		const UEnum* Enum = ModeEnum(InputHandle);
		return SNew(SButton)
			.ButtonStyle(&Style, "PCGUtils.ActionIcon")
			.ContentPadding(FMargin(2, 0))
			.ToolTipText_Lambda([InputHandle, Enum]()
			{
				// Both mode enums use 0 for Constant and 1 for the attribute mode.
				const int64 Current = IsAttributeMode(InputHandle) ? 1 : 0;
				return FText::Format(LOCTEXT("ModeTooltip", "{0}: {1}\nClick to switch."),
					Enum->GetDisplayNameTextByValue(Current),
					Enum->GetToolTipTextByIndex(Enum->GetIndexByValue(Current)));
			})
			.OnClicked_Lambda([InputHandle]()
			{
				const uint8 Next = IsAttributeMode(InputHandle)
					? static_cast<uint8>(EPCGUtilsInputValueType::Constant)
					: static_cast<uint8>(EPCGUtilsInputValueType::Attribute);
				InputHandle->SetValue(Next);
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.DesiredSizeOverride(FVector2D(16.0f, 16.0f))
				.Image_Lambda([InputHandle, &Style]()
				{
					return Style.GetBrush(IsAttributeMode(InputHandle)
						? "PCGUtils.ActionIcon.DataAttribute" : "PCGUtils.ActionIcon.Constant");
				})
			];
	}
}

TSharedRef<IPropertyTypeCustomization> FPCGUtilsInputShorthandCustomization::MakeInstance()
{
	return MakeShared<FPCGUtilsInputShorthandCustomization>();
}

TArray<FName> FPCGUtilsInputShorthandCustomization::RegisterAll(FPropertyEditorModule& PropertyEditor)
{
	TArray<FName> Registered;
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		if (*It != FPCGUtilsInputShorthandBase::StaticStruct() && *It != FPCGUtilsPointShorthandBase::StaticStruct()
			&& PCGUtilsInputShorthands::IsShorthandStruct(*It))
		{
			PropertyEditor.RegisterCustomPropertyTypeLayout(It->GetFName(),
				FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGUtilsInputShorthandCustomization::MakeInstance));
			Registered.Add(It->GetFName());
		}
	}
	return Registered;
}

void FPCGUtilsInputShorthandCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const TSharedPtr<IPropertyHandle> InputHandle = PropertyHandle->GetChildHandle(TEXT("Input"));
	const TSharedPtr<IPropertyHandle> ConstantHandle = PropertyHandle->GetChildHandle(TEXT("Constant"));
	const TSharedPtr<IPropertyHandle> AttributeHandle = PropertyHandle->GetChildHandle(TEXT("Attribute"));
	if (!InputHandle.IsValid() || !ConstantHandle.IsValid() || !AttributeHandle.IsValid())
	{
		return;
	}

	// The engine strips a leading "b" only from bool properties; a Boolean shorthand is a struct, so do it here.
	FText DisplayName;
	const FProperty* Property = PropertyHandle->GetProperty();
	if (Property && !Property->HasMetaData(TEXT("DisplayName")) && CastField<FBoolProperty>(ConstantHandle->GetProperty()))
	{
		DisplayName = FText::FromString(FName::NameToDisplayString(Property->GetName(), /*bIsBool=*/true));
	}

	const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
	const bool bPerPoint = StructProperty && PCGUtilsPointShorthands::IsPointShorthandStruct(StructProperty->Struct);

	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget(DisplayName)
	]
	.ValueContent()
	.MinDesiredWidth(250)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1).Padding(1)
		[
			SNew(SBox)
			.Visibility_Lambda([InputHandle]() { return IsAttributeMode(InputHandle) ? EVisibility::Collapsed : EVisibility::Visible; })
			[
				MakeConstantWidget(ConstantHandle)
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1).Padding(1)
		[
			SNew(SBox)
			.Visibility_Lambda([InputHandle]() { return IsAttributeMode(InputHandle) ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				MakeAttributeWidget(AttributeHandle, bPerPoint)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 0, 0)
		[
			MakeModeToggle(InputHandle)
		]
	];
}

#undef LOCTEXT_NAMESPACE
