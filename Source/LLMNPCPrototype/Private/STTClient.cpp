#include "STTClient.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Async/Async.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

void USTTClient::AppendStringToBody(TArray<uint8>& Body, const FString& Text)
{
    FTCHARToUTF8 Converter(*Text);
    Body.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
}

void USTTClient::TranscribeAudioFile(const FString& FilePath)
{
    TranscribeAudioFileWithId(FilePath, -1);
}

void USTTClient::TranscribeAudioFileWithId(const FString& FilePath, int32 RequestId)
{
    UE_LOG(LogTemp, Warning, TEXT("STT TranscribeAudioFileWithId called. RequestId=%d, FilePath=%s"), RequestId, *FilePath);

    TArray<uint8> AudioData;
    if (!FFileHelper::LoadFileToArray(AudioData, *FilePath))
    {
        const FString ErrorMessage = FString::Printf(TEXT("Failed to load audio file: %s"), *FilePath);
        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnSTTErrorReceived.Broadcast(ErrorMessage);
        return;
    }

    const FString Url = TEXT("http://127.0.0.1:8001/transcribe");
    const FString Boundary = TEXT("----UE5WhisperBoundary7MA4YWxkTrZu0gW");

    TArray<uint8> Body;

    const FString FileName = FPaths::GetCleanFilename(FilePath);

    AppendStringToBody(Body, TEXT("--") + Boundary + TEXT("\r\n"));
    AppendStringToBody(
        Body,
        FString::Printf(
            TEXT("Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"),
            *FileName
        )
    );
    AppendStringToBody(Body, TEXT("Content-Type: audio/wav\r\n\r\n"));

    Body.Append(AudioData);

    AppendStringToBody(Body, TEXT("\r\n"));
    AppendStringToBody(Body, TEXT("--") + Boundary + TEXT("--\r\n"));

    TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("multipart/form-data; boundary=") + Boundary);
    Request->SetContent(Body);

    // faster-whisper small / medium can take longer.
    Request->SetTimeout(300.0f);

    Request->OnProcessRequestComplete().BindUObject(
        this,
        &USTTClient::OnSTTResponseReceivedWithId,
        RequestId
    );

    Request->ProcessRequest();

    UE_LOG(LogTemp, Warning, TEXT("STT request sent. RequestId=%d"), RequestId);
}

void USTTClient::TranscribeAudioFileWhenReady(
    const FString& FilePath,
    float TimeoutSeconds,
    float PollIntervalSeconds
)
{
    TranscribeReadyWithId(FilePath, -1, TimeoutSeconds, PollIntervalSeconds);
}


void USTTClient::TranscribeReadyWithId(
    const FString& FilePath,
    int32 RequestId,
    float TimeoutSeconds,
    float PollIntervalSeconds
)
{
    UE_LOG(LogTemp, Warning, TEXT("Waiting for audio file. RequestId=%d, FilePath=%s"), RequestId, *FilePath);

    TWeakObjectPtr<USTTClient> WeakThis(this);

    Async(EAsyncExecution::ThreadPool, [WeakThis, FilePath, RequestId, TimeoutSeconds, PollIntervalSeconds]()
        {
            IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

            const double StartTime = FPlatformTime::Seconds();
            int64 LastFileSize = -1;
            int32 StableCount = 0;

            while (FPlatformTime::Seconds() - StartTime < TimeoutSeconds)
            {
                if (PlatformFile.FileExists(*FilePath))
                {
                    const int64 CurrentFileSize = PlatformFile.FileSize(*FilePath);

                    // WAV header is about 44 bytes.
                    if (CurrentFileSize > 44)
                    {
                        if (CurrentFileSize == LastFileSize)
                        {
                            StableCount++;
                        }
                        else
                        {
                            StableCount = 0;
                            LastFileSize = CurrentFileSize;
                        }

                        // File size stayed stable. It is probably finished writing.
                        if (StableCount >= 1)
                        {
                            AsyncTask(ENamedThreads::GameThread, [WeakThis, FilePath, RequestId]()
                                {
                                    if (WeakThis.IsValid())
                                    {
                                        UE_LOG(
                                            LogTemp,
                                            Warning,
                                            TEXT("Audio file is ready. Start STT. RequestId=%d, FilePath=%s"),
                                            RequestId,
                                            *FilePath
                                        );

                                        WeakThis->TranscribeAudioFileWithId(FilePath, RequestId);
                                    }
                                });

                            return;
                        }
                    }
                }

                FPlatformProcess::Sleep(FMath::Max(0.01f, PollIntervalSeconds));
            }

            AsyncTask(ENamedThreads::GameThread, [WeakThis, FilePath, RequestId]()
                {
                    if (WeakThis.IsValid())
                    {
                        const FString ErrorMessage = FString::Printf(
                            TEXT("Timeout waiting for audio file. RequestId=%d, FilePath=%s"),
                            RequestId,
                            *FilePath
                        );

                        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
                        WeakThis->OnSTTErrorReceived.Broadcast(ErrorMessage);
                    }
                });
        });
}

