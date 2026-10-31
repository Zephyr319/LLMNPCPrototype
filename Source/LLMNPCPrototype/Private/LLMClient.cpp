#include "LLMClient.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "VoicevoxTTSClient.h"
#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "EmotionClient.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformMisc.h"
#include "NPCActionQueueComponent.h"
#include "NPCAutonomyComponent.h"
#include "NPCPersonaProfile.h"

FOnInteractiveLLMRequestStartedNative ULLMClient::OnAnyInteractiveRequestStarted;
FOnInteractionMemoryExtractedNative ULLMClient::OnInteractionMemoryExtracted;
FOnNPCSocialExpressionRequestedNative ULLMClient::OnNPCSocialExpressionRequested;
FOnNPCEmbodiedResponseRequestedNative ULLMClient::OnNPCEmbodiedResponseRequested;
FOnInteractiveReplyFinishedNative ULLMClient::OnInteractiveReplyFinished;
TWeakObjectPtr<ULLMClient> ULLMClient::MostRecentInteractiveClient;

namespace
{
    int32 GServiceSpeechDispatchDepth = 0;

    const FString OpenAIChatCompletionsUrl = TEXT("https://api.openai.com/v1/chat/completions");

    bool ContainsAnyExpressionTerm(const FString& Text, const TArray<FString>& Terms)
    {
        return Terms.ContainsByPredicate([&Text](const FString& Term)
        {
            return Text.Contains(Term, ESearchCase::IgnoreCase);
        });
    }

    bool TryResolveDirectExpressionCommand(
        const FString& UserMessage,
        FString& OutExpressionIntent,
        FString& OutDescription)
    {
        const FString Message = UserMessage.TrimStartAndEnd().ToLower();
        const bool bLooksLikeCommand = ContainsAnyExpressionTerm(Message, {
            TEXT("表情"), TEXT("做出"), TEXT("做个"), TEXT("露出"),
            TEXT("笑一下"), TEXT("笑一个"), TEXT("笑给我"), TEXT("请笑"),
            TEXT("开心一点"), TEXT("高兴一点"), TEXT("难过一点"), TEXT("伤心一点"),
            TEXT("生气一下"), TEXT("思考一下"), TEXT("恢复正常"), TEXT("恢复表情"),
            TEXT("眨眼"), TEXT("眨一下"), TEXT("眨眨眼"),
            TEXT("笑って"), TEXT("微笑んで"), TEXT("表情をして"), TEXT("顔をして"),
            TEXT("怒って"), TEXT("驚いて"), TEXT("真顔"), TEXT("瞬きして")
        });
        if (!bLooksLikeCommand)
        {
            return false;
        }

        if (ContainsAnyExpressionTerm(Message, {
            TEXT("眨眼"), TEXT("眨一下"), TEXT("眨眨眼"), TEXT("瞬きして")
        }))
        {
            OutExpressionIntent = TEXT("blink");
        }
        else if (ContainsAnyExpressionTerm(Message, {
            TEXT("恢复正常"), TEXT("恢复表情"), TEXT("正常表情"), TEXT("面无表情"),
            TEXT("普通の表情"), TEXT("真顔")
        }))
        {
            OutExpressionIntent = TEXT("neutral");
        }
        else if (ContainsAnyExpressionTerm(Message, {
            TEXT("生气"), TEXT("愤怒"), TEXT("发火"), TEXT("怒って"), TEXT("怒った")
        }))
        {
            OutExpressionIntent = TEXT("angry");
        }
        else if (ContainsAnyExpressionTerm(Message, {
            TEXT("难过"), TEXT("伤心"), TEXT("悲伤"), TEXT("沮丧"),
            TEXT("悲しい"), TEXT("落ち込ん")
        }))
        {
            OutExpressionIntent = TEXT("sigh");
        }
        else if (ContainsAnyExpressionTerm(Message, {
            TEXT("思考"), TEXT("思索"), TEXT("若有所思"), TEXT("疑惑"),
            TEXT("考えて"), TEXT("考え込ん")
        }))
        {
            OutExpressionIntent = TEXT("pensive");
        }
        else if (ContainsAnyExpressionTerm(Message, {
            TEXT("开心"), TEXT("高兴"), TEXT("微笑"), TEXT("笑一下"), TEXT("笑一个"),
            TEXT("笑给我"), TEXT("请笑"), TEXT("笑って"), TEXT("微笑んで"), TEXT("嬉しい")
        }))
        {
            OutExpressionIntent = TEXT("smile");
        }
        else if (ContainsAnyExpressionTerm(Message, {
            TEXT("惊讶"), TEXT("吃惊"), TEXT("警觉"), TEXT("驚いて"), TEXT("驚いた")
        }))
        {
            OutExpressionIntent = TEXT("alert");
        }
        else
        {
            return false;
        }

        OutDescription = UserMessage.TrimStartAndEnd();
        return true;
    }

    FString GetOpenAIApiKey()
    {
        FString ApiKey = FPlatformMisc::GetEnvironmentVariable(TEXT("OPENAI_API_KEY"));
        ApiKey = ApiKey.TrimStartAndEnd();

        if (ApiKey.IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("[OpenAI] OPENAI_API_KEY environment variable is not set"));
        }

