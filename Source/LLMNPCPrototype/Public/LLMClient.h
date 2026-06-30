#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Http.h"
#include "Dom/JsonValue.h"
#include "TimerManager.h"
#include "LLMClient.generated.h"

class UVoicevoxTTSClient;
class UWorld;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnLLMResponseReceived,
    const FString&,
    ResponseText
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnLLMDisplayTextReceived,
    const FString&,
    DisplayText
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnNPCSubtitleRequested,
    const FString&,
    Text
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnLLMErrorReceived,
    const FString&,
    ErrorMessage
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_NineParams(
    FOnStrategyResultReceived,
    const FString&,
    Reply,
    const FString&,
    StrategyTone,
    const FString&,
    StrategyLength,
    const FString&,
    StrategyPriority,
    const FString&,
    StrategyReason,
    const FString&,
    ActionName,
    const FString&,
    ActionItem,
    int32,
    ActionQuantity,
    const FString&,
    RawJson
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
    FOnNPCActionRequested,
    const FString&,
    ActionType,
    const FString&,
    Timing,
    const FString&,
    Item,
    int32,
    Quantity
);

UCLASS(Blueprintable)
class LLMNPCPROTOTYPE_API ULLMClient : public UObject
{
    GENERATED_BODY()

public:
    ULLMClient();

    UFUNCTION(BlueprintCallable, Category = "LLM")
    void SendDeepSeekMessage(const FString& UserMessage);

    UFUNCTION(BlueprintCallable, Category = "LLM")
    void SendStrategyMessage(const FString& UserMessage);

    UFUNCTION(BlueprintCallable, Category = "TTS")
    void SpeakDirectText(const FString& Text);

    UFUNCTION(BlueprintCallable, Category = "TTS")
    void SpeakDirectTextQueued(const FString& Text);

    UFUNCTION(BlueprintCallable, Category = "TTS")
    void ClearQueuedSpeech();

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "TTS")
    bool IsQueuedSpeechPlaying() const;

    UFUNCTION(BlueprintCallable, Category = "TTS")
    void SetVoicevoxTTSClient(UVoicevoxTTSClient* InVoicevoxTTSClient);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TTS")
    bool bUseVoicevoxTTS;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TTS")
    UVoicevoxTTSClient* VoicevoxTTSClient;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TTS|Speech Queue")
    float MinSpeechGapSeconds;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TTS|Speech Queue")
    float QueuedSpeechFallbackTimeoutSeconds;

    static void BeginServiceSpeechDispatch();
    static void EndServiceSpeechDispatch();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenAI")
    FString OpenAIModelName;

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnLLMResponseReceived OnLLMResponseReceived;

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnLLMDisplayTextReceived OnLLMDisplayTextReceived;

    UPROPERTY(BlueprintAssignable, Category = "LLM|Subtitle")
    FOnNPCSubtitleRequested OnNPCSubtitleRequested;

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnLLMErrorReceived OnLLMErrorReceived;

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnStrategyResultReceived OnStrategyResultReceived;

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnNPCActionRequested OnNPCActionRequested;

private:
    TArray<TSharedPtr<FJsonValue>> ConversationHistory;
    int32 ActiveLLMRequestId;
    FString LastStrategyUserMessage;
    bool bQueuedSpeechPlaying;
    bool bCurrentAudioIsQueuedServiceSpeech;
    TArray<FString> PendingServiceSpeechQueue;
    FTimerHandle QueuedSpeechFallbackTimerHandle;
    FTimerHandle QueuedSpeechGapTimerHandle;
    int32 QueuedSpeechGeneration;
    int32 ActiveQueuedSpeechGeneration;
    bool bHasDeferredStrategyAction;
    FString DeferredStrategyActionType;
    FString DeferredStrategyActionTiming;
    FString DeferredStrategyActionItem;
    int32 DeferredStrategyActionQuantity;
    int32 DeferredStrategyRequestId;

    void OnOpenAINormalResponseReceived(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful,
        int32 RequestId
    );

    void OnStrategyResponseReceived(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful,
        int32 RequestId
    );

    void InterruptCurrentVoicevoxSpeech();
    void HandleFinalReplyText(const FString& ReplyText);
    FString SanitizeReplyTextForSpeechAndDisplay(const FString& RawText) const;
    UVoicevoxTTSClient* ResolveVoicevoxTTSClient();
    void CancelDeferredStrategyAction(bool bLogCancellation);
    bool ShouldDeferGuideActionUntilReplyFinished(const FString& ActionType, const FString& ReplyText);
    void StoreDeferredStrategyAction(const FString& ActionType, const FString& Timing, const FString& Item, int32 Quantity, int32 RequestId);
    void DispatchDeferredStrategyActionIfReady();
    void TryPlayNextQueuedSpeech();
    void PlaySpeechInternal(const FString& ReplyText, bool bQueuedServiceSpeech, bool bInterruptCurrent);
    void HandleQueuedSpeechFinished(int32 Generation);
    void HandleQueuedSpeechFallbackTimeout(int32 Generation);
    UWorld* ResolveSpeechWorld() const;
    static bool IsServiceSpeechDispatchActive();

    UFUNCTION()
    void HandleVoicevoxSpeechFinished();

    void HandleQueuedVoicevoxSpeechFinished();
};
