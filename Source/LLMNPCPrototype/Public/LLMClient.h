#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Http.h"
#include "Dom/JsonValue.h"
#include "NPCAutonomyTypes.h"
#include "NPCMemoryTypes.h"
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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
    FOnAutonomyDecisionReceived,
    int32,
    RequestId,
    const FString&,
    SelectedAction,
    const FString&,
    Reason,
    const FString&,
    RawJson
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnAutonomyDecisionError,
    int32,
    RequestId,
    const FString&,
    ErrorMessage
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnMemoryAppraisalReceived,
    int32,
    RequestId,
    const FString&,
    RawJson
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnMemoryAppraisalError,
    int32,
    RequestId,
    const FString&,
    ErrorMessage
);

DECLARE_MULTICAST_DELEGATE(FOnInteractiveLLMRequestStartedNative);
DECLARE_MULTICAST_DELEGATE_OneParam(
    FOnInteractionMemoryExtractedNative,
    const FNPCInteractionMemorySignal&);

DECLARE_MULTICAST_DELEGATE_ThreeParams(
    FOnNPCSocialExpressionRequestedNative,
    const FString&,
    const FString&,
    float);

DECLARE_MULTICAST_DELEGATE_OneParam(
    FOnNPCEmbodiedResponseRequestedNative,
    const FNPCEmbodiedResponseSignal&);

DECLARE_MULTICAST_DELEGATE(FOnInteractiveReplyFinishedNative);

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

    UFUNCTION(BlueprintCallable, Category = "LLM|Social")
    int32 SendSocialOpportunityMessage(const FString& ContextJson);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "LLM|Social")
    bool IsInteractiveRequestPending() const;

    UFUNCTION(BlueprintCallable, Category = "LLM|Social")
    bool IsReadyForSocialOpportunity(float MinimumQuietSeconds);

    static ULLMClient* GetMostRecentInteractiveClient();

    UFUNCTION(BlueprintCallable, Category = "LLM|Autonomy")
    int32 SendAutonomyDecisionMessage(const FString& ContextJson);

    UFUNCTION(BlueprintCallable, Category = "LLM|Memory")
    int32 SendMemoryAppraisalMessage(const FString& ContextJson);

    UFUNCTION(BlueprintCallable, Category = "TTS")
    void SpeakDirectText(const FString& Text);

    UFUNCTION(BlueprintCallable, Category = "TTS")
    void SpeakDirectTextQueued(const FString& Text);

    UFUNCTION(BlueprintCallable, Category = "LLM|Subtitle")
    void ShowSilentDisplayText(const FString& Text);

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

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenAI|Usage")
    int32 LastAutonomyPromptTokens;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenAI|Usage")
    int32 LastAutonomyCompletionTokens;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenAI|Usage")
    int32 LastAutonomyCachedPromptTokens;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenAI|Usage")
    int32 LastAppraisalPromptTokens;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenAI|Usage")
    int32 LastAppraisalCompletionTokens;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenAI|Usage")
    int32 LastAppraisalCachedPromptTokens;

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnLLMResponseReceived OnLLMResponseReceived;

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnLLMDisplayTextReceived OnLLMDisplayTextReceived;

    UPROPERTY(BlueprintAssignable, Category = "LLM|Subtitle")
    FOnNPCSubtitleRequested OnNPCSubtitleRequested;

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnLLMErrorReceived OnLLMErrorReceived;

    UPROPERTY(BlueprintAssignable, Category = "LLM|Autonomy")
    FOnAutonomyDecisionReceived OnAutonomyDecisionReceived;

    UPROPERTY(BlueprintAssignable, Category = "LLM|Autonomy")
    FOnAutonomyDecisionError OnAutonomyDecisionError;

    UPROPERTY(BlueprintAssignable, Category = "LLM|Memory")
    FOnMemoryAppraisalReceived OnMemoryAppraisalReceived;

    UPROPERTY(BlueprintAssignable, Category = "LLM|Memory")
    FOnMemoryAppraisalError OnMemoryAppraisalError;

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnStrategyResultReceived OnStrategyResultReceived;

    UPROPERTY(BlueprintAssignable, Category = "LLM")
    FOnNPCActionRequested OnNPCActionRequested;

    static FOnInteractiveLLMRequestStartedNative OnAnyInteractiveRequestStarted;
    static FOnInteractionMemoryExtractedNative OnInteractionMemoryExtracted;
    static FOnNPCSocialExpressionRequestedNative OnNPCSocialExpressionRequested;
    static FOnNPCEmbodiedResponseRequestedNative OnNPCEmbodiedResponseRequested;
    static FOnInteractiveReplyFinishedNative OnInteractiveReplyFinished;

private:
    static TWeakObjectPtr<ULLMClient> MostRecentInteractiveClient;

    TArray<TSharedPtr<FJsonValue>> ConversationHistory;
    int32 ActiveLLMRequestId;
    int32 ActiveAppraisalRequestId;
    FString LastStrategyUserMessage;
    bool bInteractiveRequestPending;
    double LastInteractiveOutputWorldTime;
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

    void OnSocialOpportunityResponseReceived(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful,
        int32 RequestId
    );

    void OnAutonomyResponseReceived(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful,
        int32 RequestId
    );

    void OnMemoryAppraisalResponseReceived(
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
    void PlaySpeechAndDisplayInternal(
        const FString& DisplayText,
        const FString& SpeechText,
        bool bQueuedServiceSpeech,
        bool bInterruptCurrent);
    void HandleQueuedSpeechFinished(int32 Generation);
    void HandleQueuedSpeechFallbackTimeout(int32 Generation);
    UWorld* ResolveSpeechWorld() const;
    static bool IsServiceSpeechDispatchActive();

    UFUNCTION()
    void HandleVoicevoxSpeechFinished();

    void HandleQueuedVoicevoxSpeechFinished();
};