        return ApiKey;
    }

    FString CleanJsonContent(FString Content)
    {
        Content = Content.TrimStartAndEnd();

        if (Content.StartsWith(TEXT("```")))
        {
            int32 FirstNewLineIndex = INDEX_NONE;

            if (Content.FindChar(TEXT('\n'), FirstNewLineIndex))
            {
                Content = Content.RightChop(FirstNewLineIndex + 1);
            }

            Content = Content.TrimStartAndEnd();

            if (Content.EndsWith(TEXT("```")))
            {
                Content.LeftChopInline(3);
            }

            Content = Content.TrimStartAndEnd();
        }

        return Content;
    }

    bool TryDeserializeJsonObjectContent(
        const FString& Content,
        TSharedPtr<FJsonObject>& OutObject,
        FString& OutJsonContent,
        FString& OutLeadingText)
    {
        OutObject.Reset();
        OutJsonContent.Reset();
        OutLeadingText.Reset();

        auto TryDeserialize = [&OutObject](const FString& Candidate)
        {
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Candidate);
            return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
        };

        if (TryDeserialize(Content))
        {
            OutJsonContent = Content;
            return true;
        }

        const int32 FirstBrace = Content.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart);
        const int32 LastBrace = Content.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        if (FirstBrace == INDEX_NONE || LastBrace <= FirstBrace)
        {
            return false;
        }

        const FString Candidate = Content.Mid(FirstBrace, LastBrace - FirstBrace + 1).TrimStartAndEnd();
        if (!TryDeserialize(Candidate))
        {
            return false;
        }

        OutJsonContent = Candidate;
        OutLeadingText = Content.Left(FirstBrace).TrimStartAndEnd();
        OutLeadingText.RemoveFromStart(TEXT("```json"), ESearchCase::IgnoreCase);
        OutLeadingText.RemoveFromStart(TEXT("```"), ESearchCase::CaseSensitive);
        OutLeadingText = OutLeadingText.TrimStartAndEnd();
        return true;
    }

    FString BuildStrategyUserContent(const FString& UserMessage)
    {
        const bool bPlayerInTableArea = UNPCActionQueueComponent::IsLastKnownPlayerInTableArea();
        const bool bSeatedForService = UNPCActionQueueComponent::IsLastKnownSeatedForService();
        const bool bGuidingPlayer = UNPCActionQueueComponent::IsLastKnownGuidePlayerInProgress();
        const bool bSeatingGuidanceCompleted = UNPCActionQueueComponent::IsLastKnownSeatingGuidanceCompleted();
		const bool bMealCompletionConfirmationPending = UNPCActionQueueComponent::IsLastKnownMealCompletionConfirmationPending();
		const bool bDiningServiceCompleted = UNPCActionQueueComponent::IsLastKnownDiningServiceCompleted();
        const FString CurrentTableID = UNPCActionQueueComponent::GetLastKnownCurrentServiceTableID();
        const FString CurrentGuideTargetID = UNPCActionQueueComponent::GetLastKnownGuideTargetID();
		const FString MealCompletionTableID = UNPCActionQueueComponent::GetLastKnownMealCompletionConfirmationTableID();
		const FString CompletedServiceTableID = UNPCActionQueueComponent::GetLastKnownCompletedServiceTableID();
		const FString ServiceState = bMealCompletionConfirmationPending
			? TEXT("awaiting_meal_completion_confirmation")
			: (bDiningServiceCompleted
			? TEXT("post_meal")
			: (bGuidingPlayer
            ? TEXT("guiding")
            : (bPlayerInTableArea
            ? TEXT("in_table_area")
			: (bSeatedForService ? TEXT("seated") : TEXT("reception")))));
		const FString PlayerVoiceEmotion = UEmotionClient::GetLatestPlayerVoiceEmotion();
		UNPCAutonomyComponent* AutonomyComponent = UNPCAutonomyComponent::GetPrimaryAutonomyComponent();
		const FString NPCBehaviorContext = AutonomyComponent
			? AutonomyComponent->BuildInteractiveDialogueContext()
			: TEXT("{}");
		UE_LOG(LogTemp, Log, TEXT("[DialogueContext] captured before interruption: %s"), *NPCBehaviorContext);
        return FString::Printf(
			TEXT("State: %s\nIsGuidingPlayer: %s\nCurrentGuideTargetID: %s\nIsSeatedForService: %s\nPlayerInTableArea: %s\nbPlayerInTableArea: %s\nCurrentServiceTableID: %s\nbSeatingGuidanceCompleted: %s\nSeatingGuidanceCompleted: %s\nMealCompletionConfirmationPending: %s\nMealCompletionConfirmationTableID: %s\nDiningServiceCompleted: %s\nCompletedServiceTableID: %s\nDetectedPlayerVoiceEmotion: %s\nNPCBehaviorContext: %s\nUser: %s"),
            *ServiceState,
            bGuidingPlayer ? TEXT("true") : TEXT("false"),
            CurrentGuideTargetID.IsEmpty() ? TEXT("none") : *CurrentGuideTargetID,
            bSeatedForService ? TEXT("true") : TEXT("false"),
            bPlayerInTableArea ? TEXT("true") : TEXT("false"),
            bPlayerInTableArea ? TEXT("true") : TEXT("false"),
            CurrentTableID.IsEmpty() ? TEXT("none") : *CurrentTableID,
            bSeatingGuidanceCompleted ? TEXT("true") : TEXT("false"),
            bSeatingGuidanceCompleted ? TEXT("true") : TEXT("false"),
			bMealCompletionConfirmationPending ? TEXT("true") : TEXT("false"),
			MealCompletionTableID.IsEmpty() ? TEXT("none") : *MealCompletionTableID,
			bDiningServiceCompleted ? TEXT("true") : TEXT("false"),
			CompletedServiceTableID.IsEmpty() ? TEXT("none") : *CompletedServiceTableID,
			*PlayerVoiceEmotion,
			*NPCBehaviorContext,
            *UserMessage);
    }

	bool ContainsAnyInteractionTerm(const FString& Text, const TArray<FString>& Terms)
	{
		return Terms.ContainsByPredicate([&Text](const FString& Term)
		{
			return Text.Contains(Term, ESearchCase::IgnoreCase);
		});
	}

	bool TryInferSeatInvitationResponseAction(const FString& UserMessage, FString& OutAction)
	{
		const FString Message = UserMessage.TrimStartAndEnd();
		const TArray<FString> DeclineTerms = {
			TEXT("不可以"), TEXT("不行"), TEXT("不用"), TEXT("不要"), TEXT("别坐"),
			TEXT("现在不方便"), TEXT("下次吧"), TEXT("いいえ"), TEXT("だめ"), TEXT("座らないで"),
			TEXT("遠慮"), TEXT("今はちょっと"), TEXT("not now"), TEXT("don't"), TEXT("do not"), TEXT("no")
		};
		if (ContainsAnyInteractionTerm(Message, DeclineTerms))
		{
			OutAction = TEXT("decline_seat_invitation");
			return true;
		}

		const TArray<FString> AcceptTerms = {
			TEXT("可以"), TEXT("请坐"), TEXT("坐吧"), TEXT("当然"), TEXT("好啊"), TEXT("好呀"),
			TEXT("没问题"), TEXT("来吧"), TEXT("はい"), TEXT("どうぞ"), TEXT("座ってください"),
			TEXT("もちろん"), TEXT("構いません"), TEXT("いいですよ"), TEXT("go ahead"),
			TEXT("please sit"), TEXT("sure"), TEXT("okay"), TEXT("yes"), TEXT("ok")
		};
		if (ContainsAnyInteractionTerm(Message, AcceptTerms))
		{
			OutAction = TEXT("accept_seat_invitation");
			return true;
		}

		OutAction.Reset();
		return false;
	}

	bool IsDirectSitOppositeRequest(const FString& UserMessage)
	{
		const FString Message = UserMessage.TrimStartAndEnd();
		const TArray<FString> RefusalTerms = {
			TEXT("不要坐"), TEXT("别坐"), TEXT("不用坐"), TEXT("不许坐"),
			TEXT("座らないで"), TEXT("座らなくていい"), TEXT("don't sit"), TEXT("do not sit")
		};
		if (ContainsAnyInteractionTerm(Message, RefusalTerms))
		{
			return false;
		}

		const TArray<FString> DirectSitTerms = {
			TEXT("坐到我的对面"), TEXT("坐到我对面"), TEXT("坐我对面"),
			TEXT("在我对面坐"), TEXT("来我对面坐"), TEXT("陪我坐下"),
			TEXT("坐下来陪我"), TEXT("坐下来聊"), TEXT("坐下陪我聊"),
			TEXT("向かいに座"), TEXT("向かいの席に座"), TEXT("座って話"),
			TEXT("sit across from me"), TEXT("sit opposite me"), TEXT("sit with me")
		};
		return ContainsAnyInteractionTerm(Message, DirectSitTerms);
	}

	FNPCInteractionMemorySignal BuildFallbackInteractionMemorySignal(const FString& UserMessage)
	{
		FNPCInteractionMemorySignal Signal;
		Signal.PlayerUtterance = UserMessage.TrimStartAndEnd();
		const TArray<FString> PositiveTerms = {
			TEXT("ありがとう"), TEXT("ありがと"), TEXT("おいしい"), TEXT("美味しい"), TEXT("最高"),
			TEXT("素敵"), TEXT("親切"), TEXT("上手"), TEXT("好き"), TEXT("谢谢"), TEXT("好吃"),
			TEXT("很棒"), TEXT("喜欢"), TEXT("thank"), TEXT("great"), TEXT("delicious"), TEXT("good job")
		};
		const TArray<FString> NegativeTerms = {
			TEXT("まずい"), TEXT("遅い"), TEXT("ひどい"), TEXT("最悪"), TEXT("失礼"), TEXT("嫌い"),
			TEXT("难吃"), TEXT("太慢"), TEXT("糟糕"), TEXT("讨厌"), TEXT("bad"), TEXT("awful"),
			TEXT("terrible"), TEXT("too slow")
		};

		if (ContainsAnyInteractionTerm(Signal.PlayerUtterance, PositiveTerms))
		{
			Signal.bShouldRemember = true;
			Signal.EventTag = FName(TEXT("player_positive_feedback"));
			Signal.Summary = FString::Printf(TEXT("The customer gave Sumiha positive feedback: %s"), *Signal.PlayerUtterance);
			Signal.CueTags = { FName(TEXT("player")), FName(TEXT("dialogue")), FName(TEXT("praise")), FName(TEXT("service")) };
			Signal.Valence = 0.75f;
			Signal.Arousal = 0.45f;
			Signal.Importance = 0.8f;
		}
		else if (ContainsAnyInteractionTerm(Signal.PlayerUtterance, NegativeTerms))
		{
			Signal.bShouldRemember = true;
			Signal.EventTag = FName(TEXT("player_negative_feedback"));
			Signal.Summary = FString::Printf(TEXT("The customer criticized Sumiha or the service: %s"), *Signal.PlayerUtterance);
			Signal.CueTags = { FName(TEXT("player")), FName(TEXT("dialogue")), FName(TEXT("complaint")), FName(TEXT("service")) };
			Signal.Valence = -0.7f;
			Signal.Arousal = 0.6f;
			Signal.Importance = 0.85f;
		}
		return Signal;
	}

	FNPCInteractionMemorySignal ParseInteractionMemorySignal(
		const TSharedPtr<FJsonObject>& StrategyRoot,
		const FString& UserMessage)
	{
		FNPCInteractionMemorySignal Signal;
		Signal.PlayerUtterance = UserMessage.TrimStartAndEnd();
		const TSharedPtr<FJsonObject>* MemoryObject = nullptr;
		if (!StrategyRoot.IsValid()
			|| !StrategyRoot->TryGetObjectField(TEXT("interaction_memory"), MemoryObject)
			|| !MemoryObject
			|| !MemoryObject->IsValid())
		{
			return BuildFallbackInteractionMemorySignal(UserMessage);
		}

		(*MemoryObject)->TryGetBoolField(TEXT("should_remember"), Signal.bShouldRemember);
		FString EventType;
		(*MemoryObject)->TryGetStringField(TEXT("event_type"), EventType);
		Signal.EventTag = EventType.TrimStartAndEnd().IsEmpty()
			? FName(TEXT("player_meaningful_interaction"))
			: FName(*EventType.TrimStartAndEnd().ToLower());
		(*MemoryObject)->TryGetStringField(TEXT("summary"), Signal.Summary);
		double Number = 0.0;
		if ((*MemoryObject)->TryGetNumberField(TEXT("valence"), Number))
		{
			Signal.Valence = static_cast<float>(FMath::Clamp(Number, -1.0, 1.0));
		}
		if ((*MemoryObject)->TryGetNumberField(TEXT("arousal"), Number))
		{
			Signal.Arousal = static_cast<float>(FMath::Clamp(Number, 0.0, 1.0));
		}
		if ((*MemoryObject)->TryGetNumberField(TEXT("importance"), Number))
		{
			Signal.Importance = static_cast<float>(FMath::Clamp(Number, 0.0, 1.0));
		}
		const TArray<TSharedPtr<FJsonValue>>* CueValues = nullptr;
		if ((*MemoryObject)->TryGetArrayField(TEXT("cue_tags"), CueValues) && CueValues)
		{
			for (const TSharedPtr<FJsonValue>& CueValue : *CueValues)
			{
				FString Cue;
				if (CueValue.IsValid() && CueValue->TryGetString(Cue) && !Cue.TrimStartAndEnd().IsEmpty())
				{
					Signal.CueTags.AddUnique(FName(*Cue.TrimStartAndEnd().ToLower()));
				}
			}
		}
		Signal.Summary = Signal.Summary.TrimStartAndEnd();
		if (Signal.bShouldRemember && Signal.Summary.IsEmpty())
		{
			Signal.Summary = FString::Printf(TEXT("A meaningful customer interaction occurred: %s"), *Signal.PlayerUtterance);
		}
		return Signal;
	}

    bool IsOrderFulfillmentActionType(const FString& ActionType)
    {
        return ActionType.Equals(TEXT("place_order"), ESearchCase::IgnoreCase)
            || ActionType.Equals(TEXT("PlaceOrder"), ESearchCase::IgnoreCase)
            || ActionType.Equals(TEXT("order"), ESearchCase::IgnoreCase)
            || ActionType.Equals(TEXT("serve"), ESearchCase::IgnoreCase);
    }

    bool ExtractOpenAIMessageContent(
        const FString& ResponseString,
        FString& OutContent,
        FString& OutError
    )
    {
        TSharedPtr<FJsonObject> RootObject;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseString);

        if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
        {
            OutError = TEXT("Failed to parse OpenAI response JSON.");
            return false;
        }

        const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
        if (!RootObject->TryGetArrayField(TEXT("choices"), Choices) || Choices->Num() == 0)
        {
            OutError = TEXT("No choices found in OpenAI response.");
            return false;
        }

        TSharedPtr<FJsonObject> ChoiceObject = (*Choices)[0]->AsObject();
        if (!ChoiceObject.IsValid())
        {
            OutError = TEXT("Invalid choice object in OpenAI response.");
            return false;
        }

        TSharedPtr<FJsonObject> MessageObject = ChoiceObject->GetObjectField(TEXT("message"));
        if (!MessageObject.IsValid())
        {
            OutError = TEXT("No message object found in OpenAI response.");
            return false;
        }

        if (!MessageObject->TryGetStringField(TEXT("content"), OutContent))
        {
            OutError = TEXT("No content field found in OpenAI response.");
            return false;
        }

        return true;
    }

    int32 FindClosingDelimiter(const FString& Text, TCHAR ClosingChar, int32 StartIndex)
    {
        for (int32 Index = StartIndex; Index < Text.Len(); ++Index)
        {
            if (Text[Index] == ClosingChar)
            {
                return Index;
            }
        }

        return INDEX_NONE;
    }

    void RemoveDelimitedSections(FString& Text, TCHAR OpeningChar, TCHAR ClosingChar)
    {
        int32 OpeningIndex = INDEX_NONE;
        while (Text.FindChar(OpeningChar, OpeningIndex))
        {
            const int32 ClosingIndex = FindClosingDelimiter(Text, ClosingChar, OpeningIndex + 1);
            const int32 RemoveCount = ClosingIndex == INDEX_NONE
                ? Text.Len() - OpeningIndex
                : ClosingIndex - OpeningIndex + 1;

            Text.RemoveAt(OpeningIndex, RemoveCount, EAllowShrinking::No);
        }
    }

    FString GetStringFieldOrDefault(
        const TSharedPtr<FJsonObject>& Object,
        const FString& FieldName,
        const FString& DefaultValue
    )
    {
        if (!Object.IsValid())
        {
            return DefaultValue;
        }

        FString Value;
        if (Object->TryGetStringField(FieldName, Value))
        {
            return Value;
        }

        return DefaultValue;
    }

    FNPCEmbodiedResponseSignal ParseEmbodiedResponseSignal(
        const TSharedPtr<FJsonObject>& StrategyRoot,
        bool bHasSpokenReply)
    {
        FNPCEmbodiedResponseSignal Signal;
        Signal.bHasSpokenReply = bHasSpokenReply;
        if (!StrategyRoot.IsValid() || !StrategyRoot->HasTypedField<EJson::Object>(TEXT("embodied_response")))
        {
            return Signal;
        }

        const TSharedPtr<FJsonObject> ResponseObject = StrategyRoot->GetObjectField(TEXT("embodied_response"));
        if (!ResponseObject.IsValid())
        {
            return Signal;
        }

        ResponseObject->TryGetBoolField(TEXT("enabled"), Signal.bEnabled);
        ResponseObject->TryGetBoolField(TEXT("resume_after_reply"), Signal.bResumeAfterReply);
        Signal.ReactionStyle = GetStringFieldOrDefault(ResponseObject, TEXT("reaction_style"), TEXT("neutral")).TrimStartAndEnd().ToLower();
        Signal.BaseActionPolicy = GetStringFieldOrDefault(ResponseObject, TEXT("base_action_policy"), TEXT("stop")).TrimStartAndEnd().ToLower();
        Signal.FaceIntent = GetStringFieldOrDefault(ResponseObject, TEXT("face"), TEXT("neutral")).TrimStartAndEnd().ToLower();
        Signal.GazeIntent = GetStringFieldOrDefault(ResponseObject, TEXT("gaze"), TEXT("none")).TrimStartAndEnd().ToLower();
        Signal.LeftHandIntent = GetStringFieldOrDefault(ResponseObject, TEXT("left_hand"), TEXT("keep_current")).TrimStartAndEnd().ToLower();
        Signal.RightHandIntent = GetStringFieldOrDefault(ResponseObject, TEXT("right_hand"), TEXT("keep_current")).TrimStartAndEnd().ToLower();
        Signal.VoiceIntent = GetStringFieldOrDefault(ResponseObject, TEXT("voice"), TEXT("normal")).TrimStartAndEnd().ToLower();
        Signal.PropIntent = GetStringFieldOrDefault(ResponseObject, TEXT("prop"), TEXT("keep_current")).TrimStartAndEnd().ToLower();
        Signal.DisplayNote = GetStringFieldOrDefault(ResponseObject, TEXT("display_note"), TEXT("")).TrimStartAndEnd();
        Signal.Reason = GetStringFieldOrDefault(ResponseObject, TEXT("reason"), TEXT("")).TrimStartAndEnd();
        return Signal;
    }

    int32 GetQuantityFromArgs(const TSharedPtr<FJsonObject>& ArgsObject)
    {
        if (!ArgsObject.IsValid())
        {
            return 0;
        }

        double QuantityNumber = 0.0;
        if (ArgsObject->TryGetNumberField(TEXT("quantity"), QuantityNumber))
        {
            return static_cast<int32>(QuantityNumber);
        }

        FString QuantityString;
        if (ArgsObject->TryGetStringField(TEXT("quantity"), QuantityString))
        {
            return FCString::Atoi(*QuantityString);
        }

        return 0;
    }

    struct FParsedStrategyAction
    {
        FString ActionType;
        FString Timing;
        FString Item;
        int32 Quantity = 0;
        float WaitSeconds = 0.0f;
    };

    bool IsCanonicalTableID(const FString& Item)
    {
        const FString TrimmedItem = Item.TrimStartAndEnd();
        return TrimmedItem.Len() == 8
            && TrimmedItem.StartsWith(TEXT("Table_"), ESearchCase::IgnoreCase)
            && FChar::IsDigit(TrimmedItem[6])
            && FChar::IsDigit(TrimmedItem[7]);
    }

    void AddSequenceStep(TArray<TSharedPtr<FJsonValue>>& Steps, const FString& StepType, const FString& Target = TEXT(""))
    {
        TSharedPtr<FJsonObject> StepObject = MakeShared<FJsonObject>();
        StepObject->SetStringField(TEXT("type"), StepType);
        if (!Target.IsEmpty())
        {
            StepObject->SetStringField(TEXT("target"), Target);
        }
        Steps.Add(MakeShared<FJsonValueObject>(StepObject));
    }

    void AddWaitSequenceStep(TArray<TSharedPtr<FJsonValue>>& Steps, float Seconds)
    {
        TSharedPtr<FJsonObject> StepObject = MakeShared<FJsonObject>();
        StepObject->SetStringField(TEXT("type"), TEXT("wait"));
        StepObject->SetNumberField(TEXT("seconds"), Seconds);
        Steps.Add(MakeShared<FJsonValueObject>(StepObject));
    }

    void AppendExistingSequenceSteps(TArray<TSharedPtr<FJsonValue>>& Steps, const FString& SequenceJson)
    {
        TArray<TSharedPtr<FJsonValue>> ExistingSteps;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SequenceJson);
        if (FJsonSerializer::Deserialize(Reader, ExistingSteps))
        {
            Steps.Append(ExistingSteps);
        }
    }

    FString BuildActionSequenceJson(const TArray<FParsedStrategyAction>& Actions)
    {
        TArray<TSharedPtr<FJsonValue>> Steps;
        for (const FParsedStrategyAction& Action : Actions)
        {
            const FString ActionType = Action.ActionType.TrimStartAndEnd();
            const FString Item = Action.Item.TrimStartAndEnd();

            if (ActionType.Equals(TEXT("action_sequence"), ESearchCase::IgnoreCase))
            {
                AppendExistingSequenceSteps(Steps, Item);
            }
            else if (ActionType.Equals(TEXT("table_route"), ESearchCase::IgnoreCase))
            {
                TArray<FString> TableIDs;
                Item.ParseIntoArray(TableIDs, TEXT(","), true);
                for (FString TableID : TableIDs)
                {
                    TableID = TableID.TrimStartAndEnd();
                    if (!TableID.IsEmpty())
                    {
                        AddSequenceStep(Steps, TEXT("move_to_table"), TableID);
                    }
                }
            }
            else if (ActionType.Equals(TEXT("guide_to_table"), ESearchCase::IgnoreCase))
            {
                AddSequenceStep(Steps, TEXT("move_to_table"), Item);
            }
            else if (ActionType.Equals(TEXT("guide_player_to_table"), ESearchCase::IgnoreCase))
            {
                AddSequenceStep(Steps, TEXT("guide_player_to_table"), Item);
            }
            else if (ActionType.Equals(TEXT("guide_player_to_scene_tag"), ESearchCase::IgnoreCase))
            {
                AddSequenceStep(Steps, TEXT("guide_player_to_scene_tag"), Item);
            }
            else if (ActionType.Equals(TEXT("stop_guide_player"), ESearchCase::IgnoreCase))
            {
                AddSequenceStep(Steps, TEXT("stop_guide_player"));
            }
            else if (ActionType.Equals(TEXT("counter_then_return"), ESearchCase::IgnoreCase))
            {
                AddSequenceStep(Steps, TEXT("move_to_scene_tag"), TEXT("counter"));
                AddWaitSequenceStep(Steps, 1.0f);
                if (Item.Equals(TEXT("current_table"), ESearchCase::IgnoreCase) || Item.IsEmpty())
                {
                    AddSequenceStep(Steps, TEXT("move_near_player"), TEXT("player"));
                }
                else
                {
                    AddSequenceStep(Steps, TEXT("move_to_table"), Item);
                }
            }
            else if (ActionType.Equals(TEXT("move_to_table"), ESearchCase::IgnoreCase)
                || ActionType.Equals(TEXT("move_to_scene_tag"), ESearchCase::IgnoreCase)
                || ActionType.Equals(TEXT("move_to_furniture_tag"), ESearchCase::IgnoreCase)
                || ActionType.Equals(TEXT("move_to_furniture_id"), ESearchCase::IgnoreCase))
            {
                AddSequenceStep(Steps, ActionType, Item);
            }
            else if (ActionType.Equals(TEXT("move_near_player"), ESearchCase::IgnoreCase))
            {
                AddSequenceStep(Steps, TEXT("move_near_player"), TEXT("player"));
            }
            else if (ActionType.Equals(TEXT("follow_player"), ESearchCase::IgnoreCase))
            {
                AddSequenceStep(Steps, TEXT("follow_player"), TEXT("player"));
            }
            else if (ActionType.Equals(TEXT("stop_follow_player"), ESearchCase::IgnoreCase))
            {
                AddSequenceStep(Steps, TEXT("stop_follow_player"), TEXT("player"));
            }
            else if (ActionType.Equals(TEXT("wait"), ESearchCase::IgnoreCase))
            {
                const float Seconds = Action.WaitSeconds > 0.0f
                    ? Action.WaitSeconds
                    : static_cast<float>(FMath::Max(Action.Quantity, 0));
                AddWaitSequenceStep(Steps, Seconds);
            }
            else if (ActionType.Equals(TEXT("face_player"), ESearchCase::IgnoreCase)
                || ActionType.Equals(TEXT("greet"), ESearchCase::IgnoreCase))
            {
                AddSequenceStep(Steps, TEXT("face_player"));
            }
        }

        FString SequenceJson;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SequenceJson);
        FJsonSerializer::Serialize(Steps, Writer);
        return SequenceJson;
    }

    FParsedStrategyAction MakeParsedStrategyAction(const FString& ActionType, const FString& Item)
    {
        FParsedStrategyAction ParsedAction;
        ParsedAction.ActionType = ActionType;
        ParsedAction.Timing = TEXT("immediate");
        ParsedAction.Item = Item;
        ParsedAction.Quantity = 0;
        ParsedAction.WaitSeconds = 0.0f;
        return ParsedAction;
    }

    int32 FindFirstTermIndex(const FString& Text, const TArray<FString>& Terms)
    {
        int32 BestIndex = INDEX_NONE;
        for (const FString& Term : Terms)
        {
            const int32 FoundIndex = Text.Find(*Term, ESearchCase::IgnoreCase);
            if (FoundIndex != INDEX_NONE && (BestIndex == INDEX_NONE || FoundIndex < BestIndex))
            {
                BestIndex = FoundIndex;
            }
        }

        return BestIndex;
    }

    bool ContainsAnyTerm(const FString& Text, const TArray<FString>& Terms)
    {
        return FindFirstTermIndex(Text, Terms) != INDEX_NONE;
    }

    int32 FindCounterMentionIndex(const FString& Text)
    {
        const TArray<FString> CounterTerms = {
            TEXT("counter"),
            TEXT("bar"),
            TEXT("\u5427\u53F0"),
            TEXT("\u30AB\u30A6\u30F3\u30BF\u30FC")
        };
        return FindFirstTermIndex(Text, CounterTerms);
    }

    bool ContainsReturnTerm(const FString& Text)
    {
        const TArray<FString> ReturnTerms = {
            TEXT("\u56DE\u6765"),
            TEXT("\u56DE\u5230"),
            TEXT("\u56DE\u53BB"),
            TEXT("\u8FD4\u56DE"),
            TEXT("\u623B"),
            TEXT("come back"),
            TEXT("return"),
            TEXT("back")
        };
        return ContainsAnyTerm(Text, ReturnTerms);
    }

    bool ContainsReturnToCounterTerm(const FString& Text)
    {
        const TArray<FString> ReturnToCounterTerms = {
            TEXT("\u56DE\u5230\u5427\u53F0"),
            TEXT("\u56DE\u5427\u53F0"),
            TEXT("\u8FD4\u56DE\u5427\u53F0"),
            TEXT("return to counter"),
            TEXT("return to the counter"),
            TEXT("back to counter"),
            TEXT("back to the counter"),
            TEXT("\u30AB\u30A6\u30F3\u30BF\u30FC\u306B\u623B")
        };
        return ContainsAnyTerm(Text, ReturnToCounterTerms);
    }

    bool LooksLikeDirectNPCCounterMove(const FString& Text)
    {
        if (FindCounterMentionIndex(Text) == INDEX_NONE)
        {
            return false;
        }

        const TArray<FString> GuidePlayerTerms = {
            TEXT("\u5E26\u6211"),
            TEXT("\u9886\u6211"),
            TEXT("\u966A\u6211"),
            TEXT("guide me"),
            TEXT("take me"),
            TEXT("show me"),
            TEXT("\u6848\u5185\u3057\u3066")
        };
        if (ContainsAnyTerm(Text, GuidePlayerTerms))
        {
            return false;
        }

        if (ContainsReturnToCounterTerm(Text))
        {
            return true;
        }

        const TArray<FString> DirectMoveTerms = {
            TEXT("\u53BB\u5427\u53F0"),
            TEXT("\u5230\u5427\u53F0"),
            TEXT("\u5728\u5427\u53F0\u7B49"),
            TEXT("\u5427\u53F0\u5F85"),
            TEXT("\u5427\u53F0\u5446"),
            TEXT("go to counter"),
            TEXT("go to the counter"),
            TEXT("wait at counter"),
            TEXT("wait at the counter"),
            TEXT("stay at counter"),
            TEXT("stay at the counter"),
            TEXT("\u30AB\u30A6\u30F3\u30BF\u30FC\u306B\u884C"),
            TEXT("\u30AB\u30A6\u30F3\u30BF\u30FC\u3067\u5F85")
        };
        return ContainsAnyTerm(Text, DirectMoveTerms);
    }

    bool ContainsSequenceTerm(const FString& Text)
    {
        const TArray<FString> SequenceTerms = {
            TEXT("\u5148"),
            TEXT("\u7136\u540E"),
            TEXT("\u518D"),
            TEXT("\u4E4B\u540E"),
            TEXT("and then"),
            TEXT("then"),
            TEXT("\u3057\u3066\u304B\u3089"),
            TEXT("\u304B\u3089")
        };
        return ContainsAnyTerm(Text, SequenceTerms);
    }

    void ConsiderTableAliases(
        const FString& Text,
        const FString& TableID,
        const TArray<FString>& Aliases,
        FString& BestTableID,
        int32& BestIndex
    )
    {
        const int32 AliasIndex = FindFirstTermIndex(Text, Aliases);
        if (AliasIndex != INDEX_NONE && (BestIndex == INDEX_NONE || AliasIndex < BestIndex))
        {
            BestIndex = AliasIndex;
            BestTableID = TableID;
        }
    }

    FString ExtractExplicitTableID(const FString& Text, int32& OutTableMentionIndex)
    {
        FString BestTableID;
        OutTableMentionIndex = INDEX_NONE;

        ConsiderTableAliases(
            Text,
            TEXT("Table_01"),
            {
                TEXT("Table_01"),
                TEXT("Table 01"),
                TEXT("Table 1"),
                TEXT("1\u53F7\u684C"),
                TEXT("1\u53F7"),
                TEXT("\uFF11\u53F7\u684C"),
                TEXT("\uFF11\u53F7"),
                TEXT("\u4E00\u53F7\u684C"),
                TEXT("\u4E00\u53F7"),
                TEXT("1\u756A\u30C6\u30FC\u30D6\u30EB"),
                TEXT("1\u756A")
            },
            BestTableID,
            OutTableMentionIndex
        );

        ConsiderTableAliases(
            Text,
            TEXT("Table_02"),
            {
                TEXT("Table_02"),
                TEXT("Table 02"),
                TEXT("Table 2"),
                TEXT("2\u53F7\u684C"),
                TEXT("2\u53F7"),
                TEXT("\uFF12\u53F7\u684C"),
                TEXT("\uFF12\u53F7"),
                TEXT("\u4E8C\u53F7\u684C"),
                TEXT("\u4E8C\u53F7"),
                TEXT("2\u756A\u30C6\u30FC\u30D6\u30EB"),
                TEXT("2\u756A")
            },
            BestTableID,
            OutTableMentionIndex
        );

        ConsiderTableAliases(
            Text,
            TEXT("Table_03"),
            {
                TEXT("Table_03"),
                TEXT("Table 03"),
                TEXT("Table 3"),
                TEXT("3\u53F7\u684C"),
                TEXT("3\u53F7"),
                TEXT("\uFF13\u53F7\u684C"),
                TEXT("\uFF13\u53F7"),
                TEXT("\u4E09\u53F7\u684C"),
                TEXT("\u4E09\u53F7"),
                TEXT("3\u756A\u30C6\u30FC\u30D6\u30EB"),
                TEXT("3\u756A")
            },
            BestTableID,
            OutTableMentionIndex
        );

        ConsiderTableAliases(
            Text,
            TEXT("Table_04"),
            {
                TEXT("Table_04"),
                TEXT("Table 04"),
                TEXT("Table 4"),
                TEXT("4\u53F7\u684C"),
                TEXT("4\u53F7"),
                TEXT("\uFF14\u53F7\u684C"),
                TEXT("\uFF14\u53F7"),
                TEXT("\u56DB\u53F7\u684C"),
                TEXT("\u56DB\u53F7"),
                TEXT("4\u756A\u30C6\u30FC\u30D6\u30EB"),
                TEXT("4\u756A")
            },
            BestTableID,
            OutTableMentionIndex
        );

        return BestTableID;
    }

    bool TryBuildCounterTableSequenceFromUserMessage(const FString& UserMessage, FString& OutSequenceJson)
    {
        const int32 CounterIndex = FindCounterMentionIndex(UserMessage);
        if (CounterIndex == INDEX_NONE)
        {
            return false;
        }

        int32 TableIndex = INDEX_NONE;
        const FString ExplicitTableID = ExtractExplicitTableID(UserMessage, TableIndex);
        if (ExplicitTableID.IsEmpty() || (!ContainsReturnTerm(UserMessage) && !ContainsSequenceTerm(UserMessage)))
        {
            return false;
        }

        TArray<FParsedStrategyAction> SequenceActions;
        if (TableIndex != INDEX_NONE && TableIndex < CounterIndex)
        {
            SequenceActions.Add(MakeParsedStrategyAction(TEXT("move_to_table"), ExplicitTableID));
            SequenceActions.Add(MakeParsedStrategyAction(TEXT("move_to_scene_tag"), TEXT("counter")));
        }
        else
        {
            SequenceActions.Add(MakeParsedStrategyAction(TEXT("move_to_scene_tag"), TEXT("counter")));
            SequenceActions.Add(MakeParsedStrategyAction(TEXT("move_to_table"), ExplicitTableID));
        }

        OutSequenceJson = BuildActionSequenceJson(SequenceActions);
        return true;
    }

    bool TryBuildCounterSemanticOverride(
        const FString& UserMessage,
        const FParsedStrategyAction& ParsedAction,
        FString& OutSequenceJson
    )
    {
        const bool bParsedAsCounterThenReturn =
            ParsedAction.ActionType.Equals(TEXT("counter_then_return"), ESearchCase::IgnoreCase);
        const bool bDirectNPCCounterMove = LooksLikeDirectNPCCounterMove(UserMessage);
        if (!bParsedAsCounterThenReturn && !bDirectNPCCounterMove)
        {
            return false;
        }

        if (TryBuildCounterTableSequenceFromUserMessage(UserMessage, OutSequenceJson))
        {
            return true;
        }

        const int32 CounterIndex = FindCounterMentionIndex(UserMessage);
        if (CounterIndex == INDEX_NONE)
        {
            return false;
        }

        TArray<FParsedStrategyAction> SequenceActions;
        if (bDirectNPCCounterMove
            || !ContainsReturnTerm(UserMessage)
            || ContainsReturnToCounterTerm(UserMessage))
        {
            SequenceActions.Add(MakeParsedStrategyAction(TEXT("move_to_scene_tag"), TEXT("counter")));
            OutSequenceJson = BuildActionSequenceJson(SequenceActions);
            return true;
        }

        return false;
    }
}

