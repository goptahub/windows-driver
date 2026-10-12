#pragma once

extern BOOLEAN ThreadCallbackRegistered;

VOID CreateThreadNotifyRoutine(
    HANDLE ProcessId,
    HANDLE ThreadId,
    BOOLEAN Create
);

