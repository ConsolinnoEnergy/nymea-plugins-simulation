/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                         *
 *  Copyright (C) 2019 Developer Name <developer.name@example.com>         *
h*                                                                         *
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

#ifndef INTEGRATIONPLUGINDYNPRICINGSIMULATION_H
#define INTEGRATIONPLUGINDYNPRICINGSIMULATION_H

#include "integrations/integrationplugin.h"
#include "plugintimer.h"

#include <QDebug>
#include <QTimer>
#include <QPointer>
#include <QDateTime>
#include <QJsonDocument>


class IntegrationPluginDynPricing : public IntegrationPlugin
{
    Q_OBJECT

    Q_PLUGIN_METADATA(IID "io.nymea.IntegrationPlugin" FILE "integrationplugindynpricingsimulation.json")
    Q_INTERFACES(IntegrationPlugin)

public:
    explicit IntegrationPluginDynPricing();
    ~IntegrationPluginDynPricing();

    void setupThing(ThingSetupInfo *info) override;
    void thingRemoved(Thing *thing) override;

private slots:
    void onPluginTimer();
    void updateData(Thing *thing);
    void requestPriceData(Thing *thing, ThingSetupInfo *setup = nullptr);
    void processPriceData(Thing *thing);

    QDateTime roundToPrev15Minutes(const QDateTime &dateTime);

private:
    PluginTimer *m_pluginTimer = nullptr;

    QString m_serverUrl;
    QString m_token;
    QJsonDocument m_jsonResponse;
    StateTypeId m_connectedStateTypeId;
    StateTypeId m_currentMarketPriceStateTypeId;
    StateTypeId m_validUntilStateTypeId;
    StateTypeId m_validSinceStateTypeId;
    StateTypeId m_averagePriceStateTypeId;
    StateTypeId m_lowestPriceStateTypeId;
    StateTypeId m_highestPriceStateTypeId;
    StateTypeId m_averageDeviationStateTypeId;
    StateTypeId m_priceSeriesStateTypeId;
    StateTypeId m_currentSlotStateTypeId;
    StateTypeId m_currentSlotISOStateTypeId;
    StateTypeId m_dataDurationStateTypeId;

};

#endif // INTEGRATIONPLUGINDYNPRICINGSIMULATION_H