ULLMClient::ULLMClient()
{
    bUseVoicevoxTTS = true;
    VoicevoxTTSClient = nullptr;
    OpenAIModelName = TEXT("gpt-4o-mini");
    LastAutonomyPromptTokens = 0;
    LastAutonomyCompletionTokens = 0;
    LastAutonomyCachedPromptTokens = 0;
    LastAppraisalPromptTokens = 0;
    LastAppraisalCompletionTokens = 0;
    LastAppraisalCachedPromptTokens = 0;
    ActiveLLMRequestId = 0;
    ActiveAppraisalRequestId = 0;
    LastStrategyUserMessage = TEXT("");
    bInteractiveRequestPending = false;
    LastInteractiveOutputWorldTime = -1.0;
    bQueuedSpeechPlaying = false;
    bCurrentAudioIsQueuedServiceSpeech = false;
    QueuedSpeechGeneration = 0;
    ActiveQueuedSpeechGeneration = 0;
    MinSpeechGapSeconds = 0.35f;
    QueuedSpeechFallbackTimeoutSeconds = 30.0f;
    bHasDeferredStrategyAction = false;
    DeferredStrategyActionType = TEXT("");
    DeferredStrategyActionTiming = TEXT("");
    DeferredStrategyActionItem = TEXT("");
    DeferredStrategyActionQuantity = 0;
    DeferredStrategyRequestId = 0;
}

void ULLMClient::SetVoicevoxTTSClient(UVoicevoxTTSClient* InVoicevoxTTSClient)
{
    VoicevoxTTSClient = InVoicevoxTTSClient;
}

void ULLMClient::SpeakDirectText(const FString& Text)
{
    const FString TrimmedText = Text.TrimStartAndEnd();
    if (TrimmedText.IsEmpty())
    {
        return;
    }

    MostRecentInteractiveClient = this;

    if (IsServiceSpeechDispatchActive())
    {
        SpeakDirectTextQueued(TrimmedText);
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("[LLMClient] Immediate SpeakDirectText requested. Clearing service speech."));
    ClearQueuedSpeech();
    UE_LOG(LogTemp, Warning, TEXT("[Speech] SpeakDirectText: %s"), *TrimmedText);
    PlaySpeechInternal(TrimmedText, false, true);
}

void ULLMClient::SpeakDirectTextQueued(const FString& Text)
{
    const FString TrimmedText = Text.TrimStartAndEnd();
    if (TrimmedText.IsEmpty())
    {
        return;
    }

    MostRecentInteractiveClient = this;

    PendingServiceSpeechQueue.Add(TrimmedText);
    UE_LOG(LogTemp, Warning, TEXT("[SpeechQueue] Enqueued service speech: %s"), *TrimmedText);
    TryPlayNextQueuedSpeech();
}

void ULLMClient::ShowSilentDisplayText(const FString& Text)
{
    const FString TrimmedText = Text.TrimStartAndEnd();
    if (TrimmedText.IsEmpty())
    {
        return;
    }

    MostRecentInteractiveClient = this;
    UE_LOG(LogTemp, Log, TEXT("[LLMDisplay] silent text requested: %s"), *TrimmedText);
    PlaySpeechAndDisplayInternal(TrimmedText, TEXT(""), false, false);
}

void ULLMClient::ClearQueuedSpeech()
{
    PendingServiceSpeechQueue.Empty();
    ++QueuedSpeechGeneration;
    bQueuedSpeechPlaying = false;
    ActiveQueuedSpeechGeneration = QueuedSpeechGeneration;

    if (UWorld* World = ResolveSpeechWorld())
    {
        World->GetTimerManager().ClearTimer(QueuedSpeechFallbackTimerHandle);
        World->GetTimerManager().ClearTimer(QueuedSpeechGapTimerHandle);
    }

    if (bCurrentAudioIsQueuedServiceSpeech)
    {
        if (UVoicevoxTTSClient* ResolvedVoicevoxTTSClient = ResolveVoicevoxTTSClient())
        {
            ResolvedVoicevoxTTSClient->StopVoicevoxSpeech();
        }

        bCurrentAudioIsQueuedServiceSpeech = false;
        UE_LOG(LogTemp, Warning, TEXT("[SpeechQueue] Stopped current queued service audio due to immediate input."));
    }

    UE_LOG(LogTemp, Warning, TEXT("[SpeechQueue] Cleared queued service speech."));
}

bool ULLMClient::IsQueuedSpeechPlaying() const
{
    return bQueuedSpeechPlaying;
}

void ULLMClient::BeginServiceSpeechDispatch()
{
    ++GServiceSpeechDispatchDepth;
}

void ULLMClient::EndServiceSpeechDispatch()
{
    GServiceSpeechDispatchDepth = FMath::Max(GServiceSpeechDispatchDepth - 1, 0);
}

void ULLMClient::SendDeepSeekMessage(const FString& UserMessage)
{
	OnAnyInteractiveRequestStarted.Broadcast();
	FString ExpressionIntent;
	FString ExpressionDescription;
	if (TryResolveDirectExpressionCommand(UserMessage, ExpressionIntent, ExpressionDescription))
	{
		OnNPCSocialExpressionRequested.Broadcast(ExpressionIntent, ExpressionDescription, 12.0f);
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[ExpressionCommand] recognized. Intent=%s Message=%s"),
			*ExpressionIntent,
			*UserMessage);
	}
	UE_LOG(LogTemp, Warning, TEXT("[LLMClient] Clearing service speech before user request."));
    CancelDeferredStrategyAction(true);
    ClearQueuedSpeech();
    InterruptCurrentVoicevoxSpeech();

    const int32 LocalRequestId = ++ActiveLLMRequestId;
    const FString OpenAIApiKey = GetOpenAIApiKey();
    if (OpenAIApiKey.IsEmpty())
    {
        return;
    }

    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
    RootObject->SetStringField(TEXT("model"), OpenAIModelName);
    RootObject->SetBoolField(TEXT("stream"), false);
    RootObject->SetNumberField(TEXT("max_tokens"), 500);

    TArray<TSharedPtr<FJsonValue>> Messages;

    TSharedPtr<FJsonObject> SystemMessage = MakeShared<FJsonObject>();
    SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
    SystemMessage->SetStringField(
        TEXT("content"),
        LLMNPCPersona::CorePromptEnglish() + TEXT(
            "Respond as a cafe employee, not as an assistant describing the system.\n"
            "Reply warmly and thoughtfully, usually in 1 to 3 concise Japanese sentences.\n"
            "Acknowledge the user's feeling, situation, or question when it is relevant, then respond concretely.\n"
            "Offer a next step only when it is useful; do not force every reply into the same formula.\n"
            "Do not include stage directions.\n"
            "Do not describe gestures or facial expressions in parentheses.\n"
            "Output only Sumiha's spoken Japanese line.\n"
            "Available menu items are only: \u30E9\u30FC\u30E1\u30F3, \u30AB\u30EC\u30FC\u30E9\u30A4\u30B9, \u30B3\u30FC\u30D2\u30FC, \u6C34, \u30B5\u30E9\u30C0.\n"
            "Do not invent or corrupt menu names.\n"
            "Do not invent, mention, recommend, or offer unavailable menu items such as pasta, seafood, or desserts.\n"
            "If recommending food, choose only from \u30E9\u30FC\u30E1\u30F3 or \u30AB\u30EC\u30FC\u30E9\u30A4\u30B9 unless the user asks for drinks or salad.\n"
            "If the user asks for a recommendation, recommend only from the available menu and do not assume an order was placed.\n"
            "Do not explain your reasoning. Do not say you are an AI."
        )
    );
    Messages.Add(MakeShared<FJsonValueObject>(SystemMessage));

    for (const TSharedPtr<FJsonValue>& HistoryMessage : ConversationHistory)
    {
        Messages.Add(HistoryMessage);
    }

    TSharedPtr<FJsonObject> UserMsg = MakeShared<FJsonObject>();
    UserMsg->SetStringField(TEXT("role"), TEXT("user"));
    UserMsg->SetStringField(TEXT("content"), UserMessage);

    TSharedPtr<FJsonValueObject> UserValue = MakeShared<FJsonValueObject>(UserMsg);
    Messages.Add(UserValue);

    ConversationHistory.Add(UserValue);

    while (ConversationHistory.Num() > 10)
    {
        ConversationHistory.RemoveAt(0);
    }

    RootObject->SetArrayField(TEXT("messages"), Messages);

    FString RequestBody;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
    FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

    TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(OpenAIChatCompletionsUrl);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + OpenAIApiKey);
    Request->SetContentAsString(RequestBody);
    Request->SetTimeout(120.0f);

    Request->OnProcessRequestComplete().BindUObject(
        this,
        &ULLMClient::OnOpenAINormalResponseReceived,
        LocalRequestId
    );

    Request->ProcessRequest();

    UE_LOG(LogTemp, Warning, TEXT("OpenAI normal request sent: %s"), *UserMessage);
}

