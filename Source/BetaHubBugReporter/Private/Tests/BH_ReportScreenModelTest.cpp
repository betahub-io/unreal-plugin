// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_ReportScreenModel.h"

#define BH_MODEL_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext \
    | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

using namespace BH_ReportScreenModel;

namespace
{
    FBH_ScreenState Compose(int32 Attachments = 3)
    {
        FBH_ScreenState S;
        S.Description = TEXT("The sword vanishes.");
        S.Focus.Reset(FieldsFor(S.Type, S.bTermsRequired), Attachments, EBH_Field::Description);
        S.AttachmentIncluded.Init(true, Attachments);
        return S;
    }

    TArray<FString> Labels(const TArray<FBH_Prompt>& Prompts)
    {
        TArray<FString> Out;
        for (const FBH_Prompt& P : Prompts)
        {
            Out.Add(P.Label.ToString());
        }
        return Out;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHFocusTest, "BetaHub.ReportScreen.Focus", BH_MODEL_TEST_FLAGS)
bool FBHFocusTest::RunTest(const FString& Parameters)
{
    // Fields: Type, Description, Steps. Seven attachments: two columns.
    FBH_ComposeFocus F;
    F.Reset(FieldsFor(EBH_ReportType::Bug, false), 7, EBH_Field::Description);
    TestEqual(TEXT("two columns from six"), F.Columns(), 2);
    F.Move(EBH_Direction::Up);
    TestEqual(TEXT("up to the type row"), F.Field(), EBH_Field::Type);
    F.Move(EBH_Direction::Up);
    TestEqual(TEXT("stays at the top"), F.Field(), EBH_Field::Type);
    F.Move(EBH_Direction::Down);
    F.Move(EBH_Direction::Down);
    F.Move(EBH_Direction::Down);
    TestFalse(TEXT("past the last field into attachments"), F.OnField());
    TestEqual(TEXT("first attachment"), F.Attachment(), 0);

    F.Move(EBH_Direction::Right);
    TestEqual(TEXT("right in the row"), F.Attachment(), 1);
    F.Move(EBH_Direction::Right);
    TestEqual(TEXT("no wrap at the row end"), F.Attachment(), 1);
    F.FocusAttachment(5);
    F.Move(EBH_Direction::Down);
    TestEqual(TEXT("a5 down lands on the last item"), F.Attachment(), 6);
    F.FocusAttachment(1);
    F.Move(EBH_Direction::Up);
    TestTrue(TEXT("top row up goes to the last field"), F.OnField());
    TestEqual(TEXT("last field"), F.Field(), EBH_Field::Steps);

    F.FocusField(EBH_Field::Description);
    F.FocusAttachment(4);
    F.Move(EBH_Direction::Left);
    TestEqual(TEXT("left from column 0 returns to the remembered field"), F.Field(), EBH_Field::Description);
    F.Move(EBH_Direction::Right);
    TestEqual(TEXT("right returns to the remembered attachment"), F.Attachment(), 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHCanSendTest, "BetaHub.ReportScreen.CanSend", BH_MODEL_TEST_FLAGS)
bool FBHCanSendTest::RunTest(const FString& Parameters)
{
    FBH_ScreenState S = Compose();
    TestTrue(TEXT("described bug"), CanSend(S));
    S.Description = TEXT("   ");
    TestFalse(TEXT("blank"), CanSend(S));
    S.Description = FString::ChrN(DescriptionLimit + 1, TEXT('a'));
    TestFalse(TEXT("over the limit"), CanSend(S));

    S.Description = TEXT("Short idea");
    S.Type = EBH_ReportType::Suggestion;
    TestFalse(TEXT("suggestion under the minimum"), CanSend(S));
    TestEqual(TEXT("minimum tag"), DescriptionTag(S).Text.ToString(), FString(TEXT("10 / 80 min")));
    S.Description = FString::ChrN(80, TEXT('i'));
    TestTrue(TEXT("suggestion at the minimum"), CanSend(S));

    S.Type = EBH_ReportType::Bug;
    S.bTermsRequired = true;
    TestFalse(TEXT("terms not accepted"), CanSend(S));
    S.bTermsAccepted = true;
    TestTrue(TEXT("terms accepted"), CanSend(S));
    S.bConfigured = false;
    TestFalse(TEXT("unconfigured"), CanSend(S));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHPromptsTest, "BetaHub.ReportScreen.Prompts", BH_MODEL_TEST_FLAGS)
bool FBHPromptsTest::RunTest(const FString& Parameters)
{
    FBH_ScreenState S = Compose();
    FBH_PromptBar Bar = Prompts(S);
    TestEqual(TEXT("field focused"), Labels(Bar.Left), TArray<FString>{TEXT("Write"), TEXT("Close")});
    TestTrue(TEXT("send is primary"), Bar.Right[0].bPrimary);

    S.bTyping = true;
    TestEqual(TEXT("typing on keyboard"), Labels(Prompts(S).Left), TArray<FString>{TEXT("Done"), TEXT("Next field")});
    S.bPadGlyphs = true;
    TestEqual(TEXT("typing on a pad"), Labels(Prompts(S).Left), TArray<FString>{TEXT("Done")});
    S.bTyping = false;

    S.Focus.FocusAttachment(0);
    TestEqual(TEXT("attachment"), Labels(Prompts(S).Left), TArray<FString>{TEXT("Leave out"), TEXT("Full screen"), TEXT("Close")});
    S.AttachmentIncluded[0] = false;
    TestEqual(TEXT("left out attachment"), Prompts(S).Left[0].Label.ToString(), FString(TEXT("Include")));

    S.Description.Empty();
    Bar = Prompts(S);
    TestTrue(TEXT("send disabled without text"), Bar.Right[0].bDisabled);
    S.Description = TEXT("x");
    S.SubmissionError = TEXT("Network down.");
    TestEqual(TEXT("retry"), Prompts(S).Right[0].Label.ToString(), FString(TEXT("Try again")));

    S.Screen = EBH_Screen::Sending;
    Bar = Prompts(S);
    TestEqual(TEXT("sending: nothing on the left"), Bar.Left.Num(), 0);
    TestTrue(TEXT("busy"), Bar.Right[0].bBusy);

    S.Screen = EBH_Screen::Sent;
    S.bCanEmail = true;
    Bar = Prompts(S);
    TestEqual(TEXT("sent, can email"), Labels(Bar.Left), TArray<FString>{TEXT("Write"), TEXT("Copy link"), TEXT("Back to game")});
    TestEqual(TEXT("email is the main action"), Bar.Right[0].Label.ToString(), FString(TEXT("Email me a link")));
    S.bCanEmail = false;
    Bar = Prompts(S);
    TestEqual(TEXT("sent, done"), Bar.Right[0].Label.ToString(), FString(TEXT("Back to game")));

    S.Screen = EBH_Screen::Preview;
    S.bPreviewIsLog = true;
    TestEqual(TEXT("log preview"), Labels(Prompts(S).Left).Num(), 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHHeaderTest, "BetaHub.ReportScreen.Header", BH_MODEL_TEST_FLAGS)
bool FBHHeaderTest::RunTest(const FString& Parameters)
{
    FBH_ScreenState S = Compose();
    TestEqual(TEXT("bug"), Header(S).Title.ToString(), FString(TEXT("REPORT A BUG")));
    S.Type = EBH_ReportType::Suggestion;
    TestEqual(TEXT("suggestion"), Header(S).Title.ToString(), FString(TEXT("SUGGEST AN IDEA")));
    S.Type = EBH_ReportType::Bug;
    S.SubmissionError = TEXT("Network down.");
    FBH_Header H = Header(S);
    TestEqual(TEXT("error title"), H.Title.ToString(), FString(TEXT("COULDN'T SEND")));
    TestEqual(TEXT("error keeps the text"), H.Subtitle.ToString(), FString(TEXT("Network down. Your text is kept.")));
    TestEqual(TEXT("error tone"), H.Tone, EBH_Tone::Err);
    S.Screen = EBH_Screen::Sent;
    S.IssueId = TEXT("4821");
    TestEqual(TEXT("sent"), Header(S).Subtitle.ToString(), FString(TEXT("#4821 · saved anonymously")));
    S.Screen = EBH_Screen::Compose;
    S.bConfigured = false;
    S.ConfigurationError = TEXT("Project ID is not set.");
    S.bDevelopmentBuild = true;
    TestEqual(TEXT("dev build sees why"), Header(S).Subtitle.ToString(), FString(TEXT("Project ID is not set. (see console)")));
    S.bDevelopmentBuild = false;
    TestEqual(TEXT("players do not"), Header(S).Subtitle.ToString(), FString(TEXT("Bug reporting isn't available right now.")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHTagsAndValuesTest, "BetaHub.ReportScreen.TagsAndValues", BH_MODEL_TEST_FLAGS)
bool FBHTagsAndValuesTest::RunTest(const FString& Parameters)
{
    FBH_ScreenState S = Compose();
    TestEqual(TEXT("required"), DescriptionTag(S).Text.ToString(), FString(TEXT("required")));
    S.Description = FString::ChrN(1600, TEXT('a'));
    TestEqual(TEXT("counts from 80%"), DescriptionTag(S).Text.ToString(), FString(TEXT("1,600 / 2,000")));
    TestFalse(TEXT("not yet at the limit"), DescriptionTag(S).bWarn);
    S.Description = FString::ChrN(DescriptionLimit, TEXT('a'));
    TestTrue(TEXT("at the limit"), DescriptionTag(S).bWarn);

    FBH_UploadState Upload;
    Upload.Status = EBH_UploadStatus::Uploading;
    Upload.Progress = 0.999f;
    TestEqual(TEXT("never shows 100 while uploading"), UploadValue(true, Upload).Text.ToString(), FString(TEXT("99%")));
    Upload.Status = EBH_UploadStatus::Sent;
    TestEqual(TEXT("sent"), UploadValue(true, Upload).Tone, EBH_Tone::Ok);
    TestEqual(TEXT("left out"), UploadValue(false, Upload).Text.ToString(), FString(TEXT("LEFT OUT")));

    const FString Log = TEXT("a\n\nb\nLogTemp: Warning: slow\nLogTemp: Error: broke\n");
    TArray<FString> Tail = LogTail(Log, 3);
    TestEqual(TEXT("tail length"), Tail.Num(), 3);
    TestEqual(TEXT("tail oldest first"), Tail[0], FString(TEXT("b")));
    TestEqual(TEXT("warning tone"), LogLineTone(Tail[1]), EBH_Tone::Warn);
    TestEqual(TEXT("error tone"), LogLineTone(Tail[2]), EBH_Tone::Err);

    TestTrue(TEXT("email"), LooksLikeEmail(TEXT(" a@b ")));
    TestFalse(TEXT("not an email"), LooksLikeEmail(TEXT("ab")));
    TestEqual(TEXT("screens"), ScreenFor(EBH_SubmissionState::AwaitingPublish), EBH_Screen::Sent);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
