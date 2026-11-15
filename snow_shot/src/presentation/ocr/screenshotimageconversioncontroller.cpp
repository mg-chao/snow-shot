#include "snow_shot/presentation/screenshotimageconversioncontroller.h"

#include "snow_shot/presentation/languagemanager.h"
#include "snow_shot/storage/settingsadapters.h"

#include <algorithm>
#include <utility>

namespace {
QString currentLocale() {
    return snow_shot::presentation::LanguageManager::instance().currentLocale().name();
}
using Settings = snow_shot::storage::ScreenshotImageConversionSettings;
} // namespace

ScreenshotImageConversionController::ScreenshotImageConversionController(QObject* parent)
    : QObject(parent) {
    m_previewTimer.setInterval(100);
    m_previewTimer.setSingleShot(true);
    connect(&m_previewTimer, &QTimer::timeout, this, &ScreenshotImageConversionController::changed);
}

ScreenshotImageConversionController::~ScreenshotImageConversionController() {
    m_destroying = true;
    cancelRequests();
}

bool ScreenshotImageConversionController::notifyChanged() {
    if (m_destroying)
        return true;
    const QPointer<ScreenshotImageConversionController> lifetime(this);
    const quint64 generation = m_generation;
    emit changed();
    return lifetime && generation == m_generation;
}

bool ScreenshotImageConversionController::notifyResultsChanged() {
    if (m_destroying)
        return true;
    const QPointer<ScreenshotImageConversionController> lifetime(this);
    const quint64 generation = m_generation;
    emit resultsChanged();
    return lifetime && generation == m_generation;
}

void ScreenshotImageConversionController::setProvider(SnowShotApiClient* provider) {
    const QPointer<ScreenshotImageConversionController> lifetime(this);
    if (provider == m_api) {
        return;
    }
    const QPointer<SnowShotApiClient> nextProvider(provider);
    const quint64 providerGeneration = ++m_providerGeneration;
    cancelRequests();
    if (m_api != nullptr) {
        disconnect(m_api, nullptr, this, nullptr);
    }
    m_api = nextProvider;
    // Provider subscriptions are part of the binding transaction, not a conversion request.
    // Complete them before any observer can reset a conversion or replace the provider again.
    bindProviderSignals();
    m_cache.removeIf([this](const auto& cached) { return !entryMatchesProvider(cached.result); });
    const bool failedToRestore =
        m_active && (busy() || !restoreCachedResult(Settings().visionModel()));
    if (!lifetime || providerGeneration != m_providerGeneration)
        return;
    if (failedToRestore) {
        m_source.clear();
        fail(tr("Model configuration changed. Retry to use the updated settings"));
        if (!lifetime || providerGeneration != m_providerGeneration)
            return;
    }
    notifyResultsChanged();
}