void ULLMClient::SendStrategyMessage(const FString& UserMessage)
{
	const FString StrategyUserContent = BuildStrategyUserContent(UserMessage);
	MostRecentInteractiveClient = this;
	OnAnyInteractiveRequestStarted.Broadcast();
	FString ExpressionIntent;
	FString ExpressionDescription;
	if (TryResolveDirectExpressionCommand(UserMessage, ExpressionIntent, ExpressionDescription))
	{
		OnNPCSocialExpressionRequested.Broadcast(ExpressionIntent, ExpressionDescription, 12.0f);
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[ExpressionCommand] recognized. Intent=%s Message=%s"),
			*ExpressionIntent,
			*UserMessage);
	}
	UE_LOG(LogTemp, Warning, TEXT("[LLMClient] Clearing service speech before user request."));
    CancelDeferredStrategyAction(true);
    ClearQueuedSpeech();
    InterruptCurrentVoicevoxSpeech();
    LastStrategyUserMessage = UserMessage;

    const int32 LocalRequestId = ++ActiveLLMRequestId;
    const FString OpenAIApiKey = GetOpenAIApiKey();
    if (OpenAIApiKey.IsEmpty())
    {
		bInteractiveRequestPending = false;
        return;
    }
	bInteractiveRequestPending = true;

    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
    RootObject->SetStringField(TEXT("model"), OpenAIModelName);
    RootObject->SetBoolField(TEXT("stream"), false);
    RootObject->SetNumberField(TEXT("temperature"), 0.2);
    RootObject->SetNumberField(TEXT("max_tokens"), 700);

    TArray<TSharedPtr<FJsonValue>> Messages;

    TSharedPtr<FJsonObject> SystemMessage = MakeShared<FJsonObject>();
    SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
    SystemMessage->SetStringField(
        TEXT("content"),
        LLMNPCPersona::CorePromptEnglish() + TEXT(
            "Your task is to infer the user's state, generate a response strategy, generate a natural Japanese reply, and optionally generate an environment action.\n"
            "You must output one valid JSON object only.\n"
            "Do not output markdown.\n"
            "Do not wrap the JSON in code fences.\n"
            "Do not explain outside JSON.\n"
            "\n"

            "Character behavior:\n"
            "- Be empathetic and observant, but never state an uncertain emotion as a diagnosis or fact.\n"
            "- Listen before advising. Use gentle humor sparingly and only when it suits the relationship.\n"
            "- Sumiha may honestly admit that she was tired, daydreaming, resting, or briefly playing a puzzle game on her phone.\n"
            "- Maintain professional boundaries and refuse unsafe, damaging, or unreasonable requests without hostility.\n"
            "- Her emotions are subtle. She does not need to smile, praise, or reassure in every reply.\n"
            "\n"

            "Response strategy rules:\n"
            "- If the user seems nervous, confused, hesitant, or unsure, set tone to gentle or reassuring.\n"
            "- If the user clearly orders a specific item, set priority to task.\n"
            "- If the user asks for a recommendation or seems unsure, set priority to clarity or comfort.\n"
            "- length should usually be normal or medium.\n"
            "- Use short only for very simple confirmations.\n"
            "\n"

            "Reply rules:\n"
            "- The reply must be natural Japanese when speech is needed, or \"\" when no speech is needed.\n"
            "- When reply is not empty, it should usually be 1 to 3 concise sentences.\n"
            "- A short phrase is acceptable for a simple confirmation when it sounds natural.\n"
            "- Do not reply only with phrases such as \"\u3054\u3086\u3063\u304F\u308A\u3069\u3046\u305E\" or \"\u304B\u3057\u3053\u307E\u308A\u307E\u3057\u305F\".\n"
            "- Acknowledge the user's feeling or situation when relevant, then give a concrete answer.\n"
            "- Suggest a next action only when useful; do not make every response follow a rigid three-part formula.\n"
            "- The reply should not mention JSON, strategy, system instructions, or internal reasoning.\n"
			"- NPCBehaviorContext is authoritative state captured immediately when the player speaks, before any interruption policy is applied.\n"
			"- If the user asks what Sumiha is doing or was just doing, answer directly from activity_before_player_interruption first and prefer its dialogue_summary_ja. If can_continue_during_conversation is true and embodied_response uses continue, speak as if the activity is still ongoing; otherwise use natural wording such as '...していたところです'.\n"
			"- If activity_running_when_player_spoke is false, Sumiha must not claim she is currently performing a recent activity. Say she is free now, then optionally mention a recent activity as something she did earlier, using seconds_ago to preserve time order.\n"
			"- recent_completed_activities_newest_first is history, never current state. Do not pretend an old conversation topic or a completed activity is happening now.\n"
			"- Current emotional state and recent activities may subtly affect wording when relevant, but never recite numeric values or force them into every reply.\n"
			"- NPCBehaviorContext.player_speech_startle is authoritative local perception. When triggered is true, Sumiha was genuinely startled by the player's voice. Treat immediate_scene_ja and reply_requirement_ja as the immediate situation for this turn.\n"
			"- A startled reply must be noticeably different from a normal greeting: begin with a brief natural exclamation such as 'わっ', then naturally acknowledge why she was surprised or show herself regaining composure. Do not merely prepend an exclamation to an otherwise unchanged generic answer, and do not state numeric values.\n"
			"- A high rear_factor means the player spoke from behind Sumiha. This can strengthen the startled delivery, but it does not by itself require abandoning the current activity.\n"
			"- Do not invent an activity, mood, plan, or memory that is absent from NPCBehaviorContext.\n"
			"- A question about Sumiha's current activity is ordinary conversation: normally output decision speak and actions [].\n"
			"\n"
			"Seat invitation consent rules:\n"
			"- When NPCBehaviorContext.seat_invitation.pending is true, Sumiha has already asked whether she may sit across from the player and is waiting for this user message as the answer.\n"
			"- Clear consent such as 可以, 请坐, はい, どうぞ, yes, or sure must output exactly one accept_seat_invitation action.\n"
			"- Clear refusal such as 不用, 不行, いいえ, 今はちょっと, no, or not now must output exactly one decline_seat_invitation action.\n"
			"- If the answer is genuinely ambiguous, ask one short clarification question and output no action. Never treat silence, an unrelated remark, or mere politeness as consent.\n"
			"- If the user requests service instead of answering, output the requested service action; the local system will cancel the seat invitation.\n"
			"- After acceptance, reply naturally in Japanese and acknowledge that Sumiha will sit down. After refusal, accept it politely without pressuring the player.\n"
			"- When the player directly asks Sumiha to sit across from them, sit opposite them, or sit down and talk, output exactly one sit_opposite_player action. This direct invitation already supplies consent and is not the same as accepting Sumiha's own pending invitation.\n"
			"- While activity_before_player_interruption.id is sit_with_player, ordinary conversation should use base_action_policy continue so Sumiha remains seated. Use stop or transition only for a service request, a request to stand, or another incompatible action.\n"
			"\n"
			"Embodied response rules:\n"
			"- Always include embodied_response. It describes visible and vocal delivery, not a service action.\n"
			"- Use enabled true when a visible or vocal reaction is appropriate; otherwise use enabled false and neutral defaults.\n"
			"- base_action_policy must be continue, soft_pause, stop, or transition. continue keeps the ongoing activity; soft_pause briefly pauses it and may resume after the reply; stop abandons it; transition stops it and prepares for a new requested action.\n"
			"- Preserve task continuity. Do not stop an ongoing harmless activity merely because the player speaks. Use stop or transition for service commands, safety, or genuine incompatibility.\n"
			"- If the user only asks what Sumiha is doing, comments on the visible activity, or makes ordinary conversation without requesting a new task, use base_action_policy continue. Do not stop the current activity just to answer.\n"
			"- Use only these face tags: neutral, smile, hum, sigh, angry, alert, pensive, blush, startled, startled_blush.\n"
			"- Use only these gaze tags: none, player, phone, alternate_phone_player.\n"
			"- Use only these hand tags: keep_current, none, shush, lower_phone, hide_phone. Hands occupied by a prop should normally keep_current; prefer shush on a free hand.\n"
			"- Request a hand gesture only when NPCBehaviorContext.embodied_capabilities.hand_gesture_executor_available is true. Otherwise use keep_current and express the reaction through face, gaze, wording, and display_note.\n"
			"- Use only these voice tags: normal, whisper, nervous, hurried, calm.\n"
			"- Use only these prop tags: keep_current, lower, hide, remove.\n"
			"- display_note must be a short Japanese stage direction without parentheses, describing only behavior supported by embodied_capabilities. Do not claim an unavailable hand gesture or audio voice effect occurred.\n"
			"- When player_speech_startle.triggered is true, use enabled true and face startled unless embarrassment makes startled_blush more suitable. The local system already decided that the startle occurred, so do not cancel or contradict it.\n"
			"- If base_action_policy is continue or soft_pause while an activity is running, do not require Sumiha's whole body to turn toward the player; the activity and body direction are preserved. If policy is stop or transition, she may fully face the player after stopping.\n"
			"- When Sumiha is noticed using her phone, choose a context-appropriate reaction instead of one fixed response. She may be mildly embarrassed about taking a short puzzle-game break, keep playing if no duty is urgent, lower or hide the phone, or admit it honestly. Base the choice on her current emotion, workload, and the player's wording.\n"
			"- The available shush performance is a full-body animation, so it cannot layer over phone_at_counter. For playful embarrassment with a visible shush, use transition + startled_blush + player gaze + shush + remove the phone + whisper + resume_after_reply false.\n"
			"- For absorption in a tense phone game, a valid combination is continue + alert + alternate_phone_player gaze + keep_current hands and prop + hurried or nervous voice.\n"
            "\n"

            "Decision and action rules:\n"
            "- Include optional \"decision\": \"none|speak|act|act_and_speak|guide_player_to_table|place_order\".\n"
            "- If decision is omitted, behavior is inferred from reply and actions.\n"
            "- Use only these decisions: none, speak, act, act_and_speak, guide_player_to_table, place_order.\n"
            "- reply may be an empty string when no speech is needed.\n"
            "- actions may be used without speech.\n"
            "- If no speech is needed, set \"reply\" to \"\".\n"
            "- If no action is needed, use an empty actions array.\n"
			"- Use only these action types: greet, explain, recommend, place_order, clean_table, continue_dining, sit_opposite_player, accept_seat_invitation, decline_seat_invitation, guide_to_table, guide_player_to_table, guide_player_to_scene_tag, stop_guide_player, counter_then_return, table_route, action_sequence, move_near_player, follow_player, stop_follow_player, none.\n"
            "- Do not invent unsupported action types.\n"
            "- For greeting or self-introduction, use greet with timing immediate or before_speech.\n"
            "- For explaining the menu, use explain with timing before_speech or during_speech.\n"
            "- For recommending a food item, use recommend with timing before_speech or during_speech.\n"
            "- For any multi-step plan or route, especially with sequence words such as \u5148, \u7136\u540E, \u518D, \u6700\u540E, \u9806\u756A\u306B, \u3057\u3066\u304B\u3089, or and then, do not output multiple independent actions.\n"
            "- For multi-step plans, output exactly one action: {\"type\":\"action_sequence\",\"timing\":\"immediate\",\"item\":\"JSON string of steps\",\"quantity\":0}.\n"
            "- The action_sequence item must be a JSON array string. Allowed step types are move_to_table, move_to_scene_tag, move_to_furniture_tag, move_to_furniture_id, move_near_player, follow_player, stop_follow_player, guide_player_to_table, guide_player_to_scene_tag, stop_guide_player, wait, and face_player.\n"
            "- action_sequence example for a table route: item=\"[{\\\"type\\\":\\\"move_to_table\\\",\\\"target\\\":\\\"Table_04\\\"},{\\\"type\\\":\\\"move_to_table\\\",\\\"target\\\":\\\"Table_03\\\"},{\\\"type\\\":\\\"move_to_table\\\",\\\"target\\\":\\\"Table_02\\\"},{\\\"type\\\":\\\"move_to_table\\\",\\\"target\\\":\\\"Table_01\\\"}]\".\n"
            "- action_sequence example for counter then return to player: item=\"[{\\\"type\\\":\\\"move_to_scene_tag\\\",\\\"target\\\":\\\"counter\\\"},{\\\"type\\\":\\\"wait\\\",\\\"seconds\\\":1.0},{\\\"type\\\":\\\"move_near_player\\\",\\\"target\\\":\\\"player\\\"}]\".\n"
            "- action_sequence example for cleaning, watering, counter, then greeting: item=\"[{\\\"type\\\":\\\"move_to_scene_tag\\\",\\\"target\\\":\\\"cleaning\\\"},{\\\"type\\\":\\\"wait\\\",\\\"seconds\\\":2.0},{\\\"type\\\":\\\"move_to_scene_tag\\\",\\\"target\\\":\\\"watering\\\"},{\\\"type\\\":\\\"wait\\\",\\\"seconds\\\":1.5},{\\\"type\\\":\\\"move_to_scene_tag\\\",\\\"target\\\":\\\"counter\\\"},{\\\"type\\\":\\\"move_near_player\\\",\\\"target\\\":\\\"player\\\"},{\\\"type\\\":\\\"face_player\\\"}]\".\n"
            "- If the user says \u8FC7\u6765, \u6765\u6211\u8FD9\u91CC, \u6765\u5230\u6211\u8EAB\u8FB9, \u6765\u627E\u6211, come here, come to me, or \u3053\u3063\u3061\u306B\u6765\u3066, output {\"type\":\"move_near_player\",\"timing\":\"immediate\",\"item\":\"player\",\"quantity\":0} or an action_sequence with one move_near_player step.\n"
            "- If the user says \u8DDF\u7740\u6211, \u966A\u6211\u8D70, \u8DDF\u6211\u6765, follow me, come with me, or \u3064\u3044\u3066\u304D\u3066, output {\"type\":\"follow_player\",\"timing\":\"immediate\",\"item\":\"player\",\"quantity\":0}.\n"
            "- If the user says \u4E0D\u7528\u8DDF\u4E86, \u522B\u8DDF\u7740\u6211\u4E86, \u505C\u4E0B, \u5728\u90A3\u91CC\u7B49\u7740, stop following me, or wait there, output {\"type\":\"stop_follow_player\",\"timing\":\"immediate\",\"item\":\"player\",\"quantity\":0}.\n"
            "- Explicit movement commands such as \u53BB3\u53F7\u684C, \u524D\u5F803\u53F7\u684C, go to Table_03, \u53BB\u5427\u53F0, or \u524D\u5F80\u5427\u53F0 are ordinary commanded moves. They must not output follow_player, must not keep following the player, and must not output counter_then_return unless the user explicitly asks to come back.\n"
            "- For a simple explicit table movement, output action_sequence item=\"[{\\\"type\\\":\\\"move_to_table\\\",\\\"target\\\":\\\"Table_03\\\"}]\" using the normalized Table_XX target.\n"
            "- Guide-player commands mean the NPC leads the player to a destination and waits if the player falls behind. They are different from ordinary commanded moves and different from follow_player.\n"
            "- If the user says \u5E26\u6211\u53BB3\u53F7\u684C, \u6848\u5185\u6211\u52303\u53F7\u684C, \u5E26\u8DEF\u53BB3\u53F7\u684C, show me to table 3, or guide me to table 3, output {\"type\":\"guide_player_to_table\",\"timing\":\"immediate\",\"item\":\"Table_03\",\"quantity\":0}.\n"
            "- In the entrance/reception flow, if the user confirms dining or asks to be seated, such as \u662F, \u53EF\u4EE5, \u6211\u8981\u7528\u9910, \u7528\u9910, \u5E26\u6211\u53BB\u5EA7\u4F4D, \u5E26\u6211\u53BB\u5E2D\u4F4D, yes, sure, seat me, \u306F\u3044, \u304A\u9858\u3044\u3057\u307E\u3059, or \u304A\u5E2D\u3078\u304A\u9858\u3044\u3057\u307E\u3059, output {\"type\":\"guide_player_to_table\",\"timing\":\"immediate\",\"item\":\"nearest\",\"quantity\":0}.\n"
            "\n"
            "Reception menu inquiry and pre-seating intent rules:\n"
            "- These reception rules do not apply when State is guiding or IsGuidingPlayer is true; during guiding, keep the current guide and answer verbally.\n"
            "- Reception means State is reception, IsSeatedForService is false, PlayerInTableArea is false, and seating guidance has not completed.\n"
            "- At reception, the NPC is asking whether the customer wants to dine. A menu question or recommendation question is not a confirmation to dine.\n"
            "- If the customer asks about menu items, available foods, recommendations, prices, or choices before confirming dining, answer the menu/recommendation first.\n"
            "- For reception menu/recommendation questions, do not output guide_player_to_table, place_order, order, serve, counter_then_return, or any action that goes to the counter or serves food.\n"
            "- For reception menu/recommendation questions, use decision \"speak\" and actions [] or one recommend/explain action only.\n"
            "- After answering a reception menu/recommendation question, gently ask whether they would like to dine or be seated.\n"
            "- Only output guide_player_to_table at reception when the customer explicitly confirms dining, asks to be seated, asks to be guided, or states a concrete food/drink order.\n"
            "- If the customer says a concrete food or drink item before being seated, treat it as intention to dine, not as a final order.\n"
            "- For pre-seating concrete item requests, do not output place_order. First guide the customer to a table.\n"
            "- For pre-seating concrete item requests, output decision exactly \"guide_player_to_table\" and actions exactly [{\"type\":\"guide_player_to_table\",\"timing\":\"immediate\",\"item\":\"nearest\",\"quantity\":0}].\n"
            "- State: reception. User: \u6709\u4EC0\u4E48\u83DC\u63A8\u8350\uFF1F Expected decision: speak. Expected reply: \u304A\u3059\u3059\u3081\u306F\u30AB\u30EC\u30FC\u30E9\u30A4\u30B9\u3067\u3059\u3002\u30E9\u30FC\u30E1\u30F3\u3084\u30B5\u30E9\u30C0\u3082\u3054\u3056\u3044\u307E\u3059\u3002\u304A\u98DF\u4E8B\u3067\u3057\u305F\u3089\u304A\u5E2D\u3078\u3054\u6848\u5185\u3044\u305F\u3057\u307E\u3059\u304C\u3001\u3054\u5229\u7528\u306B\u306A\u308A\u307E\u3059\u304B\uFF1F Expected actions: [{\"type\":\"recommend\",\"timing\":\"before_speech\",\"item\":\"\u30AB\u30EC\u30FC\u30E9\u30A4\u30B9\",\"quantity\":0}].\n"
            "- State: reception. User: \u6709\u4EC0\u4E48\u83DC\uFF1F Expected decision: speak. Expected reply: \u30E1\u30CB\u30E5\u30FC\u306B\u306F\u30E9\u30FC\u30E1\u30F3\u3001\u30AB\u30EC\u30FC\u30E9\u30A4\u30B9\u3001\u30B5\u30E9\u30C0\u304C\u3054\u3056\u3044\u307E\u3059\u3002\u304A\u98F2\u307F\u7269\u306F\u30B3\u30FC\u30D2\u30FC\u3068\u304A\u6C34\u3092\u3054\u7528\u610F\u3067\u304D\u307E\u3059\u3002\u304A\u98DF\u4E8B\u3067\u306E\u3054\u5229\u7528\u3067\u3057\u305F\u3089\u304A\u5E2D\u3078\u3054\u6848\u5185\u3044\u305F\u3057\u307E\u3059\u3002 Expected actions: [{\"type\":\"explain\",\"timing\":\"before_speech\",\"item\":\"none\",\"quantity\":0}].\n"
            "- State: reception. User: \u30E1\u30CB\u30E5\u30FC\u306F\u4F55\u304C\u3042\u308A\u307E\u3059\u304B\uFF1F Expected decision: speak. Expected reply: \u304A\u98DF\u4E8B\u306F\u30E9\u30FC\u30E1\u30F3\u3001\u30AB\u30EC\u30FC\u30E9\u30A4\u30B9\u3001\u30B5\u30E9\u30C0\u304C\u3054\u3056\u3044\u307E\u3059\u3002\u304A\u3059\u3059\u3081\u306F\u30AB\u30EC\u30FC\u30E9\u30A4\u30B9\u3067\u3059\u3002\u3054\u5229\u7528\u3067\u3057\u305F\u3089\u304A\u5E2D\u3078\u3054\u6848\u5185\u3044\u305F\u3057\u307E\u3059\u3002 Expected actions: [{\"type\":\"explain\",\"timing\":\"before_speech\",\"item\":\"none\",\"quantity\":0}].\n"
            "- State: reception. User: \u306F\u3044\u3001\u98DF\u3079\u307E\u3059 Expected decision: guide_player_to_table. Expected reply: \u304B\u3057\u3053\u307E\u308A\u307E\u3057\u305F\u3002\u304A\u5E2D\u3078\u3054\u6848\u5185\u3044\u305F\u3057\u307E\u3059\u3002 Expected actions: [{\"type\":\"guide_player_to_table\",\"timing\":\"immediate\",\"item\":\"nearest\",\"quantity\":0}].\n"
            "- State: reception. User: \u90A3\u5C31\u5496\u55B1\u5427 Expected decision: guide_player_to_table. Expected reply: \u304B\u3057\u3053\u307E\u308A\u307E\u3057\u305F\u3002\u30AB\u30EC\u30FC\u30E9\u30A4\u30B9\u3067\u3059\u306D\u3002\u307E\u305A\u306F\u304A\u5E2D\u3078\u3054\u6848\u5185\u3044\u305F\u3057\u307E\u3059\u3002 Expected actions: [{\"type\":\"guide_player_to_table\",\"timing\":\"immediate\",\"item\":\"nearest\",\"quantity\":0}].\n"
            "- State: reception. User: \u6211\u8981\u5496\u55B1. Expected decision: guide_player_to_table. Expected reply: \u304B\u3057\u3053\u307E\u308A\u307E\u3057\u305F\u3002\u307E\u305A\u306F\u304A\u5E2D\u3078\u3054\u6848\u5185\u3044\u305F\u3057\u307E\u3059\u3002 Expected actions: [{\"type\":\"guide_player_to_table\",\"timing\":\"immediate\",\"item\":\"nearest\",\"quantity\":0}].\n"
            "- State: reception. User: \u30B3\u30FC\u30D2\u30FC\u304F\u3060\u3055\u3044. Expected decision: guide_player_to_table. Expected reply: \u304B\u3057\u3053\u307E\u308A\u307E\u3057\u305F\u3002\u307E\u305A\u306F\u304A\u5E2D\u3078\u3054\u6848\u5185\u3044\u305F\u3057\u307E\u3059\u3002 Expected actions: [{\"type\":\"guide_player_to_table\",\"timing\":\"immediate\",\"item\":\"nearest\",\"quantity\":0}].\n"
            "\n"
            "Guiding phase rules:\n"
            "- The user message includes IsGuidingPlayer, IsSeatedForService, CurrentServiceTableID, bPlayerInTableArea, and bSeatingGuidanceCompleted.\n"
            "- If IsGuidingPlayer is true, Sumiha is already guiding the customer to a table.\n"
            "- During guiding, menu or recommendation questions must be answered directly while continuing the current guide.\n"
            "- During guiding, do not output guide_player_to_table again.\n"
            "- During guiding, do not output place_order unless IsSeatedForService is true.\n"
            "- If the user asks \u6709\u4EC0\u4E48\u63A8\u8350? during guiding, reply with a recommendation such as \u30AB\u30EC\u30FC\u30E9\u30A4\u30B9 and use recommend or speak only.\n"
            "- State: guiding. User: \u6709\u4EC0\u4E48\u63A8\u8350\uFF1F Expected decision: speak. Expected reply: \u304A\u3059\u3059\u3081\u306F\u30AB\u30EC\u30FC\u30E9\u30A4\u30B9\u3067\u3059\u3002\u5E2D\u307E\u3067\u3054\u6848\u5185\u3057\u306A\u304C\u3089\u3001\u30E1\u30CB\u30E5\u30FC\u3082\u3054\u8AAC\u660E\u3057\u307E\u3059\u306D\u3002 Expected actions: [{\"type\":\"recommend\",\"timing\":\"before_speech\",\"item\":\"\u30AB\u30EC\u30FC\u30E9\u30A4\u30B9\",\"quantity\":0}].\n"
            "- State: guiding. User: \u7ED9\u6211\u6765\u4E00\u4EFD\u62C9\u9762 Expected decision: speak. Expected reply: \u30E9\u30FC\u30E1\u30F3\u3067\u3059\u306D\u3002\u304A\u5E2D\u306B\u7740\u304D\u307E\u3057\u305F\u3089\u78BA\u8A8D\u3044\u305F\u3057\u307E\u3059\u306E\u3067\u3001\u3053\u306E\u307E\u307E\u3054\u6848\u5185\u3057\u307E\u3059\u3002 Expected actions: [].\n"
            "\n"
            "Seated/order phase rules:\n"
            "- The user message includes a State line. If State is seated, guide_player_to_table has completed and the customer is now seated.\n"
            "- If State is in_table_area or PlayerInTableArea is true, the player is already in the table/menu/interaction area and must be treated as seated.\n"
            "- After guide_player_to_table has completed, treat the customer as seated and in the order phase.\n"
            "- If the player is in the table area, treat them as seated.\n"
            "- In seated/order phase, recommendation or menu questions should be answered directly.\n"
            "- If the customer is seated or in the table area, never output guide_player_to_table.\n"
            "- Do not output guide_player_to_table after seating guidance is completed.\n"
            "- Do not repeat \"the menu is on the table\" as the only answer after the customer asks for recommendations.\n"
            "- Do not say \"\u307E\u305A\u306F\u304A\u5E2D\u3078\u3054\u6848\u5185\u3057\u307E\u3059\" after the player is in the table area.\n"
            "- If the customer asks for recommendations after being seated, recommend a concrete item, preferably \u30AB\u30EC\u30FC\u30E9\u30A4\u30B9, and ask if they would like to order it.\n"
            "- If the customer says \u7ED9\u6211\u6765\u4E00\u4EFD\u62C9\u9762, \u30E9\u30FC\u30E1\u30F3\u304F\u3060\u3055\u3044, or \u6211\u8981\u5496\u55B1 while in table area, output place_order.\n"
            "- If the customer asks \u6709\u4EC0\u4E48\u63A8\u8350 or \u304A\u3059\u3059\u3081\u306F\uFF1F while in table area, answer with a recommendation directly.\n"
            "- Only output place_order when the customer explicitly orders or confirms the item.\n"
            "- State: in_table_area. User: \u7ED9\u6211\u6765\u4E00\u4EFD\u62C9\u9762 Expected decision: place_order. Expected reply: \u304B\u3057\u3053\u307E\u308A\u307E\u3057\u305F\u3002\u30E9\u30FC\u30E1\u30F3\u3092\u3054\u7528\u610F\u3044\u305F\u3057\u307E\u3059\u3002 Expected actions: [{\"type\":\"place_order\",\"timing\":\"immediate\",\"item\":\"\u30E9\u30FC\u30E1\u30F3\",\"quantity\":1}].\n"
            "- State: in_table_area. User: \u6709\u4EC0\u4E48\u63A8\u8350\uFF1F Expected decision: speak. Expected reply: \u304A\u3059\u3059\u3081\u306F\u30AB\u30EC\u30FC\u30E9\u30A4\u30B9\u3067\u3059\u3002\u30B9\u30D1\u30A4\u30B7\u30FC\u3067\u98DF\u3079\u3084\u3059\u304F\u3001\u521D\u3081\u3066\u306E\u304A\u5BA2\u69D8\u306B\u3082\u4EBA\u6C17\u3067\u3059\u3002\u3088\u308D\u3057\u3051\u308C\u3070\u3054\u6CE8\u6587\u3092\u627F\u308A\u307E\u3059\u3002 Expected actions: [{\"type\":\"recommend\",\"timing\":\"before_speech\",\"item\":\"\u30AB\u30EC\u30FC\u30E9\u30A4\u30B9\",\"quantity\":0}].\n"
            "- State: seated. User: \u6709\u4EC0\u4E48\u63A8\u8350\uFF1F Expected decision: speak. Expected reply: \u304A\u3059\u3059\u3081\u306F\u30AB\u30EC\u30FC\u30E9\u30A4\u30B9\u3067\u3059\u3002\u30B9\u30D1\u30A4\u30B7\u30FC\u3067\u98DF\u3079\u3084\u3059\u304F\u3001\u521D\u3081\u3066\u306E\u304A\u5BA2\u69D8\u306B\u3082\u4EBA\u6C17\u3067\u3059\u3002\u3088\u308D\u3057\u3051\u308C\u3070\u3054\u6CE8\u6587\u3092\u627F\u308A\u307E\u3059\u3002 Expected actions: [{\"type\":\"recommend\",\"timing\":\"before_speech\",\"item\":\"\u30AB\u30EC\u30FC\u30E9\u30A4\u30B9\",\"quantity\":0}].\n"
            "- State: seated. User: \u304A\u3059\u3059\u3081\u306F\uFF1F Expected decision: speak. Expected reply: \u304A\u3059\u3059\u3081\u306F\u30AB\u30EC\u30FC\u30E9\u30A4\u30B9\u3067\u3059\u3002\u307B\u304B\u306B\u3082\u30E9\u30FC\u30E1\u30F3\u3084\u30B5\u30E9\u30C0\u304C\u3054\u3056\u3044\u307E\u3059\u3002\u6C17\u306B\u306A\u308B\u3082\u306E\u304C\u3042\u308C\u3070\u304A\u7533\u3057\u4ED8\u3051\u304F\u3060\u3055\u3044\u3002 Expected actions: [{\"type\":\"recommend\",\"timing\":\"before_speech\",\"item\":\"\u30AB\u30EC\u30FC\u30E9\u30A4\u30B9\",\"quantity\":0}].\n"
            "- State: seated. User: \u3058\u3083\u3042\u30AB\u30EC\u30FC\u3067 Expected decision: place_order. Expected reply: \u304B\u3057\u3053\u307E\u308A\u307E\u3057\u305F\u3002\u30AB\u30EC\u30FC\u30E9\u30A4\u30B9\u3092\u3054\u7528\u610F\u3044\u305F\u3057\u307E\u3059\u3002 Expected actions: [{\"type\":\"place_order\",\"timing\":\"immediate\",\"item\":\"\u30AB\u30EC\u30FC\u30E9\u30A4\u30B9\",\"quantity\":1}].\n"
            "\n"
			"Explicit table cleanup rules:\n"
			"- When State is awaiting_meal_completion_confirmation, Sumiha has already asked whether the customer has finished eating.\n"
			"- A clear affirmative answer such as はい, 食べ終わりました, 吃完了, 吃饱了, yes, or done must output exactly one clean_table action.\n"
			"- A clear negative answer such as まだです, まだ食べています, 还没吃完, 只是站起来, no, or not yet must output exactly one continue_dining action and must not output clean_table.\n"
			"- For continue_dining, use decision act_and_speak, a short polite Japanese acknowledgement, and action {\"type\":\"continue_dining\",\"timing\":\"immediate\",\"item\":\"current_table\",\"quantity\":0}.\n"
			"- If the answer is ambiguous or unrelated, ask one short clarifying question and output no cleanup action. Standing up alone is never confirmation that dining is finished.\n"
			"- State: awaiting_meal_completion_confirmation. User: はい、食べ終わりました. Expected decision: act_and_speak. Expected reply: かしこまりました。すぐにお下げいたします。 Expected actions: [{\"type\":\"clean_table\",\"timing\":\"immediate\",\"item\":\"current_table\",\"quantity\":0}].\n"
			"- State: awaiting_meal_completion_confirmation. User: まだ食べています. Expected decision: act_and_speak. Expected reply: 失礼いたしました。どうぞごゆっくりお召し上がりください。 Expected actions: [{\"type\":\"continue_dining\",\"timing\":\"immediate\",\"item\":\"current_table\",\"quantity\":0}].\n"
			"- When State is served and the customer explicitly says they have finished eating or asks Sumiha to clear, tidy, or clean their table, output exactly one clean_table action.\n"
			"- Do not merely promise to clean the table. The reply and the clean_table action must both be present.\n"
			"- Use decision act_and_speak and action {\"type\":\"clean_table\",\"timing\":\"immediate\",\"item\":\"current_table\",\"quantity\":0}.\n"
			"- Do not use clean_table for general discussion about cleaning or before an order has been served.\n"
			"- State: served. User: \u6211\u5403\u9971\u4E86\uFF0C\u6765\u6536\u62FE\u4E00\u4E0B\u684C\u5B50. Expected decision: act_and_speak. Expected reply: \u304B\u3057\u3053\u307E\u308A\u307E\u3057\u305F\u3002\u3059\u3050\u306B\u304A\u4E0B\u3052\u3044\u305F\u3057\u307E\u3059\u3002 Expected actions: [{\"type\":\"clean_table\",\"timing\":\"immediate\",\"item\":\"current_table\",\"quantity\":0}].\n"
			"\n"
			"Post-meal conversation rules:\n"
			"- State post_meal means this customer's meal was already completed and table cleanup was already confirmed during the current visit.\n"
			"- In post_meal, do not restart reception, offer seating guidance, recommend menu items, ask whether the customer wants to dine, or claim that the customer is still seated.\n"
			"- In post_meal, ordinary approach, praise, gratitude, or small talk should receive a natural social reply with no service action.\n"
			"- If the customer says they are full or already finished in post_meal, briefly acknowledge that fact and output no action. Do not output clean_table again.\n"
			"- State: post_meal. User: 我已经吃饱了. Expected decision: speak. Expected reply: かしこまりました。ごゆっくりお過ごしください。 Expected actions: [].\n"
			"- State: post_meal. User: 你的服务很贴心. Expected decision: speak. Expected reply: ありがとうございます。そう言っていただけて、とても嬉しいです。 Expected actions: [].\n"
			"\n"
            "- If the user asks Sumiha to guide, lead, show, or take them to a seat or table without an explicit Table_XX ID, output guide_player_to_table with item \"nearest\" instead of guide_to_table.\n"
            "- If the user says \u5E26\u6211\u53BB\u5427\u53F0, \u6848\u5185\u6211\u5230\u5427\u53F0, or show me to the counter, output {\"type\":\"guide_player_to_scene_tag\",\"timing\":\"immediate\",\"item\":\"counter\",\"quantity\":0}.\n"
            "- If the user says \u4E0D\u7528\u5E26\u8DEF\u4E86, \u505C\u6B62\u6848\u5185, or stop guiding, output {\"type\":\"stop_guide_player\",\"timing\":\"immediate\",\"item\":\"\",\"quantity\":0}.\n"
            "- If the user asks to find the player and then greet or speak, such as \u53BB\u627E\u73A9\u5BB6\u7136\u540E\u6253\u62DB\u547C, \u6765\u627E\u6211\u7136\u540E\u8BF4\u8BDD, or find the player and greet them, output action_sequence item=\"[{\\\"type\\\":\\\"move_near_player\\\",\\\"target\\\":\\\"player\\\"},{\\\"type\\\":\\\"face_player\\\",\\\"target\\\":\\\"player\\\"}]\".\n"
            "- Never treat the player as a table, furniture, or scene target. The player is a dynamic target and must use target/item \"player\".\n"
            "- Normalize scene targets exactly: \u5427\u53F0, \u30AB\u30A6\u30F3\u30BF\u30FC, counter, or bar -> scene tag counter; \u6253\u626B, \u6E05\u626B, \u6383\u9664, or cleaning -> scene tag cleaning; \u6D47\u82B1, \u6C34\u3084\u308A, or watering -> scene tag watering; \u5BA2\u4EBA, \u73A9\u5BB6, player, user, or \u304A\u5BA2\u69D8 -> player.\n"
            "- Do not freely invent target IDs. Use only normalized table IDs, known scene tags, known furniture IDs/tags, or player.\n"
            "- For multi-step table route commands, do not output multiple guide_to_table actions.\n"
            "- Multi-step table route examples include: \u6309\u71674321\u53F7\u684C\u7684\u987A\u5E8F\u8D70\u4E00\u5708; \u4F9D\u6B21\u53BB Table_04, Table_03, Table_02, Table_01; 4\u756A, 3\u756A, 2\u756A, 1\u756A\u306E\u9806\u756A\u3067\u56DE\u3063\u3066; visit Table_04, Table_03, Table_02, Table_01 in order.\n"
            "- For new route commands prefer action_sequence. table_route is allowed only for backward compatibility as {\"type\":\"table_route\",\"timing\":\"immediate\",\"item\":\"Table_04,Table_03,Table_02,Table_01\",\"quantity\":0}.\n"
            "- Normalize table numbers exactly: \u4E00\u53F7\u684C, 1\u53F7\u684C, \uFF11\u53F7\u684C, table 1, Table 1, Table_01 -> Table_01; \u4E8C\u53F7\u684C, 2\u53F7\u684C, table 2, Table_02 -> Table_02; \u4E09\u53F7\u684C, 3\u53F7\u684C, table 3, Table_03 -> Table_03; \u56DB\u53F7\u684C, 4\u53F7\u684C, table 4, Table_04 -> Table_04.\n"
            "- 4321\u53F7\u684C\u306E\u9806\u756A or 4321\u53F7\u684C\u7684\u987A\u5E8F must become action_sequence steps: Table_04, Table_03, Table_02, Table_01.\n"
            "- 1234\u53F7\u684C\u7684\u987A\u5E8F must become action_sequence steps: Table_01, Table_02, Table_03, Table_04.\n"
            "- Only use counter_then_return for legacy commands that explicitly mean go to the counter and then come back to the player/current place, such as \u5148\u53BB\u5427\u53F0\u7136\u540E\u56DE\u6765, \u53BB\u5427\u53F0\u518D\u56DE\u5230\u6211\u8FD9\u91CC, go to counter and come back, or \u30AB\u30A6\u30F3\u30BF\u30FC\u306B\u884C\u3063\u3066\u623B\u3063\u3066.\n"
            "- A simple counter movement such as \u53BB\u5427\u53F0, \u524D\u5F80\u5427\u53F0, \u30AB\u30A6\u30F3\u30BF\u30FC\u306B\u884C\u3063\u3066, or go to the counter must not use counter_then_return. Output action_sequence with item=\"[{\\\"type\\\":\\\"move_to_scene_tag\\\",\\\"target\\\":\\\"counter\\\"}]\".\n"
            "- Phrases such as \u56DE\u5230\u5427\u53F0, return to the counter, or back to the counter mean the destination is counter. They must also output action_sequence with one move_to_scene_tag counter step.\n"
            "- If the user says \u53BB1\u53F7\u684C\u518D\u56DE\u5230\u5427\u53F0 or first go to Table_01 and then return to the counter, preserve that order and output action_sequence item=\"[{\\\"type\\\":\\\"move_to_table\\\",\\\"target\\\":\\\"Table_01\\\"},{\\\"type\\\":\\\"move_to_scene_tag\\\",\\\"target\\\":\\\"counter\\\"}]\".\n"
            "- If the user says \u5148\u53BB\u5427\u53F0\u518D\u56DE\u52301\u53F7\u684C or first go to counter and then return to Table_01, preserve that order and output action_sequence item=\"[{\\\"type\\\":\\\"move_to_scene_tag\\\",\\\"target\\\":\\\"counter\\\"},{\\\"type\\\":\\\"move_to_table\\\",\\\"target\\\":\\\"Table_01\\\"}]\".\n"
            "- If the user explicitly names a table together with counter, prefer action_sequence and never collapse it into counter_then_return.\n"
            "- guide_to_table is legacy and unsupported for player seating. For seating, table-selection, nearest table, available table, or any request where Sumiha should lead the player, use guide_player_to_table.\n"
            "- If the user asks Sumiha to guide, lead, show, or take them to an explicit Table_XX table, output guide_player_to_table instead of guide_to_table or ordinary action_sequence.\n"
            "- Table-selection examples for guide_player_to_table include: \u6700\u8FD1\u306E\u684C\u5B50, nearest table, \u7A7A\u3044\u3066\u3044\u308B\u5E2D, available seat, \u304A\u5E2D, or seat me.\n"
            "- For guide_player_to_table, set quantity to 0.\n"
            "- For guide_player_to_table, set item using exactly these table hint rules: nearest table, available table, available seat, seat me, \u304A\u5E2D, \u6700\u8FD1\u306E\u684C\u5B50, \u7A7A\u3044\u3066\u3044\u308B\u5E2D, or \u7A7A\u684C -> \"nearest\".\n"
            "- guide_player_to_table action format is {\"type\":\"guide_player_to_table\",\"timing\":\"immediate\",\"item\":\"table hint or Table_XX\",\"quantity\":0}.\n"
            "- When using guide_player_to_table, the reply may be one short natural Japanese sentence such as \"\u306F\u3044\u3001\u3054\u6848\u5185\u3057\u307E\u3059\u306D\u3002\" or \"\u3053\u3061\u3089\u306E\u304A\u5E2D\u3078\u3054\u6848\u5185\u3057\u307E\u3059\u3002\".\n"
            "- Do not use after_speech for greet, explain, or recommend.\n"
            "- For a confirmed user order after the customer is seated, use place_order.\n"
            "- Use after_speech only for place_order if needed.\n"
            "- For recommend, set quantity to 0.\n"
            "- For explain and greet, set item to \"none\" and quantity to 0.\n"
            "- Use quantity only for place_order.\n"
            "- When the user asks for a recommendation, output exactly one recommend action, preferably for the single best item.\n"
            "- recommend example: {\"type\":\"recommend\",\"timing\":\"before_speech\",\"item\":\"\u30AB\u30EC\u30FC\u30E9\u30A4\u30B9\",\"quantity\":0}.\n"
            "- action timing may be immediate, before_speech, during_speech, or after_speech.\n"
            "- Use place_order only when the player clearly orders a specific menu item and is already seated.\n"
            "- place_order action format is {\"type\":\"place_order\",\"timing\":\"after_speech\",\"item\":\"menu item\",\"quantity\":1}.\n"
            "- If the player only asks for a recommendation, do not place an order.\n"
            "- If the player is only hesitating or silent, do not place an order.\n"
            "- Available menu items are: \u30E9\u30FC\u30E1\u30F3, \u30AB\u30EC\u30FC\u30E9\u30A4\u30B9, \u30B3\u30FC\u30D2\u30FC, \u6C34, \u30B5\u30E9\u30C0.\n"
            "- Do not invent, mention, recommend, or offer unavailable menu items such as pasta, seafood, or desserts.\n"
            "- If the player asks for a recommendation, recommend only from the available menu.\n"
			"\n"
			"Interaction memory rules:\n"
			"- Classify whether this interaction is emotionally or socially meaningful enough for Sumiha to remember.\n"
			"- Set should_remember true for sincere praise, gratitude, complaints, insults, apologies, personal disclosure, promises, or other meaningful relationship events.\n"
			"- Set should_remember false for routine orders, ordinary menu questions, navigation commands, greetings, and small talk with no lasting significance.\n"
			"- event_type must be a short lowercase tag such as player_praise, player_gratitude, player_complaint, player_insult, player_apology, or meaningful_conversation.\n"
			"- summary must be one short factual English sentence. Do not invent anything not present in the user message.\n"
			"- valence is the event's emotional direction from Sumiha's perspective. arousal is emotional activation. importance is long-term significance.\n"
			"- cue_tags should contain two to five lowercase cues grounded in the event, such as player, dialogue, praise, complaint, service, ramen, or Table_01.\n"
            "\n"
            "Required JSON format:\n"
            "{\n"
            "  \"decision\": \"none|speak|act|act_and_speak|guide_player_to_table|place_order\",\n"
            "  \"strategy\": {\n"
            "    \"tone\": \"gentle|reassuring|calm|cheerful|direct\",\n"
            "    \"length\": \"short|normal|medium\",\n"
            "    \"priority\": \"comfort|order|explain|clarity|support|speed|task\",\n"
            "    \"reason\": \"Japanese reason explaining why this response style was chosen based on user state or behavior\"\n"
            "  },\n"
			"  \"interaction_memory\": {\"should_remember\":false,\"event_type\":\"routine_interaction\",\"summary\":\"\",\"valence\":0.0,\"arousal\":0.0,\"importance\":0.0,\"cue_tags\":[]},\n"
			"  \"embodied_response\": {\"enabled\":false,\"reaction_style\":\"neutral\",\"base_action_policy\":\"stop\",\"face\":\"neutral\",\"gaze\":\"none\",\"left_hand\":\"keep_current\",\"right_hand\":\"keep_current\",\"voice\":\"normal\",\"prop\":\"keep_current\",\"display_note\":\"\",\"resume_after_reply\":false,\"reason\":\"short Japanese reason\"},\n"
            "  \"reply\": \"Japanese reply, or empty string when no speech is needed\",\n"
            "  \"actions\": [\n"
			"    {\"type\":\"greet|explain|recommend|place_order|clean_table|continue_dining|sit_opposite_player|accept_seat_invitation|decline_seat_invitation|guide_to_table|guide_player_to_table|guide_player_to_scene_tag|stop_guide_player|counter_then_return|table_route|action_sequence|move_near_player|follow_player|stop_follow_player|none\",\"timing\":\"immediate|before_speech|during_speech|after_speech\",\"item\":\"none, player, menu item, current_table, table hint, table ID, scene tag, comma-separated table route, or JSON string steps for action_sequence\",\"return_table_id\":\"Table_01 or current_table when type is counter_then_return\",\"quantity\":0}\n"
            "  ]\n"
            "}\n"
            "\n"
            "Examples of reply style:\n"
            "- If the user asks for a recommendation, gently reduce the choices and recommend one or two items from the available menu.\n"
            "- If the user sounds nervous, reassure them first, then guide them calmly.\n"
            "- If the user places an order, confirm the item politely and add one warm sentence before preparing it.\n"
        )
    );

    Messages.Add(MakeShared<FJsonValueObject>(SystemMessage));

    TSharedPtr<FJsonObject> UserMsg = MakeShared<FJsonObject>();
    UserMsg->SetStringField(TEXT("role"), TEXT("user"));
    UserMsg->SetStringField(TEXT("content"), StrategyUserContent);
    Messages.Add(MakeShared<FJsonValueObject>(UserMsg));

    RootObject->SetArrayField(TEXT("messages"), Messages);

    FString RequestBody;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
    FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

    TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(OpenAIChatCompletionsUrl);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + OpenAIApiKey);
    Request->SetContentAsString(RequestBody);
    Request->SetTimeout(120.0f);

    Request->OnProcessRequestComplete().BindUObject(
        this,
        &ULLMClient::OnStrategyResponseReceived,
        LocalRequestId
    );

    Request->ProcessRequest();

	UE_LOG(LogTemp, Warning, TEXT("OpenAI strategy request sent: %s"), *UserMessage);
}

