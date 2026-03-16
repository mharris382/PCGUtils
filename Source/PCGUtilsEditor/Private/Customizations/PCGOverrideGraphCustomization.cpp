#include "Customizations/PCGOverrideGraphCustomization.h"

#include "OverrideGraphs.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PCGOverrideGraphCustomization"

TSharedRef<IPropertyTypeCustomization> FPCGOverrideGraphCustomization::MakeInstance()
{
    return MakeShared<FPCGOverrideGraphCustomization>();
}

void FPCGOverrideGraphCustomization::CustomizeHeader(
    TSharedRef<IPropertyHandle> StructPropertyHandle,
    FDetailWidgetRow& HeaderRow,
    IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
    // Get the two child property handles.
    TSharedPtr<IPropertyHandle> UseGraphHandle =
        StructPropertyHandle->GetChildHandle(
            GET_MEMBER_NAME_CHECKED(FPCGOverrideGraph, bUseGraph));
    TSharedPtr<IPropertyHandle> GraphHandle =
        StructPropertyHandle->GetChildHandle(
            GET_MEMBER_NAME_CHECKED(FPCGOverrideGraph, Graph));

    // The parent property name becomes the label (e.g. "PostSpawnGraph").
    // This replaces the default "Graph" child label.
    const FText ParentDisplayName = StructPropertyHandle->GetPropertyDisplayName();

    // Forward the parent tooltip to the graph picker row so hovering the
    // label shows any Tooltip meta set on the owning property.
    const FText ParentTooltip = StructPropertyHandle->GetToolTipText();

    HeaderRow
        .NameContent()
        [
            SNew(SHorizontalBox)

            // Toggle checkbox — inline edit condition toggle.
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.f, 0.f, 4.f, 0.f)
            [
                UseGraphHandle->CreatePropertyValueWidget()
            ]

            // Parent property name as label (e.g. "PostSpawnGraph").
            + SHorizontalBox::Slot()
            .VAlign(VAlign_Center)
            .FillWidth(1.f)
            [
                SNew(STextBlock)
                .Text(ParentDisplayName)
                .ToolTipText(ParentTooltip)
                .Font(IPropertyTypeCustomizationUtils::GetRegularFont())
            ]
        ]
        .ValueContent()
        .MinDesiredWidth(250.f)
        [
            GraphHandle->CreatePropertyValueWidget()
        ];
}

void FPCGOverrideGraphCustomization::CustomizeChildren(
    TSharedRef<IPropertyHandle> StructPropertyHandle,
    IDetailChildrenBuilder& ChildBuilder,
    IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
    // Intentionally empty — both fields are surfaced in the header row.
    // Suppressing children prevents "Graph" and "bUseGraph" from appearing
    // again below the header.
}

#undef LOCTEXT_NAMESPACE

