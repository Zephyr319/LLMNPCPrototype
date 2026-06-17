#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Http.h"
#include "Sound/SoundWave.h"
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

private:
    UPROPERTY()
    USoundWave* LastGeneratedSound;

    UPROPERTY()
    USoundWave* LastWarmupSound;

    FString BuildVoicevoxUrl(const FString& PathAndQuery) const;

    void StartAudioQuery(const FString& Text, bool bBroadcastSound, bool bIsWarmup);
    void OnAudioQueryResponseReceived(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful,
        bool bBroadcastSound,
        bool bIsWarmup
    );

    void StartSynthesis(const FString& AudioQueryJson, bool bBroadcastSound, bool bIsWarmup);
    void OnSynthesisResponseReceived(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful,
        bool bBroadcastSound,
        bool bIsWarmup
    );

    USoundWave* CreateSoundWaveFromWavData(const TArray<uint8>& WavData);
};
