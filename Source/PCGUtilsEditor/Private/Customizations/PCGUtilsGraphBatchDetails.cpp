#include "Customizations/PCGUtilsGraphBatchDetails.h"

#include "Data/PCGUtilsGraphBatch.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PCGUtilsGraphBatchDetails"

TSharedRef<IDetailCustomization> FPCGUtilsGraphBatchDetails::MakeInstance()
{
	return MakeShared<FPCGUtilsGraphBatchDetails>();
}

void FPCGUtilsGraphBatchDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() == 1)
	{
		Batch = Cast<UPCGUtilsGraphBatch>(Objects[0].Get());
	}

	IDetailCategoryBuilder& ExecutionCategory = DetailBuilder.EditCategory(
		TEXT("Execution"),
		LOCTEXT("ExecutionCategory", "Execution"),
		ECategoryPriority::Important);

	ExecutionCategory.AddCustomRow(LOCTEXT("RunBatchFilter", "Run Batch"))
		.WholeRowContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 4.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SBox)
					.MinDesiredHeight(34.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("RunBatchButton", "Run Batch"))
						.ToolTipText(LOCTEXT("RunBatchTooltip", "Validate and sequentially execute all enabled standalone asset graphs."))
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.IsEnabled_Lambda([WeakBatch = Batch]()
						{
							return WeakBatch.IsValid() && WeakBatch->CanRunBatch();
						})
						.OnClicked_Lambda([WeakBatch = Batch]()
						{
							if (UPCGUtilsGraphBatch* BatchPtr = WeakBatch.Get())
							{
								BatchPtr->RunBatch();
							}
							return FReply::Handled();
						})
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.MinDesiredHeight(34.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("CancelBatchButton", "Cancel"))
						.ToolTipText(LOCTEXT("CancelBatchTooltip", "Cancel the graph that is currently executing and stop the batch."))
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.IsEnabled_Lambda([WeakBatch = Batch]()
						{
							return WeakBatch.IsValid() && WeakBatch->IsBatchRunning();
						})
						.OnClicked_Lambda([WeakBatch = Batch]()
						{
							if (UPCGUtilsGraphBatch* BatchPtr = WeakBatch.Get())
							{
								BatchPtr->CancelBatch();
							}
							return FReply::Handled();
						})
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.f, 2.f, 2.f, 4.f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([WeakBatch = Batch]()
				{
					if (const UPCGUtilsGraphBatch* BatchPtr = WeakBatch.Get())
					{
						const FText Status = BatchPtr->GetExecutionStatusText();
						return Status.IsEmpty()
							? LOCTEXT("ReadyStatus", "Ready. Entries run sequentially in array order.")
							: Status;
					}
					return LOCTEXT("MultipleSelectionStatus", "Select one PCG Graph Batch to execute it.");
				})
			]
		];
}

#undef LOCTEXT_NAMESPACE