void USTTClient::OnSTTResponseReceived(
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool bWasSuccessful
)
{
    OnSTTResponseReceivedWithId(Request, Response, bWasSuccessful, -1);
}

void USTTClient::OnSTTResponseReceivedWithId(
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool bWasSuccessful,
    int32 RequestId
)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        const FString ErrorMessage = FString::Printf(
            TEXT("STT request failed. RequestId=%d, bWasSuccessful=%s, ResponseValid=%s, RequestStatus=%d"),
            RequestId,
            bWasSuccessful ? TEXT("true") : TEXT("false"),
            Response.IsValid() ? TEXT("true") : TEXT("false"),
            Request.IsValid() ? static_cast<int32>(Request->GetStatus()) : -1
        );

        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnSTTErrorReceived.Broadcast(ErrorMessage);
        return;
    }

    const int32 ResponseCode = Response->GetResponseCode();
    const FString ResponseString = Response->GetContentAsString();

    UE_LOG(LogTemp, Warning, TEXT("STT response code: %d, RequestId=%d"), ResponseCode, RequestId);
    UE_LOG(LogTemp, Warning, TEXT("STT raw response: %s"), *ResponseString);

    if (ResponseCode != 200)
    {
        const FString ErrorMessage = FString::Printf(
            TEXT("STT server error. RequestId=%d, Code=%d, Body=%s"),
            RequestId,
            ResponseCode,
            *ResponseString
        );

        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnSTTErrorReceived.Broadcast(ErrorMessage);
        return;
    }

    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseString);

    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        const FString ErrorMessage = FString::Printf(
            TEXT("Failed to parse STT response JSON. RequestId=%d"),
            RequestId
        );

        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnSTTErrorReceived.Broadcast(ErrorMessage);
        return;
    }

    FString TranscribedText;
    if (!RootObject->TryGetStringField(TEXT("text"), TranscribedText))
    {
        const FString ErrorMessage = FString::Printf(
            TEXT("No text field found in STT response. RequestId=%d"),
            RequestId
        );

        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        OnSTTErrorReceived.Broadcast(ErrorMessage);
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("STT result. RequestId=%d, Text=%s"), RequestId, *TranscribedText);

    // Old event, kept for compatibility.
    OnSTTResultReceived.Broadcast(TranscribedText);

    // New event with RequestId.
    OnSTTResultReceivedWithId.Broadcast(RequestId, TranscribedText);
}

void USTTClient::DeleteFileIfExists(const FString& FilePath)
{
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    if (PlatformFile.FileExists(*FilePath))
    {
        const bool bDeleted = PlatformFile.DeleteFile(*FilePath);

        if (bDeleted)
        {
            UE_LOG(LogTemp, Warning, TEXT("Deleted old audio file: %s"), *FilePath);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to delete old audio file: %s"), *FilePath);
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Audio file does not exist, skip delete: %s"), *FilePath);
    }
}