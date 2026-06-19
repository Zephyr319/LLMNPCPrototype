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
}

ULLMClient::ULLMClient()
{
    bUseVoicevoxTTS = true;
    VoicevoxTTSClient = nullptr;
    OpenAIModelName = TEXT("gpt-4o-mini");
    ActiveLLMRequestId = 0;
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
            "- Do not reply only with phrases such as \"ごゆっくりどうぞ\" or \"かしこまりました\".\n"
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
            "- Use only these action types: greet, explain, recommend, place_order, none.\n"
            "- Do not invent unsupported action types.\n"
            "- For greeting or self-introduction, use greet with timing immediate or before_speech.\n"
            "- For explaining the menu, use explain with timing before_speech or during_speech.\n"
            "- For recommending a food item, use recommend with timing before_speech or during_speech.\n"
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
            "    {\"type\":\"greet|explain|recommend|place_order|none\",\"timing\":\"immediate|before_speech|during_speech|after_speech\",\"item\":\"none or menu item\",\"quantity\":0}\n"
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

            FString Timing = GetStringFieldOrDefault(ActionObject, TEXT("timing"), TEXT("immediate"));
            if (Timing.IsEmpty())
            {
                Timing = TEXT("immediate");
            }

            FString Item = GetStringFieldOrDefault(ActionObject, TEXT("item"), TEXT("none"));
            int32 Quantity = 0;

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

            if (ActionObject->HasField(TEXT("args")))
            {
                TSharedPtr<FJsonObject> ArgsObject = ActionObject->GetObjectField(TEXT("args"));
                if (ArgsObject.IsValid())
                {
                    if (Item.Equals(TEXT("none"), ESearchCase::IgnoreCase) || Item.IsEmpty())
                    {
                        Item = GetStringFieldOrDefault(ArgsObject, TEXT("item"), TEXT("none"));
                    }

                    if (Quantity == 0)
                    {
                        Quantity = GetQuantityFromArgs(ArgsObject);
                    }
                }
            }

            UE_LOG(LogTemp, Warning, TEXT("[Strategy] action parsed: type=%s, timing=%s, item=%s, quantity=%d"), *ActionType, *Timing, *Item, Quantity);

            const bool bIsNoneAction = ActionType.Equals(TEXT("none"), ESearchCase::IgnoreCase);
            const bool bIsPlaceOrderAction =
                ActionType.Equals(TEXT("place_order"), ESearchCase::IgnoreCase) ||
                ActionType.Equals(TEXT("PlaceOrder"), ESearchCase::IgnoreCase);

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

            if (Timing.Equals(TEXT("after_speech"), ESearchCase::IgnoreCase))
            {
                UE_LOG(LogTemp, Warning, TEXT("[Strategy] after_speech action logged for later handling: type=%s, item=%s, quantity=%d"), *ActionType, *Item, Quantity);
                continue;
            }

            const bool bShouldBroadcastBeforeSpeech =
                Timing.Equals(TEXT("immediate"), ESearchCase::IgnoreCase) ||
                Timing.Equals(TEXT("before_speech"), ESearchCase::IgnoreCase) ||
                Timing.Equals(TEXT("during_speech"), ESearchCase::IgnoreCase);

            if (!bIsNoneAction && !bIsPlaceOrderAction && bShouldBroadcastBeforeSpeech)
            {
                OnNPCActionRequested.Broadcast(ActionType, Timing, Item, Quantity);
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
        StrategyReason,
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
