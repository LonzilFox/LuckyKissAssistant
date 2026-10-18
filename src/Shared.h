#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>
#include <cstddef>

static constexpr unsigned int LKA_MAGIC = 0x35414B4C; // LKA5
static constexpr unsigned int LKA_ABI = 5;
static constexpr wchar_t LKA_MAPPING_PREFIX[] = L"Local\\LuckyKissAssistantV5_";
struct SharedState {
    unsigned int magic;                // 0
    unsigned int abi;                  // 4
    volatile LONG diceMode;            // 8: 0 off 1 selected 2 manual
    volatile LONG desiredFace;         // 12
    volatile LONG gameFace;            // 16: event-observed, never polled
    volatile LONG goldSequence;        // 20
    volatile LONG goldRuntimeSequence; // 24
    volatile LONG goldSavedSequence;   // 28: GameData synchronization requested
    volatile LONG requestedGold;       // 32
    volatile LONG observedGold;        // 36
    volatile LONG diceIntercepts;      // 40
    volatile LONG diceForced;          // 44
    volatile LONG initialized;         // 48: 1 collect 2 refresh 4 select 8 selectInstant 16 UI 32 allStopped 64 finish
    volatile LONG ready;               // 52
    volatile LONG lastError;           // 56
    volatile LONG gamePid;             // 60
    char status[256];                  // 64
    char report[512];                  // 320
    volatile LONG controllerObserved;  // 832
    volatile LONG selectedReads;       // 836: selector *events*, not worker polls
    volatile LONG bankReads;           // 840: actual GameData read attempts
    volatile LONG bankWrites;          // 844
    volatile LONG finalCalls;          // 848: collect callbacks
    volatile LONG stoppedCalls;        // 852: refresh callbacks
    volatile LONG topCalls;            // 856: supported dice list size
    volatile LONG forceMismatch;       // 860
    volatile LONG selectCalls;         // 864
    volatile LONG selectInstantCalls;  // 868
    volatile LONG goldDataStatus;      // 872: 0 no method; 1 attempted; 2 succeeded; -1 exception; -2 unsupported
    volatile LONG goldDataReadback;    // 876: -1 unavailable, other means value
    volatile LONG goldRefreshCalls;    // 880
    volatile LONG badDiceCollection;   // 884
    volatile LONG hookError;           // 888
    volatile LONG gameFaceSource;      // 892: 1 SelectValue 2 Instant 3 controller field
    volatile LONG allStoppedCalls;     // 896
    volatile LONG finishCalls;         // 900
    volatile LONG featureSequence;     // 904
    volatile LONG featureIndex;        // 908
    volatile LONG featureValueBits;    // 912
    volatile LONG featureApplied;      // 916
    volatile LONG featureStatus;       // 920
    volatile LONG featureReadback;     // 924
    volatile LONG featureType;         // 928
    volatile LONG featureWrites;       // 932
    volatile LONG featuresAvailable;   // 936
    volatile LONG dataSeen;            // 940
    volatile LONG quietExit;           // 944
    volatile LONG workerIdle;          // 948: 0 processing, 1 waiting unattached, 2 terminated
    volatile LONG snapshotSequence;    // 952
    volatile LONG snapshotApplied;     // 956
    volatile LONG snapshotValues[32];  // 960
    volatile LONG snapshotTypes[32];   // 1088
    char featureMessage[192];          // 1216
};
static_assert(offsetof(SharedState, status)==64 && offsetof(SharedState,featureMessage)==1216 && sizeof(SharedState)==1408, "IPC ABI mismatch");
inline void MakeMappingName(DWORD pid, wchar_t (&buf)[100]) {
    swprintf_s(buf, L"%ls%lu", LKA_MAPPING_PREFIX, (unsigned long)pid);
}
