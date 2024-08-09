/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                         *
 *  Copyright (C) 2019 Developer Name <developer.name@example.com>         *
 *                                                                         *
 *  This file is part of nymea.                                            *
 *                                                                         *
 *  This library is free software; you can redistribute it and/or          *
 *  modify it under the terms of the GNU Lesser General Public             *
 *  License as published by the Free Software Foundation; either           *
 *  version 2.1 of the License, or (at your option) any later version.     *
 *                                                                         *
 *  This library is distributed in the hope that it will be useful,        *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU      *
 *  Lesser General Public License for more details.                        *
 *                                                                         *
 *  You should have received a copy of the GNU Lesser General Public       *
 *  License along with this library; If not, see                           *
 *  <http://www.gnu.org/licenses/>.                                        *
 *                                                                         *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "integrationplugindynpricingsimulation.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTimeZone>
#include <QUrlQuery>

#include "hardwaremanager.h"
#include "integrations/thing.h"
#include "plugininfo.h"

// Will be used to randomize request time to spread load on backend
qint32 randomMinute = QRandomGenerator::global()->bounded(0, 30);
qint32 randomSecond = QRandomGenerator::global()->bounded(0, 60);
qint32 secsBeforeExpired = QRandomGenerator::global()->bounded(900, 1800);

IntegrationPluginDynPricing::IntegrationPluginDynPricing()
{
    m_connectedStateTypeId = elpricingConnectedStateTypeId;
    m_currentMarketPriceStateTypeId = elpricingCurrentMarketPriceStateTypeId;
    m_validUntilStateTypeId = elpricingValidUntilStateTypeId;
    m_validSinceStateTypeId = elpricingValidSinceStateTypeId;
    m_averagePriceStateTypeId = elpricingAveragePriceStateTypeId;
    m_lowestPriceStateTypeId = elpricingLowestPriceStateTypeId;
    m_highestPriceStateTypeId = elpricingHighestPriceStateTypeId;
    m_averageDeviationStateTypeId = elpricingAverageDeviationStateTypeId;
    m_priceSeriesStateTypeId = elpricingPriceSeriesStateTypeId;
    m_currentSlotStateTypeId = elpricingCurrentSlotStateTypeId;
    m_currentSlotISOStateTypeId = elpricingCurrentSlotISOStateTypeId;
    m_dataDurationStateTypeId = elpricingDataDurationStateTypeId;
}

IntegrationPluginDynPricing::~IntegrationPluginDynPricing() { }

void IntegrationPluginDynPricing::setupThing(ThingSetupInfo *info)
{
    qCDebug(dcDynpricingsimulation) << "Setup thing" << info->thing()->name() << info->thing()->params();

    if (!m_pluginTimer) {
        m_pluginTimer = hardwareManager()->pluginTimerManager()->registerTimer(60);
        connect(m_pluginTimer, &PluginTimer::timeout, this, &IntegrationPluginDynPricing::onPluginTimer);
    }

    // Get token from client_id and client_secret from /etc/nymea/flexa.conf file
    // Parse file
    QFile file("/etc/nymea/flexa.conf");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(dcDynpricingsimulation) << "Could not open /etc/nymea/flexa.conf";
        info->finish(Thing::ThingErrorHardwareFailure, QT_TR_NOOP("Could not find credentials."));
        return;
    }

    QTextStream in(&file);
    QString serverUrl;
    QString token;

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.startsWith("token")) {
            token = line.split("=").at(1);
        } else if (line.startsWith("serverURL")) {
            serverUrl = line.split("=").at(1);
        }
    }
    file.close();
    // Check if token and serverUrl are not empty
    // If empty, return error
    if (token.isEmpty() || serverUrl.isEmpty()) {
        qCWarning(dcDynpricingsimulation) << "Could not find token or serverURL in /etc/nymea/flexa.conf";
        info->finish(Thing::ThingErrorHardwareFailure, QT_TR_NOOP("Could not find credentials."));
        return;
    }
    // Add an "/" to the end of serverUrl if it is not there
    if (!serverUrl.endsWith("/")) {
        serverUrl.append("/");
    }

    m_serverUrl = serverUrl;
    m_token = token;
    requestPriceData(info->thing(), info);
}

