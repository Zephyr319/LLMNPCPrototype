#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/AudioComponent.h"
#include "Http.h"
#include "Sound/SoundWave.h"
#include "TimerManager.h"
#include "VoicevoxTTSClient.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVoicevoxSoundGenerated, USoundWave*, SoundWave);

UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent))
class LLMNPCPROTOTYPE_API UVoicevoxTTSClient : public UActorComponent
{
    GENERATED_BODY()

public:
    UVoicevoxTTSClient();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VOICEVOX")
    FString BaseUrl;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VOICEVOX")
    int32 SpeakerId;

    UPROPERTY(BlueprintAssignable, Category = "VOICEVOX")
    FOnVoicevoxSoundGenerated OnVoicevoxSoundGenerated;

    UFUNCTION(BlueprintCallable, Category = "VOICEVOX")
    void GenerateVoice(const FString& Text);

    UFUNCTION(BlueprintCallable, Category = "VOICEVOX")
    void WarmupVoicevox();

    UFUNCTION(BlueprintCallable, Category = "VOICEVOX")
    void SetTargetAudioComponent(UAudioComponent* InAudioComponent);

    UFUNCTION(BlueprintCallable, Category = "VOICEVOX")
    void StopVoicevoxSpeech();

private:
    UPROPERTY()
    USoundWave* LastGeneratedSound;

    UPROPERTY()
    USoundWave* LastWarmupSound;

    UPROPERTY()
    UAudioComponent* TargetAudioComponent;

    UPROPERTY()
    TArray<FString> PendingSentenceQueue;

    UPROPERTY()
    TArray<USoundWave*> PendingAudioQueue;

    TArray<float> PendingAudioDurations;
    FTimerHandle PlaybackFinishTimerHandle;

    bool bIsSynthesizingSentence;
    bool bIsAudioPlaying;
    int32 ActiveGenerationId;
    int32 CurrentPlaybackGenerationId;

    FString BuildVoicevoxUrl(const FString& PathAndQuery) const;

    TArray<FString> SplitIntoSentences(const FString& Text) const;
    void StartNextSentenceSynthesis();
    void FinishSentenceSynthesis(int32 GenerationId);
    void QueueGeneratedAudio(USoundWave* SoundWave, float PlaybackDuration);
    void PlayNextQueuedAudio();
    void StartPlaybackFinishTimer(float PlaybackDuration);
    void ClearPlaybackFinishTimer();
    float EstimateDurationFromWavData(const TArray<uint8>& WavData) const;

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