void ScreenshotImageConversionController::bindProviderSignals() {
    if (m_api != nullptr) {
        const quint64 providerGeneration = m_providerGeneration;
        connect(m_api, &SnowShotApiClient::chatModelsChanged, this, [this, providerGeneration]() {
            if (providerGeneration != m_providerGeneration)
                return;
            const QString selected = Settings().visionModel();
            const QString resolved =
                snowShotResolvedBuiltInModelId(m_api->cachedChatModels(), selected);
            if (resolved != selected)
                Settings().setVisionModel(resolved);
        });
        connect(m_api, &SnowShotApiClient::baseUrlChanged, this, [this, providerGeneration]() {
            if (providerGeneration != m_providerGeneration)
                return;
            const QPointer<ScreenshotImageConversionController> lifetime(this);
            m_cache.removeIf([](const auto& cached) {
                return !cached.result.model.startsWith(QStringLiteral("custom:"));
            });
            if (m_active && !m_requestModel.startsWith(QStringLiteral("custom:"))) {
                cancelRequests();
                m_source.clear();
                fail(tr("Model configuration changed. Retry to use the updated settings"));
                if (!lifetime || providerGeneration != m_providerGeneration)
                    return;
            }
            notifyResultsChanged();
        });
        connect(
            m_api, &SnowShotApiClient::customModelInvalidated, this,
            [this, providerGeneration](const QString& id, bool, bool vision) {
                if (providerGeneration != m_providerGeneration)
                    return;
                const QPointer<ScreenshotImageConversionController> lifetime(this);
                if (!vision) {
                    return;
                }
                m_cache.removeIf([&id](const auto& cached) { return cached.result.model == id; });
                if (Settings().visionModel() == id || (m_active && m_requestModel == id)) {
                    cancelRequests();
                    m_source.clear();
                    if (Settings().visionModel() == id &&
                        (!m_api->isCustomModel(id) ||
                         std::none_of(m_api->cachedChatModels().cbegin(),
                                      m_api->cachedChatModels().cend(), [&id](const auto& model) {
                                          return model.id == id && model.supportsVision;
                                      }))) {
                        Settings().setVisionModel(m_api->fallbackModel(true));
                        if (!lifetime || providerGeneration != m_providerGeneration)
                            return;
                    }
                    if (m_active) {
                        fail(tr("Model configuration changed. Retry to use the updated settings"));
                        if (!lifetime || providerGeneration != m_providerGeneration)
                            return;
                    }
                }
                notifyResultsChanged();
            });
        connect(m_api, &QObject::destroyed, this, [this, providerGeneration]() {
            if (providerGeneration != m_providerGeneration)
                return;
            ++m_providerGeneration;
            cancelRequests();
            if (busy()) {
                fail(tr("The image conversion service is unavailable"));
            }
        });
    }
}

void ScreenshotImageConversionController::activate(QString key, QImage image,
                                                   SnowShotImageConversionFormat format) {
    if (m_active && m_key == key && m_format == format && busy() &&
        m_requestModel == Settings().visionModel()) {
        return;
    }
    cancelRequests();
    m_key = std::move(key);
    m_image = std::move(image);
    m_format = format;
    m_active = true;
    m_source.clear();
    m_error.clear();
    start(false);
}

bool ScreenshotImageConversionController::entryMatchesProvider(
    const ScreenshotImageConversionEntry& entry) const {
    if (!entry.isValid())
        return false;
    const bool custom = entry.model.startsWith(QStringLiteral("custom:"));
    // Legacy built-in results remain viewable offline, but cannot establish server identity.
    if (!m_api)
        return !custom;
    if (custom && (!m_api->isCustomModel(entry.model) ||
                   std::none_of(m_api->cachedChatModels().cbegin(),
                                m_api->cachedChatModels().cend(), [&entry](const auto& model) {
                                    return model.id == entry.model && model.supportsVision;
                                })))
        return false;
    return !entry.modelFingerprint.isEmpty() &&
           entry.modelFingerprint == m_api->modelFingerprint(entry.model);
}

bool ScreenshotImageConversionController::restoreCachedResult(const QString& model) {
    qsizetype selected = -1;
    for (qsizetype index = m_cache.size(); index > 0; --index) {
        const auto& cached = m_cache.at(index - 1);
        if (cached.key != m_key || cached.result.format != m_format ||
            !entryMatchesProvider(cached.result))
            continue;
        if (cached.result.model == model) {
            selected = index - 1;
            break;
        }
        // A migrated preference can reopen a historical result, including offline. An
        // exact model match still wins, and the stored result keeps its original identity.
        if (selected < 0 && snowShotBuiltInModelAlias(cached.result.model, model))
            selected = index - 1;
    }
    if (selected < 0)
        return false;
    auto entry = m_cache.takeAt(selected);
    m_source = entry.result.source;
    m_requestModel = entry.result.model;
    m_cache.append(std::move(entry));
    m_state = State::Completed;
    notifyChanged();
    return true;
}

void ScreenshotImageConversionController::cacheResult(const QString& key,
                                                      ScreenshotImageConversionEntry entry) {
    m_cache.removeIf([&](const auto& cached) {
        return cached.key == key &&
               snow_shot::presentation::sameImageConversionRequest(cached.result, entry);
    });
    m_cache.append({key, std::move(entry)});
    trimCache();
}