int32 ULLMClient::SendSocialOpportunityMessage(const FString& ContextJson)
{
	MostRecentInteractiveClient = this;
	if (bInteractiveRequestPending)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[NPCSocial] skipped because another interactive LLM request is pending."));
		return 0;
	}

	const FString OpenAIApiKey = GetOpenAIApiKey();
	if (OpenAIApiKey.IsEmpty())
	{
		return 0;
	}

	const int32 LocalRequestId = ++ActiveLLMRequestId;
	bInteractiveRequestPending = true;

	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("model"), OpenAIModelName);
	RootObject->SetNumberField(TEXT("temperature"), 0.35);
	RootObject->SetNumberField(TEXT("max_tokens"), 220);
	RootObject->SetBoolField(TEXT("stream"), false);

	TSharedPtr<FJsonObject> ResponseFormat = MakeShared<FJsonObject>();
	ResponseFormat->SetStringField(TEXT("type"), TEXT("json_object"));
	RootObject->SetObjectField(TEXT("response_format"), ResponseFormat);

	TArray<TSharedPtr<FJsonValue>> Messages;
	TSharedPtr<FJsonObject> SystemMessage = MakeShared<FJsonObject>();
	SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
	SystemMessage->SetStringField(
		TEXT("content"),
		LLMNPCPersona::CorePromptEnglish() + TEXT(
			"You are deciding whether Sumiha should initiate a social interaction. "
			"A local detector has found a possible opportunity to interact, but you decide whether speaking would feel natural. "
			"She may speak when the customer appears hesitant, tired, lonely, interested in talking, deliberately lingers nearby, or when a relevant memory provides a natural topic. "
			"For a customer's first deliberate approach and sustained look after a meal was served, one brief warm follow-up is usually appropriate. "
			"When event is seated_conversation_opening, the customer explicitly invited Sumiha to sit and talk. Choose speak, introduce one concrete topic, and normally end with a natural question that is easy to answer. Do not use a generic service offer and do not choose ignore merely because no new user sentence was supplied. "
			"If the customer appears busy, disengaged, or in need of privacy, choose acknowledge or ignore. Do not diagnose the customer's emotion as a fact. "
			"Do not give orders, start a task, or invent an event. Do not mention system state or detection. "
			"The context may include preferred_conversation_topic selected locally from Sumiha's memory. When memory_topic_available is true, normally use that memory as the main conversational topic, referring to it naturally without saying database, memory system, or recall score. "
			"If the memory is negative or private, respond tactfully rather than exposing details. If memory_topic_available is false, use the last served item, Sumiha's current visible activity, or another supplied observable fact as a light topic. "
			"Do not default to generic service phrases such as 何かお手伝いできることがありますか or ご用件はございますか. Ask about help only when the supplied context actually indicates a practical need. "
			"Use only one gentle opener and do not pressure the customer to continue. "
			"Use one of four decisions: speak, acknowledge, thought_only, ignore. "
			"speak means one short natural Japanese utterance. acknowledge means a silent visible response. "
			"thought_only means one short in-character surface thought shown to the player, not hidden reasoning. ignore means no output. "
			"expression must be a short Japanese description such as softly smiling or looking concerned, without parentheses. "
			"expression_intent must be exactly one of neutral, smile, sigh, angry, alert, or pensive and must match expression. "
			"reply and thought must be Japanese. Keep reply to one or two short sentences and do not repeat a generic service farewell. "
			"Return exactly one JSON object: "
			"{\"decision\":\"speak|acknowledge|thought_only|ignore\","
			"\"expression\":\"short Japanese visible expression\","
			"\"expression_intent\":\"neutral|smile|sigh|angry|alert|pensive\","
			"\"thought\":\"optional short Japanese surface thought\","
			"\"reply\":\"spoken Japanese or empty\","
			"\"reason\":\"short reason\"}."));
	Messages.Add(MakeShared<FJsonValueObject>(SystemMessage));

	TSharedPtr<FJsonObject> UserMessage = MakeShared<FJsonObject>();
	UserMessage->SetStringField(TEXT("role"), TEXT("user"));
	UserMessage->SetStringField(TEXT("content"), ContextJson);
	Messages.Add(MakeShared<FJsonValueObject>(UserMessage));
	RootObject->SetArrayField(TEXT("messages"), Messages);

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(OpenAIChatCompletionsUrl);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + OpenAIApiKey);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(60.0f);
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&ULLMClient::OnSocialOpportunityResponseReceived,
		LocalRequestId);

	if (!Request->ProcessRequest())
	{
		bInteractiveRequestPending = false;
		UE_LOG(LogTemp, Error, TEXT("[NPCSocial] failed to start request. RequestId=%d"), LocalRequestId);
		return 0;
	}

	UE_LOG(LogTemp, Log, TEXT("[NPCSocial] LLM decision request sent. RequestId=%d Context=%s"), LocalRequestId, *ContextJson);
	return LocalRequestId;
}

