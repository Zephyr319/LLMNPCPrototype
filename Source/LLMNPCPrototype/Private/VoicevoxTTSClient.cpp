#include "VoicevoxTTSClient.h"

#include "Audio.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "GameFramework/Actor.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Sound/SoundWaveProcedural.h"

bool UVoicevoxTTSClient::bGlobalVoicevoxWarmupCompleted = false;
bool UVoicevoxTTSClient::bGlobalVoicevoxWarmupInProgress = false;
TWeakObjectPtr<UVoicevoxTTSClient> UVoicevoxTTSClient::GlobalVoicevoxWarmupOwner;

UVoicevoxTTSClient::UVoicevoxTTSClient()
{
    PrimaryComponentTick.bCanEverTick = false;

    BaseUrl = TEXT("http://127.0.0.1:50021");
    SpeakerId = 17;
    VoicevoxWarmupDelaySeconds = 1.0f;
    LastGeneratedSound = nullptr;
    LastWarmupSound = nullptr;
    TargetAudioComponent = nullptr;
    OwnedAudioComponent = nullptr;
    bIsSynthesizingSentence = false;
    bIsAudioPlaying = false;
    bSpeechFinishNotificationPending = false;
    ActiveGenerationId = 0;
    CurrentPlaybackGenerationId = 0;
}

void UVoicevoxTTSClient::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClearPlaybackFinishTimer();
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(VoicevoxWarmupTimerHandle);
    }

    if (GlobalVoicevoxWarmupOwner.Get() == this && bGlobalVoicevoxWarmupInProgress && !bGlobalVoicevoxWarmupCompleted)
    {
        bGlobalVoicevoxWarmupInProgress = false;
        GlobalVoicevoxWarmupOwner.Reset();
    }

    Super::EndPlay(EndPlayReason);
}

void UVoicevoxTTSClient::GenerateVoice(const FString& Text)
{
    if (IsVoicevoxSpeechActive())
    {
        StopVoicevoxSpeech();
    }
    else
    {
        ++ActiveGenerationId;
        PendingSentenceQueue.Empty();
        PendingAudioQueue.Empty();
        PendingAudioDurations.Empty();
        bIsSynthesizingSentence = false;
        bIsAudioPlaying = false;
        bSpeechFinishNotificationPending = false;
        ClearPlaybackFinishTimer();
    }

    PendingSentenceQueue = SplitIntoSentences(Text);
    bSpeechFinishNotificationPending = PendingSentenceQueue.Num() > 0;
    for (const FString& Sentence : PendingSentenceQueue)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] sentence queued: %s"), *Sentence);
    }

    StartNextSentenceSynthesis();
}

void UVoicevoxTTSClient::WarmupVoicevox()
{
    if (bGlobalVoicevoxWarmupCompleted)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] Warm-up skipped: already completed."));
        return;
    }

    if (bGlobalVoicevoxWarmupInProgress)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] Warm-up skipped: already in progress."));
        return;
    }

    bGlobalVoicevoxWarmupInProgress = true;
    GlobalVoicevoxWarmupOwner = this;

    const float WarmupDelay = FMath::Max(VoicevoxWarmupDelaySeconds, 0.0f);
    if (UWorld* World = GetWorld())
    {
        if (WarmupDelay > KINDA_SMALL_NUMBER)
        {
            World->GetTimerManager().SetTimer(
                VoicevoxWarmupTimerHandle,
                this,
                &UVoicevoxTTSClient::StartDelayedVoicevoxWarmup,
                WarmupDelay,
                false);
            UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] Global warm-up scheduled after %.1fs."), WarmupDelay);
            return;
        }
    }

    StartDelayedVoicevoxWarmup();
}

void UVoicevoxTTSClient::SetTargetAudioComponent(UAudioComponent* InAudioComponent)
{
    if (TargetAudioComponent)
    {
        TargetAudioComponent->OnAudioFinished.RemoveDynamic(this, &UVoicevoxTTSClient::HandleTargetAudioFinished);
    }

    TargetAudioComponent = InAudioComponent;

    if (TargetAudioComponent)
    {
        TargetAudioComponent->OnAudioFinished.RemoveDynamic(this, &UVoicevoxTTSClient::HandleTargetAudioFinished);
        TargetAudioComponent->OnAudioFinished.AddDynamic(this, &UVoicevoxTTSClient::HandleTargetAudioFinished);
        PlayNextQueuedAudio();
    }
}

