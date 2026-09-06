#include "theme_palette.h"

namespace adqt::theme {

namespace {

QColor roleColor(const QColor& value, const QColor& fallback) {
  return value.isValid() ? value : fallback;
}

}  // namespace

QPalette buildPalette(const AdTheme& theme, const QPalette& basePalette) {
  QPalette palette(basePalette);
  const ThemeSemanticPalette& semantic = theme.semantic;

  const QColor bgLayout =
      roleColor(semantic.window, palette.color(QPalette::Active, QPalette::Window));
  const QColor bgContainer =
      roleColor(semantic.surface, palette.color(QPalette::Active, QPalette::Base));
  const QColor bgContainerDisabled =
      roleColor(semantic.surfaceDisabled, palette.color(QPalette::Disabled, QPalette::Base));
  const QColor fillAlter =
      roleColor(semantic.surfaceSubtle, palette.color(QPalette::Active, QPalette::AlternateBase));
  const QColor bgElevated =
      roleColor(semantic.surfaceElevated, palette.color(QPalette::Active, QPalette::ToolTipBase));

  const QColor text = roleColor(semantic.text, palette.color(QPalette::Active, QPalette::Text));
  const QColor textDisabled =
      roleColor(semantic.textDisabled, palette.color(QPalette::Disabled, QPalette::Text));
  const QColor textPlaceholder = roleColor(
      semantic.textPlaceholder, palette.color(QPalette::Active, QPalette::PlaceholderText));
  const QColor textLightSolid =
      roleColor(semantic.textOnAccent, palette.color(QPalette::Active, QPalette::HighlightedText));

  const QColor primary = roleColor(
      semantic.accent,
      roleColor(semantic.accentSolid, palette.color(QPalette::Active, QPalette::Highlight)));
  const QColor primaryDisabled =
      roleColor(semantic.accentDisabled, roleColor(semantic.textDisabled, primary));
  const QColor link = roleColor(semantic.link, palette.color(QPalette::Active, QPalette::Link));
  const QColor linkActive = roleColor(semantic.linkActive, link);

  for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive}) {
    palette.setColor(group, QPalette::Window, bgLayout);
    palette.setColor(group, QPalette::Base, bgContainer);
    palette.setColor(group, QPalette::AlternateBase, fillAlter);
    palette.setColor(group, QPalette::ToolTipBase, bgElevated);
    palette.setColor(group, QPalette::ToolTipText, text);
    palette.setColor(group, QPalette::Button, bgContainer);
    palette.setColor(group, QPalette::Text, text);
    palette.setColor(group, QPalette::WindowText, text);
    palette.setColor(group, QPalette::ButtonText, text);
    palette.setColor(group, QPalette::PlaceholderText, textPlaceholder);
    palette.setColor(group, QPalette::Highlight, primary);
    palette.setColor(group, QPalette::HighlightedText, textLightSolid);
    palette.setColor(group, QPalette::Link, link);
    palette.setColor(group, QPalette::LinkVisited, linkActive);
    palette.setColor(group, QPalette::BrightText, textLightSolid);
    palette.setColor(group, QPalette::Light, fillAlter.lighter(108));
    palette.setColor(group, QPalette::Midlight, fillAlter.lighter(104));
    palette.setColor(group, QPalette::Mid, bgLayout.darker(108));
    palette.setColor(group, QPalette::Dark, bgLayout.darker(116));
    palette.setColor(group, QPalette::Shadow, bgLayout.darker(140));
    palette.setColor(group, QPalette::Accent, primary);
  }

  palette.setColor(QPalette::Disabled, QPalette::Window, bgLayout);
  palette.setColor(QPalette::Disabled, QPalette::Base, bgContainerDisabled);
  palette.setColor(QPalette::Disabled, QPalette::AlternateBase, fillAlter);
  palette.setColor(QPalette::Disabled, QPalette::ToolTipBase, bgElevated);
  palette.setColor(QPalette::Disabled, QPalette::ToolTipText, textDisabled);
  palette.setColor(QPalette::Disabled, QPalette::Button, bgContainerDisabled);
  palette.setColor(QPalette::Disabled, QPalette::Text, textDisabled);
  palette.setColor(QPalette::Disabled, QPalette::WindowText, textDisabled);
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, textDisabled);
  palette.setColor(QPalette::Disabled, QPalette::PlaceholderText, textDisabled);
  palette.setColor(QPalette::Disabled, QPalette::Highlight, primaryDisabled);
  palette.setColor(QPalette::Disabled, QPalette::HighlightedText, textDisabled);
  // Disabled links should read as non-interactive, so use disabled text semantics
  // instead of hover/visited accent colors.
  palette.setColor(QPalette::Disabled, QPalette::Link, textDisabled);
  palette.setColor(QPalette::Disabled, QPalette::LinkVisited, textDisabled);
  palette.setColor(QPalette::Disabled, QPalette::BrightText, textDisabled);
  palette.setColor(QPalette::Disabled, QPalette::Light, fillAlter);
  palette.setColor(QPalette::Disabled, QPalette::Midlight, fillAlter);
  palette.setColor(QPalette::Disabled, QPalette::Mid, bgLayout.darker(106));
  palette.setColor(QPalette::Disabled, QPalette::Dark, bgLayout.darker(112));
  palette.setColor(QPalette::Disabled, QPalette::Shadow, bgLayout.darker(130));
  palette.setColor(QPalette::Disabled, QPalette::Accent, primaryDisabled);

  return palette;
}

QPalette buildPalette(const ResolvedTheme& resolved, const QPalette& basePalette) {
  return buildPalette(resolved.theme, basePalette);
}

}  // namespace adqt::theme
