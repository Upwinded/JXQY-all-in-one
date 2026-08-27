#pragma once

#include <QtGlobal>

#include <functional>

class QThread;

// Registers a short-lived editor worker before QThread::start(). The
// registration is released only after the QThread object has been safely
// destroyed. QThread::finished can be emitted before platform-thread cleanup
// and thread-local destructors have completed.
void registerEditorBackgroundWorker(QThread* worker);

// Registers a formal-resource writer whose work function must return before
// the process may use the hard-exit path. The editor window may still close
// immediately; cancellation and waiting happen only after the application
// event loop has returned.
void registerEditorExitProtectedWriteWorker(
    QThread* worker,
    std::function<void()> requestCancellation);

quint64 activeEditorBackgroundWorkerCount();

// Call after the application event loop has returned and active child
// processes have been stopped. Formal-resource writers are cancelled and
// allowed to finish their publish/rollback work before any hard exit. Other
// detached workers still cause an immediate hard exit.
int finishEditorApplicationExit(int exitCode);
