/* SPDX-License-Identifier: Apache-2.0 */
/* The Help's pages (split 5 of the phone's plan): one per topic, plain HTML kept in the source, translated in the
 * context "HelpDialog" (the .ts keeps them there), with the markers filled in when a page is shown. Without Qt Widgets,
 * in evre_engine: the desktop's Help dialog (help_dialog.cpp) and the phone's Help screen (phone_help.cpp) show the
 * same pages, the phone a topic of its own first and only those that apply on a phone. */
#pragma once

#include <QString>

struct ThemeColors;

namespace help {

int topicCount();
/* a topic's title, translated */
QString title(int index);
/* a topic's page, translated, styled with the theme's colours (the headings, inline code, the code blocks) and with
 * its markers filled: %CODE% the code block's opening tag, the engine's own numbers; empty out of range */
QString page(int index, const ThemeColors &colors);

/* The phone's Help (part B): its own topic first ("Using the phone app"), then the desktop's topics that apply on a
 * phone, in the desktop's order: not the Map editor, the API, the Dump, the command line or Keys & mouse, which a phone
 * has none of. Indexes of this list, as above. */
int phoneTopicCount();
QString phoneTitle(int index);
QString phonePage(int index, const ThemeColors &colors);

} // namespace help