void ScreenshotImageConversionController::trimCache() {
    const auto bytes = [](const CacheEntry& cached) {
        return (cached.key.size() + cached.result.model.size() + cached.result.source.size() +
                cached.result.modelFingerprint.size()) *
               static_cast<qsizetype>(sizeof(QChar));
    };
    qsizetype totalBytes = 0;
    for (const auto& cached : m_cache)
        totalBytes += bytes(cached);
    while (!m_cache.isEmpty() &&
           (m_cache.size() > snow_shot::presentation::kMaximumImageConversionEntries ||
            totalBytes > snow_shot::presentation::kMaximumImageConversionCacheBytes)) {
        totalBytes -= bytes(m_cache.first());
        m_cache.removeFirst();
    }
}

void ScreenshotImageConversionController::cancelRequests() {
    ++m_generation;
    m_reasoningActive = false;
    m_previewTimer.stop();
    if (m_api != nullptr) {
        m_api->cancel(m_modelsToken);
        m_api->cancel(m_conversionToken);
    }
    m_modelsToken = m_conversionToken = 0;
}

void ScreenshotImageConversionController::deactivate() {
    reset(false);
}

void ScreenshotImageConversionController::invalidate() {
    reset(true);
}

void ScreenshotImageConversionController::reset(bool discardCache) {
    const QPointer<ScreenshotImageConversionController> lifetime(this);
    cancelRequests();
    m_active = false;
    m_state = State::Idle;
    m_image = {};
    m_source.clear();
    m_error.clear();
    if (discardCache) {
        m_cache.clear();
        m_key.clear();
    }
    if (lifetime)
        notifyChanged();
}

void ScreenshotImageConversionController::seed(
    const QString& key, const QVector<ScreenshotImageConversionEntry>& entries) {
    if (key.isEmpty()) {
        return;
    }
    m_cache.removeIf([&key](const auto& cached) { return cached.key == key; });
    for (const auto& entry : entries) {
        if (entryMatchesProvider(entry))
            cacheResult(key, entry);
    }
}

QVector<ScreenshotImageConversionEntry>
ScreenshotImageConversionController::entries(const QString& key) const {
    QVector<ScreenshotImageConversionEntry> entries;
    for (const auto& cached : m_cache)
        if (cached.key == key && entryMatchesProvider(cached.result))
            entries.append(cached.result);
    return entries;
}

void ScreenshotImageConversionController::retry() {
    if (m_active && !busy()) {
        start(true);
    }
}

void ScreenshotImageConversionController::selectVisionModel(const QString& model) {
    if (!m_active || model == m_requestModel || !m_api)
        return;
    const auto& models = m_api->cachedChatModels();
    if (std::none_of(models.cbegin(), models.cend(), [&model](const auto& candidate) {
            return candidate.supportsVision && candidate.id == model;
        }))
        return;
    const QPointer<ScreenshotImageConversionController> lifetime(this);
    const quint64 generation = m_generation;
    const bool saved = Settings().setVisionModel(model);
    if (!lifetime || generation != m_generation)
        return;
    if (saved)
        start(false);
    else
        notifyChanged();
}

void ScreenshotImageConversionController::fail(const QString& message) {
    m_previewTimer.stop();
    m_error = message;
    m_state = State::Failed;
    notifyChanged();
}

