#include "snow_canvas_runtime_thread_affinity.h"

#include <QThread>

namespace snow_canvas_runtime {

ThreadAffinity::ThreadAffinity() : m_ownerThreadId(QThread::currentThreadId()) {}

bool ThreadAffinity::isOwnerThread() const {
    return m_ownerThreadId == QThread::currentThreadId();
}

bool ThreadAffinity::hasAccess(const char* operation) const {
    if (isOwnerThread()) {
        return true;
    }

    warnWrongThread(
        operation,
        "called from the wrong thread; construct, use, and destroy a runtime on one thread");
    return false;
}

bool ThreadAffinity::hasDestructionAccess(const char* operation) const {
    if (isOwnerThread()) {
        return true;
    }

    warnWrongThread(operation, "called from the wrong thread; runtime clients cannot be detached "
                               "safely; construct, use, and destroy a runtime on one thread");
    return false;
}

void ThreadAffinity::warnWrongThread(const char* operation, const char* message) const {
    qWarning("SnowCanvasRuntime::%s %s", operation != nullptr ? operation : "<unknown>",
             message != nullptr ? message : "called from the wrong thread");
}

} // namespace snow_canvas_runtime
