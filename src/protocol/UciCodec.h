// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QString>

#include "UbusTypes.h"

// JSON <-> typed translation for the ubus `uci` object. The only place UCI JSON
// shapes are known; everything above it uses Ubus::UciConfig / UciChangeSet.
namespace Ubus::UciCodec {

// `uci get {config, type}` -> result.values is keyed by section name.
// `uci get {config, section}` -> result.values is a flat option map (one section).
UciConfig parseConfig(const QString &config, const QJsonObject &result);

// One section out of a flat `values` map (".name", ".type", ".anonymous",
// ".index" plus the options).
UciSection parseSection(const QJsonObject &values);

// `uci changes` -> result.changes is an array of tuples.
UciChangeSet parseChanges(const QString &config, const QJsonObject &result);

// Scalar answer of `uci get {config, section, option}`: result.value.
QString parseOptionValue(const QJsonObject &result);

QJsonObject getTypeArgs(const QString &config, const QString &sectionType);
QJsonObject getSectionArgs(const QString &config, const QString &section);
QJsonObject getOptionArgs(const QString &config, const QString &section, const QString &option);
QJsonObject changesArgs(const QString &config);
QJsonObject setArgs(const UciSetRequest &request);
QJsonObject revertArgs(const QString &config);
QJsonObject applyArgs(int rollbackTimeoutSeconds);

} // namespace Ubus::UciCodec
