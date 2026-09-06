#pragma once

#include <Qt>

namespace snow_canvas_runtime {

class ThreadAffinity final {
  public:
    ThreadAffinity();

    bool isOwnerThread() const;
    bool hasAccess(const char* operation) const;
    bool hasDestructionAccess(const char* operation) const;

  private:
    void warnWrongThread(const char* operation, const char* message) const;

    Qt::HANDLE m_ownerThreadId;
};

} // namespace snow_canvas_runtime