void UVoicevoxTTSClient::StopVoicevoxSpeech()
{
    ++ActiveGenerationId;
    PendingSentenceQueue.Empty();
    PendingAudioQueue.Empty();
    PendingAudioDurations.Empty();
    bIsSynthesizingSentence = false;
    bIsAudioPlaying = false;
    bSpeechFinishNotificationPending = false;
    ClearPlaybackFinishTimer();

    if (TargetAudioComponent)
    {
        TargetAudioComponent->Stop();
    }
    if (OwnedAudioComponent)
    {
        OwnedAudioComponent->Stop();
    }

    UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] speech queue stopped"));
}

bool UVoicevoxTTSClient::IsVoicevoxSpeechActive() const
{
    const bool bTargetAudioPlaying = TargetAudioComponent && TargetAudioComponent->IsPlaying();
    const bool bOwnedAudioPlaying = OwnedAudioComponent && OwnedAudioComponent->IsPlaying();
    return bIsSynthesizingSentence
        || bIsAudioPlaying
        || bSpeechFinishNotificationPending
        || PendingSentenceQueue.Num() > 0
        || PendingAudioQueue.Num() > 0
        || bTargetAudioPlaying
        || bOwnedAudioPlaying;
}

void UVoicevoxTTSClient::StartDelayedVoicevoxWarmup()
{
    if (bGlobalVoicevoxWarmupCompleted)
    {
        bGlobalVoicevoxWarmupInProgress = false;
        if (GlobalVoicevoxWarmupOwner.Get() == this)
        {
            GlobalVoicevoxWarmupOwner.Reset();
        }
        UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] Warm-up skipped: already completed."));
        return;
    }

    if (GlobalVoicevoxWarmupOwner.IsValid() && GlobalVoicevoxWarmupOwner.Get() != this)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] Warm-up skipped: another owner is running warm-up."));
        return;
    }

    bGlobalVoicevoxWarmupInProgress = true;
    GlobalVoicevoxWarmupOwner = this;
    StartAudioQuery(TEXT("\u3053\u3093\u306B\u3061\u306F\u3002"), false, true, ActiveGenerationId);
}

void UVoicevoxTTSClient::MarkGlobalWarmupSucceeded()
{
    bGlobalVoicevoxWarmupCompleted = true;
    bGlobalVoicevoxWarmupInProgress = false;
    GlobalVoicevoxWarmupOwner.Reset();
    UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] Global warm-up completed."));
}

void UVoicevoxTTSClient::MarkGlobalWarmupFailed()
{
    bGlobalVoicevoxWarmupInProgress = false;
    GlobalVoicevoxWarmupOwner.Reset();
    UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] Global warm-up failed."));
}

FString UVoicevoxTTSClient::BuildVoicevoxUrl(const FString& PathAndQuery) const
{
    FString SanitizedBaseUrl = BaseUrl;
    SanitizedBaseUrl.RemoveFromEnd(TEXT("/"));

    return SanitizedBaseUrl + PathAndQuery;
}

TArray<FString> UVoicevoxTTSClient::SplitIntoSentences(const FString& Text) const
{
    TArray<FString> Sentences;
    FString CurrentSentence;

    for (int32 Index = 0; Index < Text.Len(); ++Index)
    {
        const TCHAR Character = Text[Index];
        CurrentSentence.AppendChar(Character);

        const bool bIsSentenceDelimiter =
            Character == static_cast<TCHAR>(0x3002) ||
            Character == static_cast<TCHAR>(0xFF01) ||
            Character == static_cast<TCHAR>(0xFF1F) ||
            Character == TCHAR('!') ||
            Character == TCHAR('?');

        if (bIsSentenceDelimiter)
        {
            const FString TrimmedSentence = CurrentSentence.TrimStartAndEnd();
            if (!TrimmedSentence.IsEmpty())
            {
                Sentences.Add(TrimmedSentence);
            }
            CurrentSentence.Empty();
        }
    }

    const FString TrimmedSentence = CurrentSentence.TrimStartAndEnd();
    if (!TrimmedSentence.IsEmpty())
    {
        Sentences.Add(TrimmedSentence);
    }

    return Sentences;
}

void UVoicevoxTTSClient::StartNextSentenceSynthesis()
{
    if (bIsSynthesizingSentence || PendingSentenceQueue.Num() == 0)
    {
        TryBroadcastSpeechFinished();
        return;
    }

    const FString Sentence = PendingSentenceQueue[0];
    PendingSentenceQueue.RemoveAt(0);

    bIsSynthesizingSentence = true;
    UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] start sentence synthesis: %s"), *Sentence);

    StartAudioQuery(Sentence, true, false, ActiveGenerationId);
}

void UVoicevoxTTSClient::FinishSentenceSynthesis(int32 GenerationId)
{
    if (GenerationId != ActiveGenerationId)
    {
        return;
    }

    bIsSynthesizingSentence = false;
    StartNextSentenceSynthesis();
}