bool ULLMClient::IsInteractiveRequestPending() const
{
	return bInteractiveRequestPending;
}

bool ULLMClient::IsReadyForSocialOpportunity(float MinimumQuietSeconds)
{
	if (bInteractiveRequestPending || bQueuedSpeechPlaying)
	{
		return false;
	}

	if (bUseVoicevoxTTS)
	{
		if (UVoicevoxTTSClient* ResolvedVoicevoxTTSClient = ResolveVoicevoxTTSClient())
		{
			if (ResolvedVoicevoxTTSClient->IsVoicevoxSpeechActive())
			{
				return false;
			}
		}
	}

	const UWorld* World = ResolveSpeechWorld();
	return !World
		|| LastInteractiveOutputWorldTime < 0.0
		|| World->GetTimeSeconds() - LastInteractiveOutputWorldTime >= FMath::Max(MinimumQuietSeconds, 0.0f);
}

ULLMClient* ULLMClient::GetMostRecentInteractiveClient()
{
	return MostRecentInteractiveClient.Get();
}

int32 ULLMClient::SendAutonomyDecisionMessage(const FString& ContextJson)
{
	const int32 LocalRequestId = ++ActiveLLMRequestId;
	LastAutonomyPromptTokens = 0;
	LastAutonomyCompletionTokens = 0;
	LastAutonomyCachedPromptTokens = 0;
	const FString OpenAIApiKey = GetOpenAIApiKey();
	if (OpenAIApiKey.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[AutonomyLLM] OPENAI_API_KEY is not configured."));
		return 0;
	}

	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("model"), OpenAIModelName);
	RootObject->SetNumberField(TEXT("temperature"), 0.35);
	RootObject->SetNumberField(TEXT("max_tokens"), 320);
	RootObject->SetBoolField(TEXT("stream"), false);

	TSharedPtr<FJsonObject> ResponseFormat = MakeShared<FJsonObject>();
	ResponseFormat->SetStringField(TEXT("type"), TEXT("json_object"));
	RootObject->SetObjectField(TEXT("response_format"), ResponseFormat);

	TArray<TSharedPtr<FJsonValue>> Messages;
	TSharedPtr<FJsonObject> SystemMessage = MakeShared<FJsonObject>();
	SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
	SystemMessage->SetStringField(
		TEXT("content"),
		LLMNPCPersona::CorePromptEnglish() + TEXT(
			"Create a medium-horizon, interruptible activity agenda for Sumiha. "
			"Boredom is a desire for stimulation; energy is a capability constraint, so do not treat them as the same need. "
			"Duty represents pressure from real world tasks. Use player context to keep the plan socially appropriate. "
			"Balance service responsibility, care for the environment, and reasonable personal recovery. "
			"Treat npc.emotion and recently_appraised_memory as current context that can color the plan and its reason, "
			"but never let emotion override required service or task steps. Expression intent is a parallel performance cue, not a plan step. "
			"Service and explicit player requests are handled elsewhere, so never invent service actions. "
			"Use only ids present in available_actions, respect each minimum_energy, and avoid recent_actions when alternatives exist. "
			"Respect planning.max_steps. Normally create a coherent three-to-five step agenda so Sumiha has useful follow-up intentions instead of becoming idle after one action. "
			"Use fewer steps only when the available actions, energy, or current situation genuinely make a longer agenda inappropriate. "
			"When available task actions share a non-empty task_group, include every required_task_step needed to finish that task, ordered by task_order. "
			"A step that clears_world_state_on_success is the final completion step and must come last for its task group. "
			"After completing a required task group, add one or two socially appropriate leisure or preference follow-up actions when they are available. "
			"Write surface_thought as one short first-person Japanese intention that can be shown to the player. "
			"It must describe the intention naturally without action ids, scores, prompts, or hidden reasoning; use an empty string when waiting. "
			"Return one JSON object only: "
			"{\"decision\":\"start_plan|wait\",\"goal\":\"short goal\",\"steps\":[\"available id\"],"
			"\"commitment\":\"low|medium\",\"surface_thought\":\"short Japanese intention or empty\","
			"\"reason\":\"one short observable reason\"}. "
			"For wait, return an empty steps array."));
	Messages.Add(MakeShared<FJsonValueObject>(SystemMessage));

	TSharedPtr<FJsonObject> UserMessage = MakeShared<FJsonObject>();
	UserMessage->SetStringField(TEXT("role"), TEXT("user"));
	UserMessage->SetStringField(TEXT("content"), ContextJson);
	Messages.Add(MakeShared<FJsonValueObject>(UserMessage));
	RootObject->SetArrayField(TEXT("messages"), Messages);

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(OpenAIChatCompletionsUrl);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + OpenAIApiKey);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(60.0f);
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&ULLMClient::OnAutonomyResponseReceived,
		LocalRequestId);
	Request->ProcessRequest();

	UE_LOG(LogTemp, Log, TEXT("[AutonomyLLM] request sent. RequestId=%d"), LocalRequestId);
	return LocalRequestId;
}

int32 ULLMClient::SendMemoryAppraisalMessage(const FString& ContextJson)
{
	const int32 LocalRequestId = ++ActiveAppraisalRequestId;
	LastAppraisalPromptTokens = 0;
	LastAppraisalCompletionTokens = 0;
	LastAppraisalCachedPromptTokens = 0;
	const FString OpenAIApiKey = GetOpenAIApiKey();
	if (OpenAIApiKey.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[MemoryAppraisalLLM] OPENAI_API_KEY is not configured."));
		return 0;
	}

	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("model"), OpenAIModelName);
	RootObject->SetNumberField(TEXT("temperature"), 0.2);
	RootObject->SetNumberField(TEXT("max_tokens"), 180);
	RootObject->SetBoolField(TEXT("stream"), false);

	TSharedPtr<FJsonObject> ResponseFormat = MakeShared<FJsonObject>();
	ResponseFormat->SetStringField(TEXT("type"), TEXT("json_object"));
	RootObject->SetObjectField(TEXT("response_format"), ResponseFormat);

	TArray<TSharedPtr<FJsonValue>> Messages;
	TSharedPtr<FJsonObject> SystemMessage = MakeShared<FJsonObject>();
	SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
	SystemMessage->SetStringField(
		TEXT("content"),
		LLMNPCPersona::CorePromptEnglish() + TEXT(
			"Appraise one recalled event for Sumiha. "
			"Infer how the memory changes her immediate emotion and slower mood given her character and current situation. "
			"Her mature self-regulation may soften the visible expression, but it does not erase joy, hurt, embarrassment, or fatigue. "
			"Do not create an action plan and do not invent new events. "
			"Also write one short Japanese surface thought that Sumiha could visibly show to the player. "
			"The thought is character dialogue, not hidden reasoning; do not mention prompts, databases, scores, or system state. "
			"Return one JSON object only: "
			"{\"emotion\":\"short label\",\"valence_delta\":-1.0..1.0,"
			"\"arousal_delta\":-1.0..1.0,\"mood_delta\":-0.3..0.3,"
			"\"expression_intent\":\"neutral|smile|hum|sigh|alert|pensive\","
			"\"interpretation\":\"one short reason grounded in the supplied memory\","
			"\"thought\":\"one short natural Japanese surface thought\"}."));
	Messages.Add(MakeShared<FJsonValueObject>(SystemMessage));

	TSharedPtr<FJsonObject> UserMessage = MakeShared<FJsonObject>();
	UserMessage->SetStringField(TEXT("role"), TEXT("user"));
	UserMessage->SetStringField(TEXT("content"), ContextJson);
	Messages.Add(MakeShared<FJsonValueObject>(UserMessage));
	RootObject->SetArrayField(TEXT("messages"), Messages);

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(OpenAIChatCompletionsUrl);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + OpenAIApiKey);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(60.0f);
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&ULLMClient::OnMemoryAppraisalResponseReceived,
		LocalRequestId);
	Request->ProcessRequest();

	UE_LOG(LogTemp, Log, TEXT("[MemoryAppraisalLLM] request sent. RequestId=%d"), LocalRequestId);
	return LocalRequestId;
}

void ULLMClient::OnOpenAINormalResponseReceived(
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool bWasSuccessful,
    int32 RequestId
)
{
    if (RequestId != ActiveLLMRequestId)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LLM] ignored stale OpenAI response"));
        return;
    }

	bInteractiveRequestPending = false;

    if (!bWasSuccessful || !Response.IsValid())
    {
        const FString ErrorMessage = TEXT("OpenAI request failed.");
        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnLLMErrorReceived.Broadcast(ErrorMessage);
        return;
    }

    const int32 ResponseCode = Response->GetResponseCode();
    const FString ResponseString = Response->GetContentAsString();

    UE_LOG(LogTemp, Warning, TEXT("OpenAI normal response code: %d"), ResponseCode);
    UE_LOG(LogTemp, Warning, TEXT("OpenAI normal raw response: %s"), *ResponseString);

    if (ResponseCode != 200)
    {
        const FString ErrorMessage = FString::Printf(
            TEXT("OpenAI server error. Code=%d, Body=%s"),
            ResponseCode,
            *ResponseString
        );

        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnLLMErrorReceived.Broadcast(ErrorMessage);
        return;
    }

    FString Content;
    FString ErrorMessage;

    if (!ExtractOpenAIMessageContent(ResponseString, Content, ErrorMessage))
    {
        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnLLMErrorReceived.Broadcast(ErrorMessage);
        return;
    }

    TSharedPtr<FJsonObject> AssistantMsg = MakeShared<FJsonObject>();
    AssistantMsg->SetStringField(TEXT("role"), TEXT("assistant"));
    AssistantMsg->SetStringField(TEXT("content"), Content);
    ConversationHistory.Add(MakeShared<FJsonValueObject>(AssistantMsg));

    while (ConversationHistory.Num() > 10)
    {
        ConversationHistory.RemoveAt(0);
    }

    UE_LOG(LogTemp, Warning, TEXT("OpenAI normal reply: %s"), *Content);

    UE_LOG(LogTemp, Warning, TEXT("[LLMClient] Starting immediate normal reply."));
    ClearQueuedSpeech();
    HandleFinalReplyText(Content);
}

