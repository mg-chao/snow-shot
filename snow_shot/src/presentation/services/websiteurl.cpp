#include "snow_shot/presentation/websiteurl.h"

#include "snow_shot/presentation/languagemanager.h"

namespace snow_shot::presentation {

QUrl localizedWebsiteUrl(const QUrl& address) {
    const QUrl website(QStringLiteral(SNOW_SHOT_WEBSITE_URL));
    QUrl url = address.isRelative() ? website.resolved(address) : address;
    if (!url.isValid() ||
        (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http")) ||
        (url.host() != website.host() && url.host() != QStringLiteral("snowshot.top"))) {
        return url;
    }

    const bool useChinese =
        LanguageManager::instance().currentLocale().language() == QLocale::Chinese;
    QString path = url.path(QUrl::FullyEncoded);
    const bool hasChinesePrefix =
        path == QStringLiteral("/zh") || path.startsWith(QStringLiteral("/zh/"));
    if (useChinese == hasChinesePrefix) {
        return url;
    }

    if (useChinese) {
        path.prepend(path.isEmpty() ? QStringLiteral("/zh/") : QStringLiteral("/zh"));
    } else {
        path.remove(0, 3);
    }
    url.setPath(path.isEmpty() ? QStringLiteral("/") : path, QUrl::StrictMode);
    return url;
}

} // namespace snow_shot::presentation