void UVoicevoxTTSClient::QueueGeneratedAudio(USoundWave* SoundWave, float PlaybackDuration)
{
    if (!SoundWave)
    {
        return;
    }

    PendingAudioQueue.Add(SoundWave);
    PendingAudioDurations.Add(PlaybackDuration);
    UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] audio queued"));

    if (!bIsAudioPlaying)
    {
        PlayNextQueuedAudio();
    }
}

void UVoicevoxTTSClient::PlayNextQueuedAudio()
{
    UAudioComponent* PlaybackAudioComponent = GetOrCreatePlaybackAudioComponent();
    if (!PlaybackAudioComponent || bIsAudioPlaying || PendingAudioQueue.Num() == 0)
    {
        TryBroadcastSpeechFinished();
        return;
    }

    USoundWave* SoundWave = PendingAudioQueue[0];
    float PlaybackDuration = PendingAudioDurations.Num() > 0 ? PendingAudioDurations[0] : 2.0f;
    PendingAudioQueue.RemoveAt(0);
    if (PendingAudioDurations.Num() > 0)
    {
        PendingAudioDurations.RemoveAt(0);
    }

    if (!SoundWave)
    {
        PlayNextQueuedAudio();
        return;
    }

    bIsAudioPlaying = true;
    CurrentPlaybackGenerationId = ActiveGenerationId;
    if (SoundWave->Duration > 0.0f)
    {
        PlaybackDuration = SoundWave->Duration;
    }
    else if (PlaybackDuration <= 0.0f)
    {
        PlaybackDuration = 2.0f;
    }

    StartPlaybackFinishTimer(PlaybackDuration + 0.1f);
    PlaybackAudioComponent->SetSound(SoundWave);
    PlaybackAudioComponent->Play();
    UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] play queued audio"));
}

UAudioComponent* UVoicevoxTTSClient::GetOrCreatePlaybackAudioComponent()
{
    if (TargetAudioComponent)
    {
        return TargetAudioComponent;
    }

    if (OwnedAudioComponent)
    {
        return OwnedAudioComponent;
    }

    UObject* ComponentOuter = GetOwner() ? static_cast<UObject*>(GetOwner()) : static_cast<UObject*>(this);
    OwnedAudioComponent = NewObject<UAudioComponent>(ComponentOuter);
    if (!OwnedAudioComponent)
    {
        return nullptr;
    }

    OwnedAudioComponent->bAutoActivate = false;
    OwnedAudioComponent->OnAudioFinished.RemoveDynamic(this, &UVoicevoxTTSClient::HandleTargetAudioFinished);
    OwnedAudioComponent->OnAudioFinished.AddDynamic(this, &UVoicevoxTTSClient::HandleTargetAudioFinished);

    if (AActor* OwnerActor = GetOwner())
    {
        OwnerActor->AddInstanceComponent(OwnedAudioComponent);
        OwnedAudioComponent->RegisterComponent();
    }
    else if (UWorld* World = GetWorld())
    {
        OwnedAudioComponent->RegisterComponentWithWorld(World);
    }

    UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] created internal speech audio component"));
    return OwnedAudioComponent;
}

void UVoicevoxTTSClient::TryBroadcastSpeechFinished()
{
    if (!bSpeechFinishNotificationPending)
    {
        return;
    }

    if (bIsSynthesizingSentence || bIsAudioPlaying || PendingSentenceQueue.Num() > 0 || PendingAudioQueue.Num() > 0)
    {
        return;
    }

    bSpeechFinishNotificationPending = false;
    UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] speech finished"));
    OnVoicevoxSpeechFinished.Broadcast();
}

void UVoicevoxTTSClient::StartPlaybackFinishTimer(float PlaybackDuration)
{
    ClearPlaybackFinishTimer();

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FTimerDelegate TimerDelegate;
    TimerDelegate.BindUObject(
        this,
        &UVoicevoxTTSClient::HandlePlaybackFinishedByTimer,
        CurrentPlaybackGenerationId
    );
    World->GetTimerManager().SetTimer(
        PlaybackFinishTimerHandle,
        TimerDelegate,
        FMath::Max(PlaybackDuration, 0.1f),
        false
    );
}

void UVoicevoxTTSClient::ClearPlaybackFinishTimer()
{
    UWorld* World = GetWorld();
    if (World && PlaybackFinishTimerHandle.IsValid())
    {
        World->GetTimerManager().ClearTimer(PlaybackFinishTimerHandle);
    }
}

