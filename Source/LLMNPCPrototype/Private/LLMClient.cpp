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
#include "GameFramework/Actor.h"
#include "HAL/PlatformMisc.h"

namespace
{
    const FString OpenAIChatCompletionsUrl = TEXT("https://api.openai.com/v1/chat/completions");

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
        if (!ParsedAction.ActionType.Equals(TEXT("counter_then_return"), ESearchCase::IgnoreCase))
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
        if (!ContainsReturnTerm(UserMessage) || ContainsReturnToCounterTerm(UserMessage))
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
    ActiveLLMRequestId = 0;
    LastStrategyUserMessage = TEXT("");
}

void ULLMClient::SetVoicevoxTTSClient(UVoicevoxTTSClient* InVoicevoxTTSClient)
{
    VoicevoxTTSClient = InVoicevoxTTSClient;
}

void ULLMClient::SendDeepSeekMessage(const FString& UserMessage)
{
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
        TEXT(
            "You are Haruka, a gentle female Japanese restaurant staff NPC in a UE5 game.\n"
            "Always speak naturally in Japanese.\n"
            "Haruka is calm, polite, considerate, and good at noticing when the user may be hesitant, anxious, confused, or unsure.\n"
            "Reply warmly and thoughtfully, usually in 2 to 4 Japanese sentences rather than one very small phrase.\n"
            "First acknowledge the user's feeling, situation, or question.\n"
            "Then give a concrete suggestion, confirmation, or explanation.\n"
            "Finally, gently guide the user's next action.\n"
            "Do not include stage directions.\n"
            "Do not describe gestures or facial expressions in parentheses.\n"
            "Output only Haruka's spoken Japanese line.\n"
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
    InterruptCurrentVoicevoxSpeech();
    LastStrategyUserMessage = UserMessage;

    const int32 LocalRequestId = ++ActiveLLMRequestId;
    const FString OpenAIApiKey = GetOpenAIApiKey();
    if (OpenAIApiKey.IsEmpty())
    {
        return;
    }

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
        TEXT(
            "You are Haruka, a female restaurant staff NPC in a UE5 research demo.\n"
            "Your task is to infer the user's state, generate a response strategy, generate a natural Japanese reply, and optionally generate an environment action.\n"
            "You must output one valid JSON object only.\n"
            "Do not output markdown.\n"
            "Do not wrap the JSON in code fences.\n"
            "Do not explain outside JSON.\n"
            "\n"

            "Character personality:\n"
            "- Haruka is calm, gentle, considerate, and good at noticing the user's hesitation or anxiety.\n"
            "- She does not sound mechanical. She adds a small reassuring phrase so that the user feels comfortable.\n"
            "- She is polite like a Japanese restaurant staff member, but also slightly warm and approachable.\n"
            "- She should not be too casual, too dramatic, or too anime-like.\n"
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
            "- When reply is not empty, it should usually be 2 to 4 sentences.\n"
            "- Do not end with only one short phrase.\n"
            "- Do not reply only with phrases such as \"\u3054\u3086\u3063\u304F\u308A\u3069\u3046\u305E\" or \"\u304B\u3057\u3053\u307E\u308A\u307E\u3057\u305F\".\n"
            "- First, lightly acknowledge the user's feeling or situation.\n"
            "- Then, give a concrete suggestion, explanation, confirmation, or guidance.\n"
            "- Finally, gently help the user choose the next action.\n"
            "- The reply should not mention JSON, strategy, system instructions, or internal reasoning.\n"
            "\n"

            "Decision and action rules:\n"
            "- Include optional \"decision\": \"none|speak|act|act_and_speak\".\n"
            "- If decision is omitted, behavior is inferred from reply and actions.\n"
            "- Use only these decisions: none, speak, act, act_and_speak.\n"
            "- reply may be an empty string when no speech is needed.\n"
            "- actions may be used without speech.\n"
            "- If no speech is needed, set \"reply\" to \"\".\n"
            "- If no action is needed, use an empty actions array.\n"
            "- Use only these action types: greet, explain, recommend, place_order, guide_to_table, counter_then_return, table_route, action_sequence, none.\n"
            "- Do not invent unsupported action types.\n"
            "- For greeting or self-introduction, use greet with timing immediate or before_speech.\n"
            "- For explaining the menu, use explain with timing before_speech or during_speech.\n"
            "- For recommending a food item, use recommend with timing before_speech or during_speech.\n"
            "- For any multi-step plan or route, especially with sequence words such as \u5148, \u7136\u540E, \u518D, \u6700\u540E, \u9806\u756A\u306B, \u3057\u3066\u304B\u3089, or and then, do not output multiple independent actions.\n"
            "- For multi-step plans, output exactly one action: {\"type\":\"action_sequence\",\"timing\":\"immediate\",\"item\":\"JSON string of steps\",\"quantity\":0}.\n"
            "- The action_sequence item must be a JSON array string. Allowed step types are move_to_table, move_to_scene_tag, move_to_furniture_tag, move_to_furniture_id, move_near_player, wait, and face_player.\n"
            "- action_sequence example for a table route: item=\"[{\\\"type\\\":\\\"move_to_table\\\",\\\"target\\\":\\\"Table_04\\\"},{\\\"type\\\":\\\"move_to_table\\\",\\\"target\\\":\\\"Table_03\\\"},{\\\"type\\\":\\\"move_to_table\\\",\\\"target\\\":\\\"Table_02\\\"},{\\\"type\\\":\\\"move_to_table\\\",\\\"target\\\":\\\"Table_01\\\"}]\".\n"
            "- action_sequence example for counter then return to player: item=\"[{\\\"type\\\":\\\"move_to_scene_tag\\\",\\\"target\\\":\\\"counter\\\"},{\\\"type\\\":\\\"wait\\\",\\\"seconds\\\":1.0},{\\\"type\\\":\\\"move_near_player\\\",\\\"target\\\":\\\"player\\\"}]\".\n"
            "- action_sequence example for cleaning, watering, counter, then greeting: item=\"[{\\\"type\\\":\\\"move_to_scene_tag\\\",\\\"target\\\":\\\"cleaning\\\"},{\\\"type\\\":\\\"wait\\\",\\\"seconds\\\":2.0},{\\\"type\\\":\\\"move_to_scene_tag\\\",\\\"target\\\":\\\"watering\\\"},{\\\"type\\\":\\\"wait\\\",\\\"seconds\\\":1.5},{\\\"type\\\":\\\"move_to_scene_tag\\\",\\\"target\\\":\\\"counter\\\"},{\\\"type\\\":\\\"move_near_player\\\",\\\"target\\\":\\\"player\\\"},{\\\"type\\\":\\\"face_player\\\"}]\".\n"
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
            "- For guiding the user to a table or seat, use guide_to_table with timing immediate.\n"
            "- Use guide_to_table when the user asks Haruka to guide, lead, take them, go to a table, go to a seat, or choose a suitable table.\n"
            "- Table guidance examples include: \u53BB1\u53F7\u684C, \u5E26\u6211\u53BB1\u53F7\u684C, 1\u756A\u30C6\u30FC\u30D6\u30EB\u3078\u6848\u5185\u3057\u3066, \u6700\u8FD1\u7684\u684C\u5B50, nearest table, \u7A7A\u3044\u3066\u3044\u308B\u5E2D, \u9760\u7A97\u7684\u684C\u5B50, window table, \u89D2\u843D\u7684\u684C\u5B50, corner table, \u5427\u53F0\u65C1\u8FB9\u7684\u684C\u5B50, counter side table.\n"
            "- For guide_to_table, set quantity to 0.\n"
            "- For guide_to_table, set item using exactly these table hint rules: explicit 1\u53F7\u684C, Table 1, or 1\u756A\u30C6\u30FC\u30D6\u30EB -> \"Table_01\"; nearest table or \u6700\u8FD1\u7684\u684C\u5B50 -> \"nearest\"; empty table, available table, \u7A7A\u3044\u3066\u3044\u308B\u5E2D, or \u7A7A\u684C -> \"empty\"; window or \u9760\u7A97 -> \"window\"; corner or \u89D2\u843D -> \"corner\"; counter side or \u5427\u53F0\u65C1\u8FB9 -> \"counter_side\".\n"
            "- guide_to_table action format is {\"type\":\"guide_to_table\",\"timing\":\"immediate\",\"item\":\"table hint\",\"quantity\":0}.\n"
            "- When using guide_to_table, the reply may be one short natural Japanese sentence such as \"\u306F\u3044\u3001\u3054\u6848\u5185\u3057\u307E\u3059\u306D\u3002\" or \"\u3053\u3061\u3089\u306E\u304A\u5E2D\u3078\u3054\u6848\u5185\u3057\u307E\u3059\u3002\".\n"
            "- Do not use after_speech for greet, explain, or recommend.\n"
            "- For a confirmed user order, use place_order.\n"
            "- Use after_speech only for place_order if needed.\n"
            "- For recommend, set quantity to 0.\n"
            "- For explain and greet, set item to \"none\" and quantity to 0.\n"
            "- Use quantity only for place_order.\n"
            "- When the user asks for a recommendation, output exactly one recommend action, preferably for the single best item.\n"
            "- recommend example: {\"type\":\"recommend\",\"timing\":\"before_speech\",\"item\":\"\u30AB\u30EC\u30FC\u30E9\u30A4\u30B9\",\"quantity\":0}.\n"
            "- action timing may be immediate, before_speech, during_speech, or after_speech.\n"
            "- Use place_order only when the player clearly orders a specific menu item.\n"
            "- place_order action format is {\"type\":\"place_order\",\"timing\":\"after_speech\",\"item\":\"menu item\",\"quantity\":1}.\n"
            "- If the player only asks for a recommendation, do not place an order.\n"
            "- If the player is only hesitating or silent, do not place an order.\n"
            "- Available menu items are: \u30E9\u30FC\u30E1\u30F3, \u30AB\u30EC\u30FC\u30E9\u30A4\u30B9, \u30B3\u30FC\u30D2\u30FC, \u6C34, \u30B5\u30E9\u30C0.\n"
            "- Do not invent, mention, recommend, or offer unavailable menu items such as pasta, seafood, or desserts.\n"
            "- If the player asks for a recommendation, recommend only from the available menu.\n"
            "\n"
            "Required JSON format:\n"
            "{\n"
            "  \"decision\": \"none|speak|act|act_and_speak\",\n"
            "  \"strategy\": {\n"
            "    \"tone\": \"gentle|reassuring|calm|cheerful|direct\",\n"
            "    \"length\": \"short|normal|medium\",\n"
            "    \"priority\": \"comfort|order|explain|clarity|support|speed|task\",\n"
            "    \"reason\": \"Japanese reason explaining why this response style was chosen based on user state or behavior\"\n"
            "  },\n"
            "  \"reply\": \"Japanese reply, or empty string when no speech is needed\",\n"
            "  \"actions\": [\n"
            "    {\"type\":\"greet|explain|recommend|place_order|guide_to_table|counter_then_return|table_route|action_sequence|none\",\"timing\":\"immediate|before_speech|during_speech|after_speech\",\"item\":\"none, menu item, table hint, comma-separated table route, or JSON string steps for action_sequence\",\"return_table_id\":\"Table_01 or current_table when type is counter_then_return\",\"quantity\":0}\n"
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
    UserMsg->SetStringField(TEXT("content"), UserMessage);
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
    TSharedRef<TJsonReader<>> StrategyReader = TJsonReaderFactory<>::Create(CleanContent);

    if (!FJsonSerializer::Deserialize(StrategyReader, StrategyRoot) || !StrategyRoot.IsValid())
    {
        const FString ParseError = FString::Printf(
            TEXT("Failed to parse strategy JSON content: %s"),
            *CleanContent
        );

        UE_LOG(LogTemp, Error, TEXT("%s"), *ParseError);
        OnLLMErrorReceived.Broadcast(ParseError);
        return;
    }

    FString Reply = GetStringFieldOrDefault(StrategyRoot, TEXT("reply"), TEXT("")).TrimStartAndEnd();

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
            UE_LOG(LogTemp, Warning, TEXT("[Strategy] broadcasting action: %s, item=%s"), *ParsedAction.ActionType, *ParsedAction.Item);
            OnNPCActionRequested.Broadcast(ParsedAction.ActionType, ParsedAction.Timing, ParsedAction.Item, ParsedAction.Quantity);
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
        CleanContent
    );

    if (!Reply.IsEmpty())
    {
        HandleFinalReplyText(Reply);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[Strategy] empty reply, skipping speech"));
    }
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

    ResolvedVoicevoxTTSClient->StopVoicevoxSpeech();
    UE_LOG(LogTemp, Warning, TEXT("[LLM] interrupted current VOICEVOX speech before new request"));
}

void ULLMClient::HandleFinalReplyText(const FString& ReplyText)
{
    const FString CleanReplyText = SanitizeReplyTextForSpeechAndDisplay(ReplyText);

    OnLLMDisplayTextReceived.Broadcast(CleanReplyText);

    if (bUseVoicevoxTTS)
    {
        UVoicevoxTTSClient* ResolvedVoicevoxTTSClient = ResolveVoicevoxTTSClient();

        if (ResolvedVoicevoxTTSClient)
        {
            ResolvedVoicevoxTTSClient->GenerateVoice(CleanReplyText);
            return;
        }

        UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] VoicevoxTTSClient not found. Falling back to existing TTS path."));
    }

    OnLLMResponseReceived.Broadcast(CleanReplyText);
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
