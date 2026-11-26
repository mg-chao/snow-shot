#ifndef SNOW_SHOT_PRESENTATION_WEBSITEURL_H
#define SNOW_SHOT_PRESENTATION_WEBSITEURL_H

#include <QUrl>

namespace snow_shot::presentation {

// Resolve an official website page in the current application language.
// An empty address selects the homepage. Absolute links to other sites are returned as given.
[[nodiscard]] QUrl localizedWebsiteUrl(const QUrl& address = {});

} // namespace snow_shot::presentation

#endif // SNOW_SHOT_PRESENTATION_WEBSITEURL_H