void ScreenshotImageConversionController::start(bool refreshModels) {
    const QPointer<ScreenshotImageConversionController> lifetime(this);
    cancelRequests();
    m_source.clear();
    m_error.clear();
    m_refreshConversion = refreshModels;
    m_requestModel = Settings().visionModel();
    if (!refreshModels && restoreCachedResult(m_requestModel))
        return;
    if (m_api == nullptr || m_image.isNull() || m_key.isEmpty()) {
        fail(tr("The image conversion service is unavailable"));
        return;
    }
    m_state = State::LoadingModels;
    if (!notifyChanged())
        return;
    if (m_api->isCustomModel(Settings().visionModel())) {
        startWithModels(m_api->cachedChatModels());
        return;
    }
    const quint64 generation = m_generation;
    const auto token = m_api->ensureChatModels(
        currentLocale(), this,
        [this, generation](SnowShotChatModelsResult result) {
            if (generation != m_generation || !m_active) {
                return;
            }
            m_modelsToken = 0;
            if (!result.succeeded()) {
                fail(tr("Unable to load vision models"));
                return;
            }
            startWithModels(result.models);
        },
        refreshModels ? SnowShotApiClient::ChatModelsCachePolicy::Refresh
                      : SnowShotApiClient::ChatModelsCachePolicy::UseCached);
    if (!lifetime || generation != m_generation)
        return;
    m_modelsToken = token;
    if (m_modelsToken == 0) {
        fail(tr("Unable to load vision models"));
    }
}

void ScreenshotImageConversionController::startWithModels(
    const QVector<SnowShotChatModel>& models) {
    const QPointer<ScreenshotImageConversionController> lifetime(this);
    const QString saved = snowShotResolvedBuiltInModelId(models, Settings().visionModel());
    auto selected = std::find_if(models.cbegin(), models.cend(), [&saved](const auto& model) {
        return model.supportsVision && model.id == saved;
    });
    if (selected == models.cend()) {
        selected = std::find_if(models.cbegin(), models.cend(), [](const auto& model) {
            return model.supportsVision && !snowShotExpertModelId(model.id);
        });
    }
    if (selected == models.cend()) {
        fail(tr("No vision models are available"));
        return;
    }
    const QString model = selected->id;
    m_requestModel = model;
    Settings().setVisionModel(model);
    if (!lifetime)
        return;
    if (!m_refreshConversion && restoreCachedResult(model))
        return;
    const QString fingerprint = m_api->modelFingerprint(model);
    m_state = State::Converting;
    if (!notifyChanged())
        return;
    const quint64 generation = m_generation;
    const auto token = m_api->streamImageConversion(
        {model, m_image, m_format}, this,
        [this, generation](const QString& delta) {
            if (generation != m_generation || !m_active) {
                return;
            }
            m_source += delta;
            if (!m_previewTimer.isActive()) {
                m_previewTimer.start();
            }
        },
        [this, generation, model, fingerprint](SnowShotImageConversionResult result) {
            if (generation != m_generation || !m_active) {
                return;
            }
            m_conversionToken = 0;
            m_previewTimer.stop();
            if (!result.succeeded()) {
                if (result.code == QStringLiteral("model_not_found") && m_api != nullptr) {
                    m_modelsToken = m_api->ensureChatModels(
                        currentLocale(), this,
                        [this, generation](SnowShotChatModelsResult) {
                            if (generation == m_generation) {
                                m_modelsToken = 0;
                            }
                        },
                        SnowShotApiClient::ChatModelsCachePolicy::Refresh);
                }
                fail(result.error.isEmpty() ? tr("Image conversion failed") : result.error);
                return;
            }
            m_source = normalizedImageConversionSource(m_source, m_format);
            ScreenshotImageConversionEntry entry{m_format, model, m_source};
            entry.modelFingerprint = fingerprint;
            if (!entry.isValid()) {
                fail(tr("The model returned no usable content"));
                return;
            }
            if (!entryMatchesProvider(entry)) {
                m_source.clear();
                fail(tr("Model configuration changed. Retry to use the updated settings"));
                return;
            }
            cacheResult(m_key, std::move(entry));
            m_state = State::Completed;
            if (!notifyChanged())
                return;
            notifyResultsChanged();
        },
        [this, generation](bool active) {
            if (generation != m_generation || !m_active)
                return;
            m_reasoningActive = active;
            emit reasoningChanged(active);
        });
    if (!lifetime || generation != m_generation)
        return;
    m_conversionToken = token;
    if (m_conversionToken == 0) {
        fail(tr("The image could not be prepared for conversion"));
    }
}
