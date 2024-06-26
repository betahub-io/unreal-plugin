#include "BH_ReportFormWidget.h"

void UBH_ReportFormWidget::Init(UBH_GameRecorder* InGameRecorder)
{
    GameRecorder = InGameRecorder;
}

void UBH_ReportFormWidget::SubmitReport()
{
    if (GameRecorder)
    {
        FString RecordingData = GameRecorder->SaveRecording();
        // Add code to submit the report along with the recording data
    }
}