void IntegrationPluginDynPricing::onPluginTimer()
{
    foreach (Thing *thing, myThings()) {
        // Check if the current slot has changed
        QDateTime currentTime = QDateTime::currentDateTime();
        QDateTime newSlot = roundToPrev15Minutes(currentTime);
        QDateTime currentSlot =
                QDateTime::fromTime_t(thing->stateValue(m_currentSlotStateTypeId).toUInt());

        if (newSlot != currentSlot) {
            qCDebug(dcDynpricingsimulation) << "Current slot changed. Updating data.";
            qCDebug(dcDynpricingsimulation) << "New slot: " << newSlot;
            qCDebug(dcDynpricingsimulation) << "Old slot: " << currentSlot;
            processPriceData(thing);
        }

        // Check if we need to request new data
        QDateTime validUntil =
                QDateTime::fromTime_t(thing->stateValue(m_validUntilStateTypeId).toUInt());
        qint32 secsToNextSlot = QDateTime::currentDateTime().secsTo(validUntil);
        qCDebug(dcDynpricingsimulation) << "Price data valid for " << secsToNextSlot << " more seconds";
        // random value between 900 and 1800 seconds to spread requests

        if (secsToNextSlot < secsBeforeExpired) {
            qCDebug(dcDynpricingsimulation) << "Price data will expire soon. Requesting new data.";
            requestPriceData(thing);
        }
    }
}

void IntegrationPluginDynPricing::thingRemoved(Thing *thing)
{
    Q_UNUSED(thing)
    qCDebug(dcDynpricingsimulation) << "Thing removed" << thing->name();
    if (m_pluginTimer && myThings().isEmpty()) {
        hardwareManager()->pluginTimerManager()->unregisterTimer(m_pluginTimer);
        m_pluginTimer = nullptr;
    }
}

void IntegrationPluginDynPricing::updateData(Thing *thing)
{
    requestPriceData(thing);
}

void IntegrationPluginDynPricing::requestPriceData(Thing *thing, ThingSetupInfo *setup)
{
    //m_jsonResponse = QJsonDocument::fromJson(...
    if (setup) {
        setup->finish(Thing::ThingErrorNoError);
    }
    thing->setStateValue(m_connectedStateTypeId, true);
    processPriceData(thing);
}

QDateTime IntegrationPluginDynPricing::roundToPrev15Minutes(const QDateTime &dateTime)
{
    // Round down to the previous 15 minutes
    int minutes = dateTime.time().minute();
    int seconds = dateTime.time().second();
    int milliseconds = dateTime.time().msec();
    int remainder = minutes % 15;

    QDateTime roundedDateTime = dateTime;
    if (remainder != 0) {
        int minutesToSubtract = remainder;
        roundedDateTime = roundedDateTime.addSecs(-minutesToSubtract * 60);
    }
    // set milliseconds to 0
    roundedDateTime = roundedDateTime.addMSecs(-milliseconds);
    roundedDateTime = roundedDateTime.addSecs(-seconds);

    return roundedDateTime;
}

