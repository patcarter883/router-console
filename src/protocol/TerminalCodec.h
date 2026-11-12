// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>

#include "protocol/ToolTypes.h"

// JSON boundary for the terminal window: builds the rpcd `file exec` arguments
// for one framed `/bin/sh -c` and turns the reply back into a typed result.
// Nothing above this file sees JSON (AGENTS.md rule 3).
namespace Ubus::TerminalCodec {

// The frame appends a status marker after the command text; the window strips
// it back out and keeps the working directory it carries.
inline constexpr QLatin1StringView kMarkerPrefix{"__rc_term__ "};

QJsonObject execArgs(const QString &command, const QString &cwd);

// `previousCwd` is kept when the shell ended before the marker could print.
TerminalResult parse(const QString &command, const QJsonObject &reply, const QString &previousCwd);

} // namespace Ubus::TerminalCodec