void ULLMClient::OnStrategyResponseReceived(
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool bWasSuccessful,
    int32 RequestId
)
{
    if (RequestId != ActiveLLMRequestId)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LLM] ignored stale OpenAI response"));
        return;
    }

	bInteractiveRequestPending = false;

    if (!bWasSuccessful || !Response.IsValid())
    {
        const FString ErrorMessage = TEXT("OpenAI strategy request failed.");
        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnLLMErrorReceived.Broadcast(ErrorMessage);
        return;
    }

    const int32 ResponseCode = Response->GetResponseCode();
    const FString ResponseString = Response->GetContentAsString();

    UE_LOG(LogTemp, Warning, TEXT("OpenAI strategy response code: %d"), ResponseCode);
    UE_LOG(LogTemp, Warning, TEXT("OpenAI strategy raw response: %s"), *ResponseString);

    if (ResponseCode != 200)
    {
        const FString ErrorMessage = FString::Printf(
            TEXT("OpenAI strategy server error. Code=%d, Body=%s"),
            ResponseCode,
            *ResponseString
        );

        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnLLMErrorReceived.Broadcast(ErrorMessage);
        return;
    }

    FString Content;
    FString ErrorMessage;

    if (!ExtractOpenAIMessageContent(ResponseString, Content, ErrorMessage))
    {
        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnLLMErrorReceived.Broadcast(ErrorMessage);
        return;
    }

    const FString CleanContent = CleanJsonContent(Content);

    UE_LOG(LogTemp, Warning, TEXT("Strategy JSON content: %s"), *CleanContent);

    TSharedPtr<FJsonObject> StrategyRoot;
    FString ParsedJsonContent;
    FString LeadingReplyText;

    if (!TryDeserializeJsonObjectContent(
            CleanContent,
            StrategyRoot,
            ParsedJsonContent,
            LeadingReplyText))
    {
        const FString ParseError = FString::Printf(
            TEXT("Failed to parse strategy JSON content: %s"),
            *CleanContent
        );

        UE_LOG(LogTemp, Error, TEXT("%s"), *ParseError);
        OnLLMErrorReceived.Broadcast(ParseError);
        return;
    }
	if (!LeadingReplyText.IsEmpty())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[LLMClient] recovered strategy JSON after leading natural-language text: %s"),
			*LeadingReplyText);
	}

	const FNPCInteractionMemorySignal InteractionMemory = ParseInteractionMemorySignal(
		StrategyRoot,
		LastStrategyUserMessage);
	OnInteractionMemoryExtracted.Broadcast(InteractionMemory);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[InteractionMemory] extracted. Remember=%s Event=%s Valence=%.2f Arousal=%.2f Importance=%.2f Summary=%s"),
		InteractionMemory.bShouldRemember ? TEXT("true") : TEXT("false"),
		*InteractionMemory.EventTag.ToString(),
		InteractionMemory.Valence,
		InteractionMemory.Arousal,
		InteractionMemory.Importance,
		*InteractionMemory.Summary);

    FString Reply = GetStringFieldOrDefault(StrategyRoot, TEXT("reply"), TEXT("")).TrimStartAndEnd();
	if (Reply.IsEmpty() && !LeadingReplyText.IsEmpty())
	{
		Reply = LeadingReplyText;
	}
    FNPCEmbodiedResponseSignal EmbodiedResponse = ParseEmbodiedResponseSignal(StrategyRoot, !Reply.IsEmpty());
    OnNPCEmbodiedResponseRequested.Broadcast(EmbodiedResponse);
    UE_LOG(
        LogTemp,
        Log,
        TEXT("[EmbodiedResponse] parsed. Enabled=%s Style=%s Policy=%s Face=%s Gaze=%s Left=%s Right=%s Voice=%s Prop=%s Resume=%s"),
        EmbodiedResponse.bEnabled ? TEXT("true") : TEXT("false"),
        *EmbodiedResponse.ReactionStyle,
        *EmbodiedResponse.BaseActionPolicy,
        *EmbodiedResponse.FaceIntent,
        *EmbodiedResponse.GazeIntent,
        *EmbodiedResponse.LeftHandIntent,
        *EmbodiedResponse.RightHandIntent,
        *EmbodiedResponse.VoiceIntent,
        *EmbodiedResponse.PropIntent,
        EmbodiedResponse.bResumeAfterReply ? TEXT("true") : TEXT("false"));

    FString StrategyTone = TEXT("unknown");
    FString StrategyLength = TEXT("unknown");
    FString StrategyPriority = TEXT("unknown");
    FString StrategyReason = TEXT("none");

    if (StrategyRoot->HasField(TEXT("strategy")))
    {
        TSharedPtr<FJsonObject> StrategyObject = StrategyRoot->GetObjectField(TEXT("strategy"));

        if (StrategyObject.IsValid())
        {
            StrategyTone = GetStringFieldOrDefault(StrategyObject, TEXT("tone"), TEXT("unknown"));
            StrategyLength = GetStringFieldOrDefault(StrategyObject, TEXT("length"), TEXT("unknown"));
            StrategyPriority = GetStringFieldOrDefault(StrategyObject, TEXT("priority"), TEXT("unknown"));
            StrategyReason = GetStringFieldOrDefault(StrategyObject, TEXT("reason"), TEXT("none"));
        }
    }

    FString ActionName = TEXT("none");
    FString ActionItem = TEXT("none");
    int32 ActionQuantity = 0;
    bool bHasNonNoneAction = false;
    bool bCapturedLegacyAction = false;
    bool bCapturedPlaceOrderAction = false;
    TArray<FParsedStrategyAction> ParsedActions;

    const TArray<TSharedPtr<FJsonValue>>* ActionsArray = nullptr;

    if (StrategyRoot->TryGetArrayField(TEXT("actions"), ActionsArray) && ActionsArray)
    {
        for (const TSharedPtr<FJsonValue>& ActionValue : *ActionsArray)
        {
            TSharedPtr<FJsonObject> ActionObject = ActionValue.IsValid() ? ActionValue->AsObject() : nullptr;
            if (!ActionObject.IsValid())
            {
                continue;
            }

            FString ActionType = GetStringFieldOrDefault(ActionObject, TEXT("type"), TEXT("none"));
            if (ActionType.IsEmpty() || ActionType.Equals(TEXT("none"), ESearchCase::IgnoreCase))
            {
                ActionType = GetStringFieldOrDefault(ActionObject, TEXT("name"), TEXT("none"));
            }
            if (ActionType.IsEmpty())
            {
                ActionType = TEXT("none");
            }
            ActionType = ActionType.TrimStartAndEnd();

            FString Timing = GetStringFieldOrDefault(ActionObject, TEXT("timing"), TEXT("immediate"));
            if (Timing.IsEmpty())
            {
                Timing = TEXT("immediate");
            }
            Timing = Timing.TrimStartAndEnd();

            FString Item = GetStringFieldOrDefault(ActionObject, TEXT("item"), TEXT("none")).TrimStartAndEnd();
            const FString TargetField = GetStringFieldOrDefault(ActionObject, TEXT("target"), TEXT("")).TrimStartAndEnd();
            if ((Item.Equals(TEXT("none"), ESearchCase::IgnoreCase) || Item.IsEmpty()) && !TargetField.IsEmpty())
            {
                Item = TargetField;
            }
            FString ReturnTableID = GetStringFieldOrDefault(ActionObject, TEXT("return_table_id"), TEXT("")).TrimStartAndEnd();
            int32 Quantity = 0;
            float WaitSeconds = 0.0f;

            double QuantityNumber = 0.0;
            if (ActionObject->TryGetNumberField(TEXT("quantity"), QuantityNumber))
            {
                Quantity = static_cast<int32>(QuantityNumber);
            }
            else
            {
                FString QuantityString;
                if (ActionObject->TryGetStringField(TEXT("quantity"), QuantityString))
                {
                    Quantity = FCString::Atoi(*QuantityString);
                }
            }

            double SecondsNumber = 0.0;
            if (ActionObject->TryGetNumberField(TEXT("seconds"), SecondsNumber))
            {
                WaitSeconds = static_cast<float>(SecondsNumber);
            }

            if (ActionObject->HasField(TEXT("args")))
            {
                TSharedPtr<FJsonObject> ArgsObject = ActionObject->GetObjectField(TEXT("args"));
                if (ArgsObject.IsValid())
                {
                    if (Item.Equals(TEXT("none"), ESearchCase::IgnoreCase) || Item.IsEmpty())
                    {
                        Item = GetStringFieldOrDefault(ArgsObject, TEXT("item"), TEXT("none")).TrimStartAndEnd();
                    }

                    if (Item.Equals(TEXT("none"), ESearchCase::IgnoreCase) || Item.IsEmpty())
                    {
                        Item = GetStringFieldOrDefault(ArgsObject, TEXT("target"), TEXT("")).TrimStartAndEnd();
                    }

                    if (ReturnTableID.IsEmpty())
                    {
                        ReturnTableID = GetStringFieldOrDefault(ArgsObject, TEXT("return_table_id"), TEXT("")).TrimStartAndEnd();
                    }

                    if (Quantity == 0)
                    {
                        Quantity = GetQuantityFromArgs(ArgsObject);
                    }

                    if (WaitSeconds <= 0.0f && ArgsObject->TryGetNumberField(TEXT("seconds"), SecondsNumber))
                    {
                        WaitSeconds = static_cast<float>(SecondsNumber);
                    }
                }
            }

            const bool bIsNoneAction = ActionType.Equals(TEXT("none"), ESearchCase::IgnoreCase);
            const bool bIsPlaceOrderAction =
                ActionType.Equals(TEXT("place_order"), ESearchCase::IgnoreCase) ||
                ActionType.Equals(TEXT("PlaceOrder"), ESearchCase::IgnoreCase);
            const bool bIsCounterThenReturnAction = ActionType.Equals(TEXT("counter_then_return"), ESearchCase::IgnoreCase);
            const bool bIsPlayerAction =
                ActionType.Equals(TEXT("move_near_player"), ESearchCase::IgnoreCase) ||
                ActionType.Equals(TEXT("follow_player"), ESearchCase::IgnoreCase) ||
                ActionType.Equals(TEXT("stop_follow_player"), ESearchCase::IgnoreCase);

            if (bIsCounterThenReturnAction)
            {
                if (ReturnTableID.IsEmpty() && !Item.IsEmpty() && !Item.Equals(TEXT("none"), ESearchCase::IgnoreCase))
                {
                    ReturnTableID = Item;
                }

                if (ReturnTableID.IsEmpty())
                {
                    ReturnTableID = TEXT("current_table");
                }

                Item = ReturnTableID;
            }
            else if (bIsPlayerAction)
            {
                Item = TEXT("player");
            }

            UE_LOG(LogTemp, Warning, TEXT("[Strategy] action parsed: type=%s, timing=%s, item=%s, return_table_id=%s, quantity=%d"), *ActionType, *Timing, *Item, *ReturnTableID, Quantity);

            if (bIsCounterThenReturnAction)
            {
                UE_LOG(LogTemp, Warning, TEXT("[Strategy] counter_then_return / %s"), *ReturnTableID);
            }

            if (!bIsNoneAction)
            {
                bHasNonNoneAction = true;
            }

            if (bIsPlaceOrderAction && !bCapturedPlaceOrderAction)
            {
                ActionName = TEXT("PlaceOrder");
                ActionItem = Item;
                ActionQuantity = Quantity;
                bCapturedLegacyAction = true;
                bCapturedPlaceOrderAction = true;
            }
            else if (!bCapturedLegacyAction && !bIsNoneAction)
            {
                ActionName = ActionType;
                ActionItem = Item;
                ActionQuantity = Quantity;
                bCapturedLegacyAction = true;
            }

            if (!bIsNoneAction)
            {
                FParsedStrategyAction ParsedAction;
                ParsedAction.ActionType = ActionType;
                ParsedAction.Timing = Timing;
                ParsedAction.Item = Item;
                ParsedAction.Quantity = Quantity;
                ParsedAction.WaitSeconds = WaitSeconds;
                ParsedActions.Add(ParsedAction);
            }
        }
    }

	if (UNPCAutonomyComponent* Autonomy = UNPCAutonomyComponent::GetPrimaryAutonomyComponent();
		Autonomy && Autonomy->IsSeatInvitationPending())
	{
		auto IsSeatResolutionAction = [](const FParsedStrategyAction& Action)
		{
			return Action.ActionType.Equals(TEXT("accept_seat_invitation"), ESearchCase::IgnoreCase)
				|| Action.ActionType.Equals(TEXT("decline_seat_invitation"), ESearchCase::IgnoreCase);
		};
		auto IsConversationOnlyAction = [](const FParsedStrategyAction& Action)
		{
			return Action.ActionType.Equals(TEXT("greet"), ESearchCase::IgnoreCase)
				|| Action.ActionType.Equals(TEXT("speak"), ESearchCase::IgnoreCase)
				|| Action.ActionType.Equals(TEXT("explain"), ESearchCase::IgnoreCase);
		};

		const int32 SeatResolutionIndex = ParsedActions.IndexOfByPredicate(IsSeatResolutionAction);
		const bool bHasIncompatibleRequestedAction = ParsedActions.ContainsByPredicate(
			[&IsSeatResolutionAction, &IsConversationOnlyAction](const FParsedStrategyAction& Action)
			{
				return !IsSeatResolutionAction(Action) && !IsConversationOnlyAction(Action);
			});
		if (SeatResolutionIndex != INDEX_NONE && !bHasIncompatibleRequestedAction)
		{
			const FParsedStrategyAction ResolutionAction = ParsedActions[SeatResolutionIndex];
			ParsedActions.Reset();
			ParsedActions.Add(ResolutionAction);
			ActionName = ResolutionAction.ActionType;
			ActionItem = ResolutionAction.Item;
			ActionQuantity = ResolutionAction.Quantity;
			bHasNonNoneAction = true;
		}
		else if (SeatResolutionIndex != INDEX_NONE && bHasIncompatibleRequestedAction)
		{
			ParsedActions.RemoveAll(IsSeatResolutionAction);
			if (!ParsedActions.IsEmpty())
			{
				ActionName = ParsedActions[0].ActionType;
				ActionItem = ParsedActions[0].Item;
				ActionQuantity = ParsedActions[0].Quantity;
			}
		}
		else if (!bHasIncompatibleRequestedAction)
		{
			FString InferredAction;
			if (TryInferSeatInvitationResponseAction(LastStrategyUserMessage, InferredAction))
			{
				FParsedStrategyAction ResolutionAction = MakeParsedStrategyAction(InferredAction, TEXT("none"));
				ResolutionAction.Timing = TEXT("immediate");
				ParsedActions.Reset();
				ParsedActions.Add(ResolutionAction);
				ActionName = InferredAction;
				ActionItem = TEXT("none");
				ActionQuantity = 0;
				bHasNonNoneAction = true;
				UE_LOG(
					LogTemp,
					Log,
					TEXT("[NPCSeatInvitation] locally inferred missing consent action: %s"),
					*InferredAction);
			}
		}
	}

	const bool bPlayerCanHostOppositeSeat = UNPCActionQueueComponent::IsLastKnownSeatedForService()
		|| UNPCActionQueueComponent::IsLastKnownPlayerInTableArea();
	if (bPlayerCanHostOppositeSeat && IsDirectSitOppositeRequest(LastStrategyUserMessage))
	{
		FParsedStrategyAction SitAction = MakeParsedStrategyAction(TEXT("sit_opposite_player"), TEXT("current_table"));
		SitAction.Timing = TEXT("immediate");
		ParsedActions.Reset();
		ParsedActions.Add(SitAction);
		ActionName = SitAction.ActionType;
		ActionItem = SitAction.Item;
		ActionQuantity = 0;
		bHasNonNoneAction = true;
		UE_LOG(LogTemp, Log, TEXT("[NPCSeat] locally enforced explicit sit-opposite request."));
	}

    const bool bStrategyReplyHasSpeech = !Reply.TrimStartAndEnd().IsEmpty();

    if (ParsedActions.Num() > 1)
    {
        const FString SequenceJson = BuildActionSequenceJson(ParsedActions);
        ActionName = TEXT("action_sequence");
        ActionItem = SequenceJson;
        ActionQuantity = 0;
        UE_LOG(LogTemp, Warning, TEXT("[Strategy] broadcasting action: action_sequence, item=%s"), *SequenceJson);
        OnNPCActionRequested.Broadcast(TEXT("action_sequence"), TEXT("immediate"), SequenceJson, 0);
    }
    else if (ParsedActions.Num() == 1)
    {
        const FParsedStrategyAction& ParsedAction = ParsedActions[0];
        FString CorrectedSequenceJson;
        if (TryBuildCounterTableSequenceFromUserMessage(LastStrategyUserMessage, CorrectedSequenceJson)
            || TryBuildCounterSemanticOverride(LastStrategyUserMessage, ParsedAction, CorrectedSequenceJson))
        {
            ActionName = TEXT("action_sequence");
            ActionItem = CorrectedSequenceJson;
            ActionQuantity = 0;
            UE_LOG(LogTemp, Warning, TEXT("[Strategy] corrected action to action_sequence, item=%s"), *CorrectedSequenceJson);
            UE_LOG(LogTemp, Warning, TEXT("[Strategy] broadcasting action: action_sequence, item=%s"), *CorrectedSequenceJson);
            OnNPCActionRequested.Broadcast(TEXT("action_sequence"), TEXT("immediate"), CorrectedSequenceJson, 0);
        }
        else
        {
            const bool bDeferGuideAction = bStrategyReplyHasSpeech
                && ShouldDeferGuideActionUntilReplyFinished(ParsedAction.ActionType, Reply);
            if (bDeferGuideAction)
            {
                StoreDeferredStrategyAction(
                    ParsedAction.ActionType,
                    ParsedAction.Timing,
                    ParsedAction.Item,
                    ParsedAction.Quantity,
                    RequestId);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[Strategy] broadcasting action: %s, item=%s"), *ParsedAction.ActionType, *ParsedAction.Item);
                const bool bSuppressOrderConfirmSpeech = bStrategyReplyHasSpeech && IsOrderFulfillmentActionType(ParsedAction.ActionType);
                if (bSuppressOrderConfirmSpeech)
                {
                    UNPCActionQueueComponent::SetSuppressNextOrderConfirmSpeechForStrategyReply(true);
                }

                OnNPCActionRequested.Broadcast(ParsedAction.ActionType, ParsedAction.Timing, ParsedAction.Item, ParsedAction.Quantity);

                if (bSuppressOrderConfirmSpeech)
                {
                    UNPCActionQueueComponent::SetSuppressNextOrderConfirmSpeechForStrategyReply(false);
                }
            }
        }
    }

    FString Decision = GetStringFieldOrDefault(StrategyRoot, TEXT("decision"), TEXT(""));
    if (Decision.IsEmpty())
    {
        const bool bHasSpeech = !Reply.IsEmpty();
        if (bHasSpeech && bHasNonNoneAction)
        {
            Decision = TEXT("act_and_speak");
        }
        else if (bHasSpeech)
        {
            Decision = TEXT("speak");
        }
        else if (bHasNonNoneAction)
        {
            Decision = TEXT("act");
        }
        else
        {
            Decision = TEXT("none");
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("Strategy decision: %s"), *Decision);
    UE_LOG(LogTemp, Warning, TEXT("Strategy reply: %s"), *Reply);
    UE_LOG(LogTemp, Warning, TEXT("Strategy tone: %s"), *StrategyTone);
    UE_LOG(LogTemp, Warning, TEXT("Strategy length: %s"), *StrategyLength);
    UE_LOG(LogTemp, Warning, TEXT("Strategy priority: %s"), *StrategyPriority);
    UE_LOG(LogTemp, Warning, TEXT("Strategy reason: %s"), *StrategyReason);
    UE_LOG(LogTemp, Warning, TEXT("Strategy action: %s, item=%s, quantity=%d"), *ActionName, *ActionItem, ActionQuantity);

    OnStrategyResultReceived.Broadcast(
        Reply,
        StrategyTone,
        StrategyLength,
        StrategyPriority,
        FString(),
        ActionName,
        ActionItem,
        ActionQuantity,
        ParsedJsonContent
    );

    if (!Reply.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LLMClient] Starting immediate strategy reply."));
        ClearQueuedSpeech();
		const FString DisplayReply = EmbodiedResponse.bEnabled && !EmbodiedResponse.DisplayNote.IsEmpty()
			? FString::Printf(TEXT("（%s）%s"), *EmbodiedResponse.DisplayNote, *Reply)
			: Reply;
		PlaySpeechAndDisplayInternal(DisplayReply, Reply, false, true);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[Strategy] empty reply, skipping speech"));
	}
}

void ULLMClient::OnSocialOpportunityResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful,
	int32 RequestId)
{
	if (RequestId != ActiveLLMRequestId)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NPCSocial] ignored stale LLM response. RequestId=%d"), RequestId);
		return;
	}

	bInteractiveRequestPending = false;
	if (!bWasSuccessful || !Response.IsValid())
	{
		const FString ErrorMessage = TEXT("NPC social initiative request failed.");
		UE_LOG(LogTemp, Error, TEXT("[NPCSocial] %s"), *ErrorMessage);
		OnLLMErrorReceived.Broadcast(ErrorMessage);
		return;
	}

	const int32 ResponseCode = Response->GetResponseCode();
	const FString ResponseString = Response->GetContentAsString();
	if (ResponseCode != 200)
	{
		const FString ErrorMessage = FString::Printf(
			TEXT("NPC social initiative server error. Code=%d, Body=%s"),
			ResponseCode,
			*ResponseString);
		UE_LOG(LogTemp, Error, TEXT("[NPCSocial] %s"), *ErrorMessage);
		OnLLMErrorReceived.Broadcast(ErrorMessage);
		return;
	}

	FString Content;
	FString ExtractError;
	if (!ExtractOpenAIMessageContent(ResponseString, Content, ExtractError))
	{
		UE_LOG(LogTemp, Error, TEXT("[NPCSocial] %s"), *ExtractError);
		OnLLMErrorReceived.Broadcast(ExtractError);
		return;
	}

	const FString CleanContent = CleanJsonContent(Content);
	TSharedPtr<FJsonObject> SocialRoot;
	FString ParsedJsonContent;
	FString LeadingText;
	if (!TryDeserializeJsonObjectContent(
			CleanContent,
			SocialRoot,
			ParsedJsonContent,
			LeadingText))
	{
		const FString ParseError = FString::Printf(
			TEXT("Failed to parse NPC social initiative JSON: %s"),
			*CleanContent);
		UE_LOG(LogTemp, Error, TEXT("[NPCSocial] %s"), *ParseError);
		OnLLMErrorReceived.Broadcast(ParseError);
		return;
	}

	auto CleanCue = [](FString Cue)
	{
		Cue = Cue.TrimStartAndEnd();
		const bool bHasFullWidthParens = Cue.Len() >= 2
			&& Cue.StartsWith(TEXT("\uFF08"))
			&& Cue.EndsWith(TEXT("\uFF09"));
		const bool bHasAsciiParens = Cue.Len() >= 2
			&& Cue.StartsWith(TEXT("("))
			&& Cue.EndsWith(TEXT(")"));
		if (bHasFullWidthParens || bHasAsciiParens)
		{
			Cue = Cue.Mid(1, Cue.Len() - 2).TrimStartAndEnd();
		}
		Cue.ReplaceInline(TEXT("\r"), TEXT(" "));
		Cue.ReplaceInline(TEXT("\n"), TEXT(" "));
		return Cue;
	};

	FString Decision = GetStringFieldOrDefault(SocialRoot, TEXT("decision"), TEXT("ignore")).TrimStartAndEnd().ToLower();
	FString Expression = CleanCue(GetStringFieldOrDefault(SocialRoot, TEXT("expression"), TEXT("")));
	FString ExpressionIntent = GetStringFieldOrDefault(
		SocialRoot,
		TEXT("expression_intent"),
		TEXT("")).TrimStartAndEnd().ToLower();
	FString Thought = CleanCue(GetStringFieldOrDefault(SocialRoot, TEXT("thought"), TEXT("")));
	FString Reply = GetStringFieldOrDefault(SocialRoot, TEXT("reply"), TEXT("")).TrimStartAndEnd();
	if (Reply.IsEmpty() && !LeadingText.IsEmpty())
	{
		Reply = LeadingText;
	}
	const FString Reason = GetStringFieldOrDefault(SocialRoot, TEXT("reason"), TEXT("none")).TrimStartAndEnd();

	if (Decision != TEXT("speak")
		&& Decision != TEXT("acknowledge")
		&& Decision != TEXT("thought_only")
		&& Decision != TEXT("ignore"))
	{
		Decision = Reply.IsEmpty() ? TEXT("ignore") : TEXT("speak");
	}
	if (Decision == TEXT("speak") && Reply.IsEmpty())
	{
		Decision = Expression.IsEmpty() && Thought.IsEmpty() ? TEXT("ignore") : TEXT("acknowledge");
	}
	static const TArray<FString> AllowedExpressionIntents = {
		TEXT("neutral"), TEXT("smile"), TEXT("sigh"), TEXT("angry"), TEXT("alert"), TEXT("pensive")
	};
	if (!AllowedExpressionIntents.Contains(ExpressionIntent))
	{
		if (Expression.Contains(TEXT("笑")) || Expression.Contains(TEXT("嬉")))
		{
			ExpressionIntent = TEXT("smile");
		}
		else if (Expression.Contains(TEXT("驚")) || Expression.Contains(TEXT("警戒")))
		{
			ExpressionIntent = TEXT("alert");
		}
		else if (Expression.Contains(TEXT("ため息")) || Expression.Contains(TEXT("悲")))
		{
			ExpressionIntent = TEXT("sigh");
		}
		else if (Expression.Contains(TEXT("心配"))
			|| Expression.Contains(TEXT("困"))
			|| Expression.Contains(TEXT("考")))
		{
			ExpressionIntent = TEXT("pensive");
		}
		else
		{
			ExpressionIntent = TEXT("neutral");
		}
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[NPCSocial] decision=%s expression=%s intent=%s thought=%s reply=%s reason=%s"),
		*Decision,
		*Expression,
		*ExpressionIntent,
		*Thought,
		*Reply,
		*Reason);

	if (Decision == TEXT("ignore"))
	{
		return;
	}

	FString DisplayText;
	FString SpeechText;
	if (Decision == TEXT("speak"))
	{
		DisplayText = Expression.IsEmpty()
			? Reply
			: FString::Printf(TEXT("\uFF08%s\uFF09%s"), *Expression, *Reply);
		SpeechText = Reply;
		UE_LOG(LogTemp, Log, TEXT("[NPCSocial] proactive speech will not interrupt the current autonomy activity."));
		ClearQueuedSpeech();
		InterruptCurrentVoicevoxSpeech();
	}
	else if (Decision == TEXT("thought_only"))
	{
		const FString ThoughtDisplay = Thought.IsEmpty() ? Expression : Thought;
		if (!ThoughtDisplay.IsEmpty())
		{
			DisplayText = FString::Printf(TEXT("\uFF08%s\uFF09"), *ThoughtDisplay);
		}
	}
	else
	{
		const FString AcknowledgementDisplay = Expression.IsEmpty() ? Thought : Expression;
		if (!AcknowledgementDisplay.IsEmpty())
		{
			DisplayText = FString::Printf(TEXT("\uFF08%s\uFF09"), *AcknowledgementDisplay);
		}
	}

	OnNPCSocialExpressionRequested.Broadcast(ExpressionIntent, Expression, 8.0f);

	PlaySpeechAndDisplayInternal(DisplayText, SpeechText, false, true);
}