void IntegrationPluginDynPricing::processPriceData(Thing *thing)
{
    if (m_jsonResponse.isEmpty()) {
        qCWarning(dcDynpricingsimulation)
                << "No data to process. Plugin should get data from server first.";
        return;
    }
    QJsonArray priceList = m_jsonResponse["response"]["daPrices"].toArray();
    qCDebug(dcDynpricingsimulation) << "Received price list" << priceList;
    Q_UNUSED(thing)

    QDateTime currentTime = QDateTime::currentDateTime();
    double sum = 0;
    double count = 0;
    double averagePrice = 0;
    double currentPrice = 0;
    QDateTime currentSlot;
    int deviation = 0;
    double maxPrice = -1000;
    double minPrice = 1000;
    QVariantMap priceMap;

    // Get first element of todayArray and write it to validSinceStateTypeId
    QVariant firstElement = priceList.at(0);
    QVariantMap elementMap = firstElement.toMap();
    QDateTime validSince = QDateTime::fromString(elementMap.value("time").toString(), Qt::ISODate);
    qCDebug(dcDynpricingsimulation) << "Valid since" << validSince;
    thing->setStateValue(m_validSinceStateTypeId, validSince.toTime_t());
    // Same for last element and write it to validUntilStateTypeId
    QVariant lastElement = priceList.at(priceList.size() - 1);
    elementMap = lastElement.toMap();
    QDateTime validUntil = QDateTime::fromString(elementMap.value("time").toString(), Qt::ISODate);
    validUntil = validUntil.addSecs(3599); // Add last hour minus one second
    qCDebug(dcDynpricingsimulation) << "Valid until" << validUntil;
    thing->setStateValue(m_validUntilStateTypeId, validUntil.toTime_t());

    // Calculate the duration of the data
    qint32 dataDuration = validSince.secsTo(validUntil);
    qCDebug(dcDynpricingsimulation) << "Data duration" << dataDuration;
    dataDuration += 1; // Add last second which valid until is deliberately missing
    // Set state in hours
    thing->setStateValue(m_dataDurationStateTypeId, dataDuration / 3600);

    foreach (QVariant element, priceList) {
        QVariantMap elementMap = element.toMap();
        QDateTime startTime =
                QDateTime::fromString(elementMap.value("time").toString(), Qt::ISODate);
        QDateTime endTime = startTime.addSecs(60 * 60); // 1h slot
        double price = elementMap.value("value").toDouble() / 10; // €/kWh -> ct/kWh
        // Add entry to priceMap using ISO 8601 date time string as key
        priceMap[startTime.toString(Qt::ISODate)] = price;
        qCDebug(dcDynpricingsimulation) << "Slot" << startTime.toString(Qt::ISODate) << "–– Price"
                                  << price << "ct/kWh";
        sum += price;
        count++;

        if (price > maxPrice)
            maxPrice = price;

        if (price < minPrice)
            minPrice = price;

        if (currentTime >= startTime && currentTime <= endTime) {
            currentPrice = price;
            qCDebug(dcDynpricingsimulation) << "Current price" << currentPrice;
            thing->setStateValue(m_currentMarketPriceStateTypeId, currentPrice);
        }

        // Add 3 15 minutes slots to priceMap with same price to simulate 15 minute
        // slots
        priceMap[startTime.addSecs(900).toString(Qt::ISODate)] = price;
        priceMap[startTime.addSecs(1800).toString(Qt::ISODate)] = price;
        priceMap[startTime.addSecs(2700).toString(Qt::ISODate)] = price;
    }

    // Round current time to previous 15 min and set it as currentSlot
    currentSlot = roundToPrev15Minutes(currentTime);
    qCDebug(dcDynpricingsimulation) << "Current slot" << currentSlot;

    // calculate averagePrice and mean deviation
    averagePrice = sum / count;

    if (currentPrice <= averagePrice) {
        deviation =
                -1 * qRound(100 + (-100 * (currentPrice - minPrice) / (averagePrice - minPrice)));
    } else {
        deviation = qRound(-100 * (averagePrice - currentPrice) / (maxPrice - averagePrice));
    }

    thing->setStateValue(m_currentSlotStateTypeId, currentSlot.toTime_t());
    thing->setStateValue(m_currentSlotISOStateTypeId, currentSlot.toString(Qt::ISODate));
    thing->setStateValue(m_averagePriceStateTypeId, averagePrice);
    thing->setStateValue(m_lowestPriceStateTypeId, minPrice);
    thing->setStateValue(m_highestPriceStateTypeId, maxPrice);
    thing->setStateValue(m_averageDeviationStateTypeId, deviation);
    thing->setStateValue(m_priceSeriesStateTypeId, priceMap);

    // TODO: Should be done outside of this function and initiated by the plugin
    // timer Randomly between 13:00 and 13:30 (CET) request new data each 5
    // minutes until we got data for tomorrow
    if (QDateTime::currentDateTime().toTimeZone(QTimeZone("CET")).time()
        > QTime(13, randomMinute, randomSecond)) {
        // check if valid until is after 01:00 tomorrow
        QDateTime tomorrow_one = QDateTime(QDate::currentDate().addDays(1), QTime(1, 0));
        if (validUntil < tomorrow_one) {
            qCDebug(dcDynpricingsimulation) << "Data for tomorrow is missing. Requesting new data.";
            QTimer::singleShot(300 * 1000, thing, [this, thing]() { updateData(thing); });
        } else {
            qCDebug(dcDynpricingsimulation)
                    << "Data for tomorrow is available. No need to request new data.";
        }
    }
}