float UVoicevoxTTSClient::EstimateDurationFromWavData(const TArray<uint8>& WavData) const
{
    FWaveModInfo WaveInfo;
    if (!WaveInfo.ReadWaveInfo(WavData.GetData(), WavData.Num()))
    {
        return 2.0f;
    }

    const int32 NumChannels = *WaveInfo.pChannels;
    const int32 SampleRate = *WaveInfo.pSamplesPerSec;
    const int32 BitsPerSample = *WaveInfo.pBitsPerSample;
    const int32 BytesPerSample = BitsPerSample / 8;
    if (NumChannels <= 0 || SampleRate <= 0 || BytesPerSample <= 0)
    {
        return 2.0f;
    }

    const float Duration = static_cast<float>(WaveInfo.SampleDataSize) / static_cast<float>(SampleRate * NumChannels * BytesPerSample);
    return Duration > 0.0f ? Duration : 2.0f;
}

void UVoicevoxTTSClient::HandleTargetAudioFinished()
{
    if (!bIsAudioPlaying)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] audio finished ignored because no queued audio is currently playing"));
        return;
    }

    ClearPlaybackFinishTimer();

    bIsAudioPlaying = false;
    UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] audio finished by component"));
    PlayNextQueuedAudio();
    TryBroadcastSpeechFinished();
}

void UVoicevoxTTSClient::HandlePlaybackFinishedByTimer(int32 GenerationId)
{
    if (GenerationId != ActiveGenerationId || !bIsAudioPlaying)
    {
        return;
    }

    bIsAudioPlaying = false;
    UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] audio finished by timer"));
    PlayNextQueuedAudio();
    TryBroadcastSpeechFinished();
}

void UVoicevoxTTSClient::StartAudioQuery(const FString& Text, bool bBroadcastSound, bool bIsWarmup, int32 GenerationId)
{
    if (Text.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[VOICEVOX] audio_query failed: text is empty"));
        if (bIsWarmup)
        {
            MarkGlobalWarmupFailed();
        }
        else
        {
            FinishSentenceSynthesis(GenerationId);
        }
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] audio_query start"));

    const FString EncodedText = FGenericPlatformHttp::UrlEncode(Text);
    const FString PathAndQuery = FString::Printf(
        TEXT("/audio_query?text=%s&speaker=%d"),
        *EncodedText,
        SpeakerId
    );

    TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(BuildVoicevoxUrl(PathAndQuery));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->OnProcessRequestComplete().BindUObject(
        this,
        &UVoicevoxTTSClient::OnAudioQueryResponseReceived,
        bBroadcastSound,
        bIsWarmup,
        GenerationId
    );
    if (!Request->ProcessRequest())
    {
        UE_LOG(LogTemp, Error, TEXT("[VOICEVOX] audio_query request failed to start"));
        if (bIsWarmup)
        {
            MarkGlobalWarmupFailed();
        }
        else
        {
            FinishSentenceSynthesis(GenerationId);
        }
    }
}

void UVoicevoxTTSClient::OnAudioQueryResponseReceived(
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool bWasSuccessful,
    bool bBroadcastSound,
    bool bIsWarmup,
    int32 GenerationId
)
{
    if (!bIsWarmup && GenerationId != ActiveGenerationId)
    {
        return;
    }

    if (!bWasSuccessful || !Response.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[VOICEVOX] audio_query failed"));
        if (bIsWarmup)
        {
            MarkGlobalWarmupFailed();
        }
        else
        {
            FinishSentenceSynthesis(GenerationId);
        }
        return;
    }

    const int32 ResponseCode = Response->GetResponseCode();
    if (ResponseCode != 200)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[VOICEVOX] audio_query failed: Code=%d, Body=%s"),
            ResponseCode,
            *Response->GetContentAsString()
        );
        if (bIsWarmup)
        {
            MarkGlobalWarmupFailed();
        }
        else
        {
            FinishSentenceSynthesis(GenerationId);
        }
        return;
    }

    const FString AudioQueryJson = Response->GetContentAsString();
    if (AudioQueryJson.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[VOICEVOX] audio_query failed: response body is empty"));
        if (bIsWarmup)
        {
            MarkGlobalWarmupFailed();
        }
        else
        {
            FinishSentenceSynthesis(GenerationId);
        }
        return;
    }

    StartSynthesis(AudioQueryJson, bBroadcastSound, bIsWarmup, GenerationId);
}

