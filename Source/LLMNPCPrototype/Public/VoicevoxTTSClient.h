#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/AudioComponent.h"
#include "Http.h"
#include "Sound/SoundWave.h"
#include "TimerManager.h"
#include "VoicevoxTTSClient.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVoicevoxSoundGenerated, USoundWave*, SoundWave);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnVoicevoxSpeechFinished);

UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent))
class LLMNPCPROTOTYPE_API UVoicevoxTTSClient : public UActorComponent
{
    GENERATED_BODY()

public:
    UVoicevoxTTSClient();

protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VOICEVOX")
    FString BaseUrl;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VOICEVOX")
    int32 SpeakerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VOICEVOX")
    float VoicevoxWarmupDelaySeconds;

    UPROPERTY(BlueprintAssignable, Category = "VOICEVOX")
    FOnVoicevoxSoundGenerated OnVoicevoxSoundGenerated;

    UPROPERTY(BlueprintAssignable, Category = "VOICEVOX")
    FOnVoicevoxSpeechFinished OnVoicevoxSpeechFinished;

    UFUNCTION(BlueprintCallable, Category = "VOICEVOX")
    void GenerateVoice(const FString& Text);

    UFUNCTION(BlueprintCallable, Category = "VOICEVOX")
    void WarmupVoicevox();

    UFUNCTION(BlueprintCallable, Category = "VOICEVOX")
    void SetTargetAudioComponent(UAudioComponent* InAudioComponent);

    UFUNCTION(BlueprintCallable, Category = "VOICEVOX")
    void StopVoicevoxSpeech();

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VOICEVOX")
    bool IsVoicevoxSpeechActive() const;

private:
    UPROPERTY()
    USoundWave* LastGeneratedSound;

    UPROPERTY()
    USoundWave* LastWarmupSound;

    UPROPERTY()
    UAudioComponent* TargetAudioComponent;

    UPROPERTY()
    UAudioComponent* OwnedAudioComponent;

    UPROPERTY()
    TArray<FString> PendingSentenceQueue;

    UPROPERTY()
    TArray<USoundWave*> PendingAudioQueue;

    TArray<float> PendingAudioDurations;
    FTimerHandle PlaybackFinishTimerHandle;
    FTimerHandle VoicevoxWarmupTimerHandle;

    bool bIsSynthesizingSentence;
    bool bIsAudioPlaying;
    bool bSpeechFinishNotificationPending;
    int32 ActiveGenerationId;
    int32 CurrentPlaybackGenerationId;

    static bool bGlobalVoicevoxWarmupCompleted;
    static bool bGlobalVoicevoxWarmupInProgress;
    static TWeakObjectPtr<UVoicevoxTTSClient> GlobalVoicevoxWarmupOwner;

    FString BuildVoicevoxUrl(const FString& PathAndQuery) const;

    TArray<FString> SplitIntoSentences(const FString& Text) const;
    void StartNextSentenceSynthesis();
    void FinishSentenceSynthesis(int32 GenerationId);
    void QueueGeneratedAudio(USoundWave* SoundWave, float PlaybackDuration);
    void PlayNextQueuedAudio();
    UAudioComponent* GetOrCreatePlaybackAudioComponent();
    void TryBroadcastSpeechFinished();
    void StartPlaybackFinishTimer(float PlaybackDuration);
    void ClearPlaybackFinishTimer();
    float EstimateDurationFromWavData(const TArray<uint8>& WavData) const;
    void StartDelayedVoicevoxWarmup();
    void MarkGlobalWarmupSucceeded();
    void MarkGlobalWarmupFailed();

    UFUNCTION()
    void HandleTargetAudioFinished();

    void HandlePlaybackFinishedByTimer(int32 GenerationId);

    void StartAudioQuery(const FString& Text, bool bBroadcastSound, bool bIsWarmup, int32 GenerationId);
    void OnAudioQueryResponseReceived(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful,
        bool bBroadcastSound,
        bool bIsWarmup,
        int32 GenerationId
    );

    void StartSynthesis(const FString& AudioQueryJson, bool bBroadcastSound, bool bIsWarmup, int32 GenerationId);
    void OnSynthesisResponseReceived(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful,
        bool bBroadcastSound,
        bool bIsWarmup,
        int32 GenerationId
    );

    USoundWave* CreateSoundWaveFromWavData(const TArray<uint8>& WavData);
};
