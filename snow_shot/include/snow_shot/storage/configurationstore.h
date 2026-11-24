#ifndef SNOW_SHOT_STORAGE_CONFIGURATIONSTORE_H
#define SNOW_SHOT_STORAGE_CONFIGURATIONSTORE_H

#include "snow_shot/storage/storageresult.h"

#include "snow_shot/mouseoperations.h"
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QMutex>
#include <QRecursiveMutex>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>
#include <functional>
#include <memory>

class QThreadPool;

namespace snow_shot::storage {
enum class ConfigurationCompatibility {
    Current,
    RecoveredDefaults,
    FutureVersion,
    Unavailable,
};

enum class ConfigurationDefaults { Standard, Portable };
enum class ConfigurationWriteOrigin { Explicit, Reset, Import, ImportDefaults, Remote, Internal };

class ConfigurationStore final : public QObject {
    Q_OBJECT

  public:
    ConfigurationStore(QString configurationFile, bool readAvailable, bool writeAvailable,
                       int debounceMilliseconds = 1000, QObject* parent = nullptr,
                       ConfigurationDefaults defaults = ConfigurationDefaults::Standard);
    ~ConfigurationStore() override;

    [[nodiscard]] static int currentSchemaVersion();

    [[nodiscard]] QJsonValue value(const QString& key) const;
    // Defaults belong to this storage mode; saved preferences take precedence.
    [[nodiscard]] QJsonValue defaultValue(const QString& key) const;
    [[nodiscard]] QMap<QString, QJsonValue> snapshot() const;
    [[nodiscard]] quint64 revision() const;
    [[nodiscard]] mouse::Bindings mouseBindings() const;
    [[nodiscard]] std::optional<mouse::Action> mouseAction(mouse::Category category,
                                                           mouse::Binding binding) const;
    // Serializes the revision check with all configuration writers, including
    // nested writes made by runtime settings adapters.
    bool mutateIfRevision(quint64 expectedRevision, const std::function<bool()>& mutation,
                          bool* conflict = nullptr);
    // On the store thread, deliver earlier worker commits before changing their observer's
    // context. Concurrent writers resume after the transition completes.
    void afterPendingChanges(const std::function<void()>& transition);
    // Record intent under the mutation lock. A true result schedules the journal snapshot
    // with the preferences. Both callbacks can run on a worker thread.
    using CloudSyncJournal =
        std::function<bool(ConfigurationWriteOrigin, const QMap<QString, QJsonValue>&,
                           const QMap<QString, QJsonValue>&)>;
    void setCloudSyncJournal(CloudSyncJournal journal, std::function<QJsonObject()> snapshot);
    // Include a status-only change in the next flush without scheduling a disk write.
    bool deferCloudSyncStatus();
    [[nodiscard]] bool isDirty() const;
    [[nodiscard]] bool isWritable() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] ConfigurationCompatibility compatibility() const;

    bool setValue(const QString& key, const QJsonValue& value,
                  ConfigurationWriteOrigin origin = ConfigurationWriteOrigin::Explicit);
    bool setValues(const QMap<QString, QJsonValue>& values,
                   ConfigurationWriteOrigin origin = ConfigurationWriteOrigin::Explicit);
    [[nodiscard]] QJsonObject cloudSyncState() const;
    // Keep sync metadata and downloaded preferences in the same atomic file write.
    bool setCloudSyncState(const QJsonObject& state,
                           const QMap<QString, QJsonValue>& remoteValues = {});
    // Replaces the whole configuration with `values` overlaid on schema
    // defaults for this store, using the same per-key salvage rules as loading a
    // configuration file. Keys absent from `values` revert to defaults.
    // Unknown keys are ignored. `schemaVersion` is the version the overlay
    // was written against: older versions are upgraded, and versions newer
    // than this application are rejected. Pass 0 to treat the overlay as
    // already at the current schema. Returns false when storage is
    // read-only or `schemaVersion` is from a newer application.
    bool applySnapshot(const QMap<QString, QJsonValue>& values, int schemaVersion = 0,
                       const QSet<QString>* explicitlyImported = nullptr);
    [[nodiscard]] StorageResult flushNow();
    void flushAsync(QObject* receiver, std::function<void(StorageResult)> completion);
    void suspendWrites(bool suspended);
    void relocate(const QString& configurationDirectory);

  signals:
    void writesResumed();
    // Accepted operations include an explicit write of the current value.
    void valuesCommitted(snow_shot::storage::ConfigurationWriteOrigin origin,
                         const QMap<QString, QJsonValue>& previous,
                         const QMap<QString, QJsonValue>& values);
    void valueChanged(const QString& key, const QJsonValue& value);
    void mutationRejected(const QString& key, const QString& error);
    void errorChanged(const QString& error);

  private:
    enum class JournalSnapshot { Current, OnNextFlush, Scheduled };
    void materializeCloudSyncJournal();
    bool setValuesImpl(const QMap<QString, QJsonValue>& values, ConfigurationWriteOrigin origin,
                       const QJsonObject* cloudSyncState);
    void load();
    void scheduleFlush();
    void setLastError(const QString& error);
    void rejectMutation(const QString& key, const QString& error);
    void announceChanges(ConfigurationWriteOrigin origin, QMap<QString, QJsonValue> previous,
                         QMap<QString, QJsonValue> values,
                         QVector<QPair<QString, QJsonValue>> changed, bool documentChanged);

    QString m_configurationFile;
    bool m_readAvailable = false;
    bool m_suspended = false;
    bool m_writeAvailable = false;
    mutable QMutex m_mutex;
    mutable QRecursiveMutex m_mutationMutex;
    CloudSyncJournal m_cloudSyncJournal;
    std::function<QJsonObject()> m_snapshotCloudSyncJournal;
    JournalSnapshot m_journalSnapshot = JournalSnapshot::Current;
    QList<std::function<void()>> m_pendingChanges;
    bool m_deliveringChanges = false;
    QMutex m_ioMutex;
    const QMap<QString, QJsonValue> m_defaults;
    QMap<QString, QJsonValue> m_values;
    mouse::Bindings m_mouseBindings;
    QJsonObject m_document;
    QString m_lastError;
    ConfigurationCompatibility m_compatibility = ConfigurationCompatibility::Current;
    quint64 m_revision = 0;
    quint64 m_documentRevision = 0;
    bool m_dirty = false;
    QTimer m_flushTimer;
    std::unique_ptr<QThreadPool> m_flushWorker;
};
} // namespace snow_shot::storage

#endif // SNOW_SHOT_STORAGE_CONFIGURATIONSTORE_H