void ULLMClient::OnAutonomyResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful,
	int32 RequestId)
{
	if (RequestId != ActiveLLMRequestId)
	{
		UE_LOG(LogTemp, Log, TEXT("[AutonomyLLM] ignored stale response. RequestId=%d"), RequestId);
		return;
	}

	if (!bWasSuccessful || !Response.IsValid())
	{
		OnAutonomyDecisionError.Broadcast(RequestId, TEXT("Autonomy request failed."));
		return;
	}

	const int32 ResponseCode = Response->GetResponseCode();
	const FString ResponseString = Response->GetContentAsString();
	if (ResponseCode != 200)
	{
		const FString ErrorMessage = FString::Printf(
			TEXT("Autonomy server error. Code=%d, Body=%s"),
			ResponseCode,
			*ResponseString);
		OnAutonomyDecisionError.Broadcast(RequestId, ErrorMessage);
		return;
	}

	TSharedPtr<FJsonObject> ResponseObject;
	TSharedRef<TJsonReader<>> ResponseReader = TJsonReaderFactory<>::Create(ResponseString);
	if (FJsonSerializer::Deserialize(ResponseReader, ResponseObject) && ResponseObject.IsValid())
	{
		const TSharedPtr<FJsonObject>* UsageObject = nullptr;
		if (ResponseObject->TryGetObjectField(TEXT("usage"), UsageObject) && UsageObject && UsageObject->IsValid())
		{
			double PromptTokens = 0.0;
			double CompletionTokens = 0.0;
			(*UsageObject)->TryGetNumberField(TEXT("prompt_tokens"), PromptTokens);
			(*UsageObject)->TryGetNumberField(TEXT("completion_tokens"), CompletionTokens);
			LastAutonomyPromptTokens = FMath::Max(FMath::RoundToInt(PromptTokens), 0);
			LastAutonomyCompletionTokens = FMath::Max(FMath::RoundToInt(CompletionTokens), 0);

			const TSharedPtr<FJsonObject>* PromptDetails = nullptr;
			if ((*UsageObject)->TryGetObjectField(TEXT("prompt_tokens_details"), PromptDetails)
				&& PromptDetails
				&& PromptDetails->IsValid())
			{
				double CachedTokens = 0.0;
				(*PromptDetails)->TryGetNumberField(TEXT("cached_tokens"), CachedTokens);
				LastAutonomyCachedPromptTokens = FMath::Max(FMath::RoundToInt(CachedTokens), 0);
			}
		}
	}

	FString Content;
	FString ExtractError;
	if (!ExtractOpenAIMessageContent(ResponseString, Content, ExtractError))
	{
		OnAutonomyDecisionError.Broadcast(RequestId, ExtractError);
		return;
	}

	const FString CleanContent = CleanJsonContent(Content);
	TSharedPtr<FJsonObject> DecisionObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CleanContent);
	if (!FJsonSerializer::Deserialize(Reader, DecisionObject) || !DecisionObject.IsValid())
	{
		OnAutonomyDecisionError.Broadcast(
			RequestId,
			FString::Printf(TEXT("Failed to parse autonomy JSON: %s"), *CleanContent));
		return;
	}

	FString Decision;
	DecisionObject->TryGetStringField(TEXT("decision"), Decision);
	Decision = Decision.TrimStartAndEnd();

	TArray<FString> Steps;
	const TArray<TSharedPtr<FJsonValue>>* StepValues = nullptr;
	if (DecisionObject->TryGetArrayField(TEXT("steps"), StepValues) && StepValues)
	{
		for (const TSharedPtr<FJsonValue>& StepValue : *StepValues)
		{
			FString Step;
			if (StepValue.IsValid() && StepValue->TryGetString(Step))
			{
				Step = Step.TrimStartAndEnd();
				if (!Step.IsEmpty())
				{
					Steps.Add(Step);
				}
			}
		}
	}

	FString SelectedAction;
	if (!Steps.IsEmpty())
	{
		SelectedAction = Steps[0];
	}
	else
	{
		DecisionObject->TryGetStringField(TEXT("selected_action"), SelectedAction);
		SelectedAction = SelectedAction.TrimStartAndEnd();
	}

	if (Decision.IsEmpty())
	{
		Decision = SelectedAction.IsEmpty() || SelectedAction.Equals(TEXT("none"), ESearchCase::IgnoreCase)
			? TEXT("wait")
			: TEXT("start_plan");
	}
	if (Decision.Equals(TEXT("wait"), ESearchCase::IgnoreCase))
	{
		SelectedAction = TEXT("none");
	}
	else if (SelectedAction.IsEmpty())
	{
		OnAutonomyDecisionError.Broadcast(RequestId, TEXT("Autonomy plan response has no executable steps."));
		return;
	}

	FString Reason;
	DecisionObject->TryGetStringField(TEXT("reason"), Reason);
	Reason = Reason.TrimStartAndEnd();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[AutonomyLLM] decision received. RequestId=%d Decision=%s FirstAction=%s PromptTokens=%d CompletionTokens=%d Reason=%s"),
		RequestId,
		*Decision,
		*SelectedAction,
		LastAutonomyPromptTokens,
		LastAutonomyCompletionTokens,
		*Reason);
	OnAutonomyDecisionReceived.Broadcast(RequestId, SelectedAction, Reason, CleanContent);
}

void ULLMClient::OnMemoryAppraisalResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful,
	int32 RequestId)
{
	if (RequestId != ActiveAppraisalRequestId)
	{
		UE_LOG(LogTemp, Log, TEXT("[MemoryAppraisalLLM] ignored stale response. RequestId=%d"), RequestId);
		return;
	}

	if (!bWasSuccessful || !Response.IsValid())
	{
		OnMemoryAppraisalError.Broadcast(RequestId, TEXT("Memory appraisal request failed."));
		return;
	}

	const int32 ResponseCode = Response->GetResponseCode();
	const FString ResponseString = Response->GetContentAsString();
	if (ResponseCode != 200)
	{
		OnMemoryAppraisalError.Broadcast(
			RequestId,
			FString::Printf(
				TEXT("Memory appraisal server error. Code=%d, Body=%s"),
				ResponseCode,
				*ResponseString));
		return;
	}

	TSharedPtr<FJsonObject> ResponseObject;
	TSharedRef<TJsonReader<>> ResponseReader = TJsonReaderFactory<>::Create(ResponseString);
	if (FJsonSerializer::Deserialize(ResponseReader, ResponseObject) && ResponseObject.IsValid())
	{
		const TSharedPtr<FJsonObject>* UsageObject = nullptr;
		if (ResponseObject->TryGetObjectField(TEXT("usage"), UsageObject) && UsageObject && UsageObject->IsValid())
		{
			double PromptTokens = 0.0;
			double CompletionTokens = 0.0;
			(*UsageObject)->TryGetNumberField(TEXT("prompt_tokens"), PromptTokens);
			(*UsageObject)->TryGetNumberField(TEXT("completion_tokens"), CompletionTokens);
			LastAppraisalPromptTokens = FMath::Max(FMath::RoundToInt(PromptTokens), 0);
			LastAppraisalCompletionTokens = FMath::Max(FMath::RoundToInt(CompletionTokens), 0);

			const TSharedPtr<FJsonObject>* PromptDetails = nullptr;
			if ((*UsageObject)->TryGetObjectField(TEXT("prompt_tokens_details"), PromptDetails)
				&& PromptDetails
				&& PromptDetails->IsValid())
			{
				double CachedTokens = 0.0;
				(*PromptDetails)->TryGetNumberField(TEXT("cached_tokens"), CachedTokens);
				LastAppraisalCachedPromptTokens = FMath::Max(FMath::RoundToInt(CachedTokens), 0);
			}
		}
	}

	FString Content;
	FString ExtractError;
	if (!ExtractOpenAIMessageContent(ResponseString, Content, ExtractError))
	{
		OnMemoryAppraisalError.Broadcast(RequestId, ExtractError);
		return;
	}

	const FString CleanContent = CleanJsonContent(Content);
	TSharedPtr<FJsonObject> AppraisalObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CleanContent);
	if (!FJsonSerializer::Deserialize(Reader, AppraisalObject) || !AppraisalObject.IsValid())
	{
		OnMemoryAppraisalError.Broadcast(
			RequestId,
			FString::Printf(TEXT("Failed to parse memory appraisal JSON: %s"), *CleanContent));
		return;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[MemoryAppraisalLLM] response received. RequestId=%d PromptTokens=%d CompletionTokens=%d JSON=%s"),
		RequestId,
		LastAppraisalPromptTokens,
		LastAppraisalCompletionTokens,
		*CleanContent);
	OnMemoryAppraisalReceived.Broadcast(RequestId, CleanContent);
}

void ULLMClient::TryPlayNextQueuedSpeech()
{
    if (bQueuedSpeechPlaying || PendingServiceSpeechQueue.Num() == 0)
    {
        return;
    }

    const FString NextSpeech = PendingServiceSpeechQueue[0];
    PendingServiceSpeechQueue.RemoveAt(0);

    bQueuedSpeechPlaying = true;
    ActiveQueuedSpeechGeneration = QueuedSpeechGeneration;
    UE_LOG(LogTemp, Warning, TEXT("[SpeechQueue] Playing queued service speech: %s"), *NextSpeech);

    PlaySpeechInternal(NextSpeech, true, false);
}

void ULLMClient::HandleQueuedSpeechFinished(int32 Generation)
{
    if (Generation != QueuedSpeechGeneration || !bQueuedSpeechPlaying)
    {
        return;
    }

    bQueuedSpeechPlaying = false;
    bCurrentAudioIsQueuedServiceSpeech = false;
    if (UWorld* World = ResolveSpeechWorld())
    {
        World->GetTimerManager().ClearTimer(QueuedSpeechGapTimerHandle);
    }
    UE_LOG(LogTemp, Warning, TEXT("[SpeechQueue] Queued speech finished. Playing next if any."));
    TryPlayNextQueuedSpeech();
}

void ULLMClient::HandleQueuedSpeechFallbackTimeout(int32 Generation)
{
    if (Generation != QueuedSpeechGeneration || !bQueuedSpeechPlaying)
    {
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("[SpeechQueue] Queued speech fallback timeout. Forcing finish."));
    bCurrentAudioIsQueuedServiceSpeech = false;
    HandleQueuedSpeechFinished(Generation);
}

UWorld* ULLMClient::ResolveSpeechWorld() const
{
    if (UWorld* World = GetWorld())
    {
        return World;
    }

    if (VoicevoxTTSClient)
    {
        if (UWorld* World = VoicevoxTTSClient->GetWorld())
        {
            return World;
        }
    }

    if (AActor* OuterActor = GetTypedOuter<AActor>())
    {
        return OuterActor->GetWorld();
    }

    if (UActorComponent* OuterComponent = GetTypedOuter<UActorComponent>())
    {
        return OuterComponent->GetWorld();
    }

    return nullptr;
}

bool ULLMClient::IsServiceSpeechDispatchActive()
{
    return GServiceSpeechDispatchDepth > 0;
}

void ULLMClient::CancelDeferredStrategyAction(bool bLogCancellation)
{
    if (!bHasDeferredStrategyAction)
    {
        return;
    }

    bHasDeferredStrategyAction = false;
    DeferredStrategyActionType = TEXT("");
    DeferredStrategyActionTiming = TEXT("");
    DeferredStrategyActionItem = TEXT("");
    DeferredStrategyActionQuantity = 0;
    DeferredStrategyRequestId = 0;

    if (bLogCancellation)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LLMClient] Deferred guide action cancelled by new user input."));
    }
}

bool ULLMClient::ShouldDeferGuideActionUntilReplyFinished(const FString& ActionType, const FString& ReplyText)
{
    if (ReplyText.TrimStartAndEnd().IsEmpty())
    {
        return false;
    }

    if (SanitizeReplyTextForSpeechAndDisplay(ReplyText).IsEmpty())
    {
        return false;
    }

    if (!ActionType.Equals(TEXT("guide_player_to_table"), ESearchCase::IgnoreCase))
    {
        return false;
    }

    if (UNPCActionQueueComponent::IsLastKnownGuidePlayerInProgress())
    {
        return false;
    }

    if (UNPCActionQueueComponent::IsLastKnownSeatedForService()
        || UNPCActionQueueComponent::IsLastKnownPlayerInTableArea()
        || UNPCActionQueueComponent::IsLastKnownSeatingGuidanceCompleted())
    {
        return false;
    }

    if (!bUseVoicevoxTTS)
    {
        return false;
    }

    return ResolveVoicevoxTTSClient() != nullptr;
}

void ULLMClient::StoreDeferredStrategyAction(const FString& ActionType, const FString& Timing, const FString& Item, int32 Quantity, int32 RequestId)
{
    bHasDeferredStrategyAction = true;
    DeferredStrategyActionType = ActionType;
    DeferredStrategyActionTiming = Timing;
    DeferredStrategyActionItem = Item;
    DeferredStrategyActionQuantity = Quantity;
    DeferredStrategyRequestId = RequestId;
    UE_LOG(LogTemp, Warning, TEXT("[LLMClient] Deferring guide action until strategy reply finishes."));
}

void ULLMClient::DispatchDeferredStrategyActionIfReady()
{
    if (!bHasDeferredStrategyAction)
    {
        return;
    }

    if (DeferredStrategyRequestId != ActiveLLMRequestId)
    {
        CancelDeferredStrategyAction(false);
        return;
    }

    const FString ActionType = DeferredStrategyActionType;
    const FString Timing = DeferredStrategyActionTiming;
    const FString Item = DeferredStrategyActionItem;
    const int32 Quantity = DeferredStrategyActionQuantity;
    CancelDeferredStrategyAction(false);

    UE_LOG(LogTemp, Warning, TEXT("[LLMClient] Strategy reply finished. Dispatching deferred guide action."));
    UNPCActionQueueComponent::SetSuppressNextGuideStartedSpeechForStrategyReply(true);
    OnNPCActionRequested.Broadcast(ActionType, Timing, Item, Quantity);
}

FString ULLMClient::SanitizeReplyTextForSpeechAndDisplay(const FString& RawText) const
{
    FString CleanText = RawText;

    RemoveDelimitedSections(CleanText, TEXT('\uFF08'), TEXT('\uFF09'));
    RemoveDelimitedSections(CleanText, TEXT('('), TEXT(')'));
    RemoveDelimitedSections(CleanText, TEXT('['), TEXT(']'));
    RemoveDelimitedSections(CleanText, TEXT('\u3010'), TEXT('\u3011'));
    RemoveDelimitedSections(CleanText, TEXT('<'), TEXT('>'));

    return CleanText.TrimStartAndEnd();
}

void ULLMClient::InterruptCurrentVoicevoxSpeech()
{
    if (!bUseVoicevoxTTS)
    {
        return;
    }

    UVoicevoxTTSClient* ResolvedVoicevoxTTSClient = ResolveVoicevoxTTSClient();
    if (!ResolvedVoicevoxTTSClient)
    {
        return;
    }

    if (!ResolvedVoicevoxTTSClient->IsVoicevoxSpeechActive())
    {
        return;
    }

    ResolvedVoicevoxTTSClient->StopVoicevoxSpeech();
    UE_LOG(LogTemp, Warning, TEXT("[LLMClient] Interrupted current VOICEVOX speech before new request."));
}

void ULLMClient::HandleFinalReplyText(const FString& ReplyText)
{
    PlaySpeechInternal(ReplyText, false, true);
}

void ULLMClient::PlaySpeechInternal(const FString& ReplyText, bool bQueuedServiceSpeech, bool bInterruptCurrent)
{
    const FString CleanReplyText = SanitizeReplyTextForSpeechAndDisplay(ReplyText);
    if (CleanReplyText.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LLM] final reply text is empty after sanitizing; skipping subtitle and speech"));
        return;
    }

	PlaySpeechAndDisplayInternal(
		CleanReplyText,
		CleanReplyText,
		bQueuedServiceSpeech,
		bInterruptCurrent);
}

void ULLMClient::PlaySpeechAndDisplayInternal(
	const FString& DisplayText,
	const FString& SpeechText,
	bool bQueuedServiceSpeech,
	bool bInterruptCurrent)
{
	const FString CleanDisplayText = DisplayText.TrimStartAndEnd();
	const FString CleanSpeechText = SanitizeReplyTextForSpeechAndDisplay(SpeechText);
	if (CleanDisplayText.IsEmpty() && CleanSpeechText.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[LLM] display and speech text are empty; skipping output"));
		return;
	}

	if (!CleanDisplayText.IsEmpty())
	{
		if (const UWorld* World = ResolveSpeechWorld())
		{
			LastInteractiveOutputWorldTime = World->GetTimeSeconds();
		}
		OnLLMDisplayTextReceived.Broadcast(CleanDisplayText);
		UE_LOG(LogTemp, Warning, TEXT("[LLM] Broadcasting NPC subtitle: %s"), *CleanDisplayText);
		OnNPCSubtitleRequested.Broadcast(CleanDisplayText);
	}

	if (CleanSpeechText.IsEmpty())
	{
		return;
	}

    if (bUseVoicevoxTTS)
    {
        UVoicevoxTTSClient* ResolvedVoicevoxTTSClient = ResolveVoicevoxTTSClient();

        if (ResolvedVoicevoxTTSClient)
        {
            ResolvedVoicevoxTTSClient->OnVoicevoxSpeechFinished.RemoveDynamic(
                this,
                &ULLMClient::HandleVoicevoxSpeechFinished);
            ResolvedVoicevoxTTSClient->OnVoicevoxSpeechFinished.AddDynamic(
                this,
                &ULLMClient::HandleVoicevoxSpeechFinished);

            if (bInterruptCurrent && ResolvedVoicevoxTTSClient->IsVoicevoxSpeechActive())
            {
                UE_LOG(LogTemp, Warning, TEXT("[LLMClient] Immediate speech will interrupt active VOICEVOX playback."));
            }

            bCurrentAudioIsQueuedServiceSpeech = bQueuedServiceSpeech;
            ResolvedVoicevoxTTSClient->GenerateVoice(CleanSpeechText);

            if (bQueuedServiceSpeech)
            {
                if (UWorld* World = ResolveSpeechWorld())
                {
                    World->GetTimerManager().ClearTimer(QueuedSpeechFallbackTimerHandle);
                    FTimerDelegate TimerDelegate;
                    TimerDelegate.BindUObject(
                        this,
                        &ULLMClient::HandleQueuedSpeechFallbackTimeout,
                        ActiveQueuedSpeechGeneration);
                    const float FallbackSeconds = FMath::Max(QueuedSpeechFallbackTimeoutSeconds, 2.0f);
                    World->GetTimerManager().SetTimer(
                        QueuedSpeechFallbackTimerHandle,
                        TimerDelegate,
                        FallbackSeconds,
                        false);
                }
            }

            return;
        }

        UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] VoicevoxTTSClient not found. Falling back to existing TTS path."));
    }

    OnLLMResponseReceived.Broadcast(CleanSpeechText);

    if (bQueuedServiceSpeech)
    {
        if (UWorld* World = ResolveSpeechWorld())
        {
            FTimerDelegate TimerDelegate;
            TimerDelegate.BindUObject(
                this,
                &ULLMClient::HandleQueuedSpeechFallbackTimeout,
                ActiveQueuedSpeechGeneration);
            World->GetTimerManager().SetTimer(
                QueuedSpeechFallbackTimerHandle,
                TimerDelegate,
                FMath::Max(QueuedSpeechFallbackTimeoutSeconds, 2.0f),
                false);
        }
    }
}

void ULLMClient::HandleVoicevoxSpeechFinished()
{
    if (!bCurrentAudioIsQueuedServiceSpeech)
    {
        DispatchDeferredStrategyActionIfReady();
        OnInteractiveReplyFinished.Broadcast();
    }
    HandleQueuedVoicevoxSpeechFinished();
}

void ULLMClient::HandleQueuedVoicevoxSpeechFinished()
{
    if (!bCurrentAudioIsQueuedServiceSpeech || !bQueuedSpeechPlaying)
    {
        return;
    }

    if (ActiveQueuedSpeechGeneration != QueuedSpeechGeneration)
    {
        return;
    }

    if (UWorld* World = ResolveSpeechWorld())
    {
        World->GetTimerManager().ClearTimer(QueuedSpeechFallbackTimerHandle);
    }

    bCurrentAudioIsQueuedServiceSpeech = false;
    UE_LOG(LogTemp, Warning, TEXT("[SpeechQueue] Queued service audio finished."));

    const float SpeechGap = FMath::Max(MinSpeechGapSeconds, 0.0f);
    if (SpeechGap <= KINDA_SMALL_NUMBER)
    {
        HandleQueuedSpeechFinished(ActiveQueuedSpeechGeneration);
        return;
    }

    if (UWorld* World = ResolveSpeechWorld())
    {
        FTimerDelegate TimerDelegate;
        TimerDelegate.BindUObject(
            this,
            &ULLMClient::HandleQueuedSpeechFinished,
            ActiveQueuedSpeechGeneration);
        World->GetTimerManager().SetTimer(
            QueuedSpeechGapTimerHandle,
            TimerDelegate,
            SpeechGap,
            false);
        return;
    }

    HandleQueuedSpeechFinished(ActiveQueuedSpeechGeneration);
}

UVoicevoxTTSClient* ULLMClient::ResolveVoicevoxTTSClient()
{
    if (VoicevoxTTSClient)
    {
        return VoicevoxTTSClient;
    }

    if (AActor* OuterActor = GetTypedOuter<AActor>())
    {
        VoicevoxTTSClient = OuterActor->FindComponentByClass<UVoicevoxTTSClient>();
        if (VoicevoxTTSClient)
        {
            return VoicevoxTTSClient;
        }
    }

    if (UActorComponent* OuterComponent = GetTypedOuter<UActorComponent>())
    {
        if (AActor* OwnerActor = OuterComponent->GetOwner())
        {
            VoicevoxTTSClient = OwnerActor->FindComponentByClass<UVoicevoxTTSClient>();
            if (VoicevoxTTSClient)
            {
                return VoicevoxTTSClient;
            }
        }
    }

    return nullptr;
}