void UVoicevoxTTSClient::StartSynthesis(const FString& AudioQueryJson, bool bBroadcastSound, bool bIsWarmup, int32 GenerationId)
{
    UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] synthesis start"));

    const FString PathAndQuery = FString::Printf(TEXT("/synthesis?speaker=%d"), SpeakerId);

    TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(BuildVoicevoxUrl(PathAndQuery));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Accept"), TEXT("audio/wav"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetContentAsString(AudioQueryJson);
    Request->OnProcessRequestComplete().BindUObject(
        this,
        &UVoicevoxTTSClient::OnSynthesisResponseReceived,
        bBroadcastSound,
        bIsWarmup,
        GenerationId
    );
    if (!Request->ProcessRequest())
    {
        UE_LOG(LogTemp, Error, TEXT("[VOICEVOX] synthesis request failed to start"));
        if (bIsWarmup)
        {
            MarkGlobalWarmupFailed();
        }
        else
        {
            FinishSentenceSynthesis(GenerationId);
        }
    }
}

void UVoicevoxTTSClient::OnSynthesisResponseReceived(
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool bWasSuccessful,
    bool bBroadcastSound,
    bool bIsWarmup,
    int32 GenerationId
)
{
    if (!bIsWarmup && GenerationId != ActiveGenerationId)
    {
        return;
    }

    if (!bWasSuccessful || !Response.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[VOICEVOX] synthesis failed"));
        if (bIsWarmup)
        {
            MarkGlobalWarmupFailed();
        }
        else
        {
            FinishSentenceSynthesis(GenerationId);
        }
        return;
    }

    const int32 ResponseCode = Response->GetResponseCode();
    if (ResponseCode != 200)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[VOICEVOX] synthesis failed: Code=%d, Body=%s"),
            ResponseCode,
            *Response->GetContentAsString()
        );
        if (bIsWarmup)
        {
            MarkGlobalWarmupFailed();
        }
        else
        {
            FinishSentenceSynthesis(GenerationId);
        }
        return;
    }

    const TArray<uint8>& WavBytes = Response->GetContent();
    UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] wav bytes: %d"), WavBytes.Num());

    if (WavBytes.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[VOICEVOX] synthesis failed: wav bytes are empty"));
        if (bIsWarmup)
        {
            MarkGlobalWarmupFailed();
        }
        else
        {
            FinishSentenceSynthesis(GenerationId);
        }
        return;
    }

    USoundWave* SoundWave = CreateSoundWaveFromWavData(WavBytes);
    if (!SoundWave)
    {
        UE_LOG(LogTemp, Error, TEXT("[VOICEVOX] SoundWave creation failed"));
        if (bIsWarmup)
        {
            MarkGlobalWarmupFailed();
        }
        else
        {
            FinishSentenceSynthesis(GenerationId);
        }
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("[VOICEVOX] SoundWave created"));

    if (bIsWarmup)
    {
        LastWarmupSound = SoundWave;
        MarkGlobalWarmupSucceeded();
        return;
    }

    LastGeneratedSound = SoundWave;
    const float EstimatedPlaybackDuration = EstimateDurationFromWavData(WavBytes);

    if (bBroadcastSound)
    {
        QueueGeneratedAudio(LastGeneratedSound, EstimatedPlaybackDuration);
    }

    if (!bIsWarmup)
    {
        FinishSentenceSynthesis(GenerationId);
    }
}

USoundWave* UVoicevoxTTSClient::CreateSoundWaveFromWavData(const TArray<uint8>& WavData)
{
    FWaveModInfo WaveInfo;

    if (!WaveInfo.ReadWaveInfo(WavData.GetData(), WavData.Num()))
    {
        UE_LOG(LogTemp, Error, TEXT("[VOICEVOX] Failed to read wav info"));
        return nullptr;
    }

    const int32 NumChannels = *WaveInfo.pChannels;
    const int32 SampleRate = *WaveInfo.pSamplesPerSec;
    const int32 BitsPerSample = *WaveInfo.pBitsPerSample;
    const int32 PCMDataSize = WaveInfo.SampleDataSize;

    if (BitsPerSample != 16)
    {
        UE_LOG(LogTemp, Error, TEXT("[VOICEVOX] Only 16-bit PCM wav is supported. BitsPerSample=%d"), BitsPerSample);
        return nullptr;
    }

    USoundWaveProcedural* SoundWave = NewObject<USoundWaveProcedural>(this);

    SoundWave->SetSampleRate(SampleRate);
    SoundWave->NumChannels = NumChannels;
    SoundWave->Duration = static_cast<float>(PCMDataSize) / static_cast<float>(SampleRate * NumChannels * sizeof(int16));
    SoundWave->SoundGroup = SOUNDGROUP_Default;
    SoundWave->bLooping = false;

    SoundWave->QueueAudio(WaveInfo.SampleDataStart, PCMDataSize);

    return SoundWave;
}
