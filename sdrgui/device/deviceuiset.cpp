///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016 Edouard Griffiths, F4EXB                                   //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
// (at your option) any later version.                                           //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#include <QFont>

#include <algorithm>

#include "dsp/spectrumvis.h"
#include "dsp/dspdevicesourceengine.h"
#include "dsp/dspdevicesinkengine.h"
#include "gui/glspectrum.h"
#include "gui/glspectrumgui.h"
#include "gui/channelwindow.h"
#include "device/devicegui.h"
#include "device/deviceset.h"
#include "plugin/pluginapi.h"
#include "plugin/plugininterface.h"
#include "channel/channelutils.h"
#include "channel/channelapi.h"
#include "channel/channelgui.h"
#include "settings/preset.h"

#include "deviceuiset.h"

DeviceUISet::DeviceUISet(int tabIndex, DeviceSet *deviceSet, int deviceType, QTimer& timer)
{
    m_spectrum = new GLSpectrum;

    if ((deviceType == 0) || (deviceType == 2)) // Single Rx or MIMO
    {
        m_spectrumVis = new SpectrumVis(SDR_RX_SCALEF);
        m_spectrumVis->setGLSpectrum(m_spectrum);
    }
    else if (deviceType == 1) // Single Tx
    {
        m_spectrumVis = new SpectrumVis(SDR_TX_SCALEF);
        m_spectrumVis->setGLSpectrum(m_spectrum);
    }

    m_spectrum->connectTimer(timer);
    m_spectrumGUI = new GLSpectrumGUI;
    m_spectrumGUI->setBuddies(m_spectrumVis, m_spectrum);
    m_channelWindow = new ChannelWindow;
    m_deviceAPI = nullptr;
    m_deviceGUI = nullptr;
    m_deviceSourceEngine = nullptr;
    m_deviceSinkEngine = nullptr;
    m_deviceMIMOEngine = nullptr;
    m_deviceTabIndex = tabIndex;
    m_deviceSet = deviceSet;
    m_nbAvailableRxChannels = 0;   // updated at enumeration for UI selector
    m_nbAvailableTxChannels = 0;   // updated at enumeration for UI selector
    m_nbAvailableMIMOChannels = 0; // updated at enumeration for UI selector

    // m_spectrum needs to have its font to be set since it cannot be inherited from the main window
    QFont font;
    font.setFamily(QStringLiteral("Liberation Sans"));
    font.setPointSize(9);
    m_spectrum->setFont(font);
}

DeviceUISet::~DeviceUISet()
{
    delete m_channelWindow;
    delete m_spectrumGUI;
    delete m_spectrumVis;
    delete m_spectrum;
}

void DeviceUISet::setSpectrumScalingFactor(float scalef)
{
    m_spectrumVis->setScalef(scalef);
}

void DeviceUISet::addChannelMarker(ChannelMarker* channelMarker)
{
    m_spectrum->addChannelMarker(channelMarker);
}

void DeviceUISet::addRollupWidget(QWidget *widget)
{
    m_channelWindow->addRollupWidget(widget);
}

void DeviceUISet::registerRxChannelInstance(const QString& channelName, ChannelAPI *channelAPI, ChannelGUI* channelGUI)
{
    m_channelInstanceRegistrations.append(ChannelInstanceRegistration(channelName, channelAPI, channelGUI, 0));
    m_deviceSet->addChannelInstance(channelName, channelAPI);
    QObject::connect(
        channelGUI,
        &ChannelGUI::closing,
        this,
        [=](){ this->handleChannelGUIClosing(channelGUI); },
        Qt::QueuedConnection
    );
}

void DeviceUISet::registerTxChannelInstance(const QString& channelName, ChannelAPI *channelAPI, ChannelGUI* channelGUI)
{
    m_channelInstanceRegistrations.append(ChannelInstanceRegistration(channelName, channelAPI, channelGUI, 1));
    m_deviceSet->addChannelInstance(channelName, channelAPI);
    QObject::connect(
        channelGUI,
        &ChannelGUI::closing,
        this,
        [=](){ this->handleChannelGUIClosing(channelGUI); },
        Qt::QueuedConnection
    );
}

void DeviceUISet::registerChannelInstance(const QString& channelName, ChannelAPI *channelAPI, ChannelGUI* channelGUI)
{
    m_channelInstanceRegistrations.append(ChannelInstanceRegistration(channelName, channelAPI, channelGUI, 2));
    m_deviceSet->addChannelInstance(channelName, channelAPI);
    QObject::connect(
        channelGUI,
        &ChannelGUI::closing,
        this,
        [=](){ this->handleChannelGUIClosing(channelGUI); },
        Qt::QueuedConnection
    );
}

void DeviceUISet::freeChannels()
{
    for(int i = 0; i < m_channelInstanceRegistrations.count(); i++)
    {
        qDebug("DeviceUISet::freeChannels: destroying channel [%s]", qPrintable(m_channelInstanceRegistrations[i].m_channelURI));
        m_channelInstanceRegistrations[i].m_gui->destroy();
        m_channelInstanceRegistrations[i].m_channelAPI->destroy();
    }

    m_deviceSet->clearChannels();
}

void DeviceUISet::deleteChannel(int channelIndex)
{
    if ((channelIndex >= 0) && (channelIndex < m_channelInstanceRegistrations.count()))
    {
        qDebug("DeviceUISet::deleteChannel: delete channel [%s] at %d",
                qPrintable(m_channelInstanceRegistrations[channelIndex].m_channelURI),
                channelIndex);
        m_channelInstanceRegistrations[channelIndex].m_gui->destroy();
        m_channelInstanceRegistrations[channelIndex].m_channelAPI->destroy();
    }

    m_deviceSet->removeChannelInstanceAt(channelIndex);
}

void DeviceUISet::loadRxChannelSettings(const Preset *preset, PluginAPI *pluginAPI)
{
    if (preset->isSourcePreset())
    {
        qDebug("DeviceUISet::loadRxChannelSettings: Loading preset [%s | %s]",
            qPrintable(preset->getGroup()), qPrintable(preset->getDescription()));

        // Available channel plugins
        PluginAPI::ChannelRegistrations *channelRegistrations = pluginAPI->getRxChannelRegistrations();

        // clear list
        for(int i = 0; i < m_channelInstanceRegistrations.count(); i++)
        {
            qDebug("DeviceUISet::loadRxChannelSettings: destroying old channel [%s]",
                qPrintable(m_channelInstanceRegistrations[i].m_channelURI));
            m_channelInstanceRegistrations[i].m_gui->destroy(); // FIXME: stop channel before
            m_channelInstanceRegistrations[i].m_channelAPI->destroy();
        }

        m_channelInstanceRegistrations.clear();
        m_deviceSet->clearChannels();
        qDebug("DeviceUISet::loadRxChannelSettings: %d channel(s) in preset", preset->getChannelCount());

        for (int i = 0; i < preset->getChannelCount(); i++)
        {
            const Preset::ChannelConfig& channelConfig = preset->getChannelConfig(i);
            ChannelGUI *rxChannelGUI = nullptr;

            // create channel instance

            for(int i = 0; i < channelRegistrations->count(); i++)
            {
                //if((*channelRegistrations)[i].m_channelIdURI == channelConfig.m_channelIdURI)
                if (ChannelUtils::compareChannelURIs((*channelRegistrations)[i].m_channelIdURI, channelConfig.m_channelIdURI))
                {
                    qDebug("DeviceUISet::loadRxChannelSettings: creating new channel [%s] from config [%s]",
                            qPrintable((*channelRegistrations)[i].m_channelIdURI),
                            qPrintable(channelConfig.m_channelIdURI));
                    ChannelAPI *channelAPI;
                    BasebandSampleSink *rxChannel;
                    (*channelRegistrations)[i].m_plugin->createRxChannel(m_deviceAPI, &rxChannel, &channelAPI);
                    rxChannelGUI = (*channelRegistrations)[i].m_plugin->createRxChannelGUI(this, rxChannel);
                    registerRxChannelInstance(channelAPI->getURI(), channelAPI, rxChannelGUI);
                    QObject::connect(
                        rxChannelGUI,
                        &ChannelGUI::closing,
                        this,
                        [=](){ this->handleChannelGUIClosing(rxChannelGUI); },
                        Qt::QueuedConnection
                    );
                    break;
                }
            }

            if (rxChannelGUI)
            {
                qDebug("DeviceUISet::loadRxChannelSettings: deserializing channel [%s]", qPrintable(channelConfig.m_channelIdURI));
                rxChannelGUI->deserialize(channelConfig.m_config);
            }
        }
    }
    else
    {
        qDebug("DeviceUISet::loadRxChannelSettings: Loading preset [%s | %s] not a source preset", qPrintable(preset->getGroup()), qPrintable(preset->getDescription()));
    }
}

void DeviceUISet::saveRxChannelSettings(Preset *preset)
{
    if (preset->isSourcePreset())
    {
        std::sort(m_channelInstanceRegistrations.begin(), m_channelInstanceRegistrations.end()); // sort by increasing delta frequency and type

        for(int i = 0; i < m_channelInstanceRegistrations.count(); i++)
        {
            qDebug("DeviceUISet::saveRxChannelSettings: saving channel [%s]", qPrintable(m_channelInstanceRegistrations[i].m_channelURI));
            preset->addChannel(m_channelInstanceRegistrations[i].m_channelURI, m_channelInstanceRegistrations[i].m_gui->serialize());
        }
    }
    else
    {
        qDebug("DeviceUISet::saveRxChannelSettings: not a source preset");
    }
}

void DeviceUISet::loadTxChannelSettings(const Preset *preset, PluginAPI *pluginAPI)
{
    if (preset->isSinkPreset())
    {
        qDebug("DeviceUISet::loadTxChannelSettings: Loading preset [%s | %s]",
            qPrintable(preset->getGroup()), qPrintable(preset->getDescription()));

        // Available channel plugins
        PluginAPI::ChannelRegistrations *channelRegistrations = pluginAPI->getTxChannelRegistrations();

        //  clear list
        for(int i = 0; i < m_channelInstanceRegistrations.count(); i++)
        {
            qDebug("DeviceUISet::loadTxChannelSettings: destroying old channel [%s]",
                qPrintable(m_channelInstanceRegistrations[i].m_channelURI));
            m_channelInstanceRegistrations[i].m_gui->destroy();
            m_channelInstanceRegistrations[i].m_channelAPI->destroy();
        }

        m_channelInstanceRegistrations.clear();
        m_deviceSet->clearChannels();
        qDebug("DeviceUISet::loadTxChannelSettings: %d channel(s) in preset", preset->getChannelCount());

        for(int i = 0; i < preset->getChannelCount(); i++)
        {
            const Preset::ChannelConfig& channelConfig = preset->getChannelConfig(i);
            ChannelGUI *txChannelGUI = nullptr;

            // create channel instance

            for(int i = 0; i < channelRegistrations->count(); i++)
            {
                if ((*channelRegistrations)[i].m_channelIdURI == channelConfig.m_channelIdURI)
                {
                    qDebug("DeviceUISet::loadTxChannelSettings: creating new channel [%s] from config [%s]",
                            qPrintable((*channelRegistrations)[i].m_channelIdURI),
                            qPrintable(channelConfig.m_channelIdURI));
                    ChannelAPI *channelAPI;
                    BasebandSampleSource *txChannel;
                    (*channelRegistrations)[i].m_plugin->createTxChannel(m_deviceAPI, &txChannel, &channelAPI);
                    txChannelGUI = (*channelRegistrations)[i].m_plugin->createTxChannelGUI(this, txChannel);
                    registerTxChannelInstance(channelAPI->getURI(), channelAPI, txChannelGUI);
                    QObject::connect(
                        txChannelGUI,
                        &ChannelGUI::closing,
                        this,
                        [=](){ this->handleChannelGUIClosing(txChannelGUI); },
                        Qt::QueuedConnection
                    );
                    break;
                }
            }

            if(txChannelGUI)
            {
                qDebug("DeviceUISet::loadTxChannelSettings: deserializing channel [%s]", qPrintable(channelConfig.m_channelIdURI));
                txChannelGUI->deserialize(channelConfig.m_config);
            }
        }
    }
    else
    {
        qDebug("DeviceUISet::loadTxChannelSettings: Loading preset [%s | %s] not a sink preset", qPrintable(preset->getGroup()), qPrintable(preset->getDescription()));
    }

}

void DeviceUISet::saveTxChannelSettings(Preset *preset)
{
    if (preset->isSinkPreset())
    {
        std::sort(m_channelInstanceRegistrations.begin(), m_channelInstanceRegistrations.end()); // sort by increasing delta frequency and type

        for(int i = 0; i < m_channelInstanceRegistrations.count(); i++)
        {
            qDebug("DeviceUISet::saveTxChannelSettings: saving channel [%s]", qPrintable(m_channelInstanceRegistrations[i].m_channelURI));
            preset->addChannel(m_channelInstanceRegistrations[i].m_channelURI, m_channelInstanceRegistrations[i].m_gui->serialize());
        }
    }
    else
    {
        qDebug("DeviceUISet::saveTxChannelSettings: not a sink preset");
    }
}

void DeviceUISet::loadMIMOChannelSettings(const Preset *preset, PluginAPI *pluginAPI)
{
    if (preset->isMIMOPreset())
    {
        qDebug("DeviceUISet::loadMIMOChannelSettings: Loading preset [%s | %s]",
            qPrintable(preset->getGroup()), qPrintable(preset->getDescription()));

        // Available channel plugins
        PluginAPI::ChannelRegistrations *channelRegistrations = pluginAPI->getMIMOChannelRegistrations();

        // clear list
        for(int i = 0; i < m_channelInstanceRegistrations.count(); i++)
        {
            qDebug("DeviceUISet::loadMIMOChannelSettings: destroying old channel [%s]",
                qPrintable(m_channelInstanceRegistrations[i].m_channelURI));
            m_channelInstanceRegistrations[i].m_gui->destroy(); // FIXME: stop channel before
            m_channelInstanceRegistrations[i].m_channelAPI->destroy();
        }

        m_channelInstanceRegistrations.clear();
        m_deviceSet->clearChannels();
        qDebug("DeviceUISet::loadMIMOChannelSettings: %d channel(s) in preset", preset->getChannelCount());

        for (int i = 0; i < preset->getChannelCount(); i++)
        {
            const Preset::ChannelConfig& channelConfig = preset->getChannelConfig(i);
            ChannelGUI *mimoChannelGUI = nullptr;

            // create channel instance

            for(int i = 0; i < channelRegistrations->count(); i++)
            {
                //if((*channelRegistrations)[i].m_channelIdURI == channelConfig.m_channelIdURI)
                if (ChannelUtils::compareChannelURIs((*channelRegistrations)[i].m_channelIdURI, channelConfig.m_channelIdURI))
                {
                    qDebug("DeviceUISet::loadMIMOChannelSettings: creating new channel [%s] from config [%s]",
                            qPrintable((*channelRegistrations)[i].m_channelIdURI),
                            qPrintable(channelConfig.m_channelIdURI));
                    ChannelAPI *channelAPI;
                    MIMOChannel *mimoChannel;
                    (*channelRegistrations)[i].m_plugin->createMIMOChannel(m_deviceAPI, &mimoChannel, &channelAPI);
                    mimoChannelGUI = (*channelRegistrations)[i].m_plugin->createMIMOChannelGUI(this, mimoChannel);
                    (*channelRegistrations)[i].m_plugin->createMIMOChannel(m_deviceAPI, &mimoChannel, &channelAPI);
                    registerChannelInstance(channelAPI->getURI(), channelAPI, mimoChannelGUI);
                    QObject::connect(
                        mimoChannelGUI,
                        &ChannelGUI::closing,
                        this,
                        [=](){ this->handleChannelGUIClosing(mimoChannelGUI); },
                        Qt::QueuedConnection
                    );
                    break;
                }
            }

            if (mimoChannelGUI)
            {
                qDebug("DeviceUISet::loadMIMOChannelSettings: deserializing channel [%s]", qPrintable(channelConfig.m_channelIdURI));
                mimoChannelGUI->deserialize(channelConfig.m_config);
            }
        }
    }
    else
    {
        qDebug("DeviceUISet::loadMIMOChannelSettings: Loading preset [%s | %s] not a MIMO preset", qPrintable(preset->getGroup()), qPrintable(preset->getDescription()));
    }
}

void DeviceUISet::saveMIMOChannelSettings(Preset *preset)
{
    if (preset->isMIMOPreset())
    {
        std::sort(m_channelInstanceRegistrations.begin(), m_channelInstanceRegistrations.end()); // sort by increasing delta frequency and type

        for(int i = 0; i < m_channelInstanceRegistrations.count(); i++)
        {
            qDebug("DeviceUISet::saveMIMOChannelSettings: saving channel [%s]", qPrintable(m_channelInstanceRegistrations[i].m_channelURI));
            preset->addChannel(m_channelInstanceRegistrations[i].m_channelURI, m_channelInstanceRegistrations[i].m_gui->serialize());
        }
    }
    else
    {
        qDebug("DeviceUISet::saveMIMOChannelSettings: not a MIMO preset");
    }
}

// sort by increasing delta frequency and type (i.e. name)
bool DeviceUISet::ChannelInstanceRegistration::operator<(const ChannelInstanceRegistration& other) const
{
    if (m_channelAPI && other.m_channelAPI)
    {
        if (m_channelAPI->getCenterFrequency() == other.m_channelAPI->getCenterFrequency())
        {
            return m_channelAPI->getName() < other.m_channelAPI->getName();
        }
        else
        {
            return m_channelAPI->getCenterFrequency() < other.m_channelAPI->getCenterFrequency();
        }
    }
    else
    {
        return false;
    }
}

void DeviceUISet::handleChannelGUIClosing(ChannelGUI* channelGUI)
{
    for (ChannelInstanceRegistrations::iterator it = m_channelInstanceRegistrations.begin(); it != m_channelInstanceRegistrations.end(); ++it)
    {
        if (it->m_gui == channelGUI)
        {
            m_deviceSet->removeChannelInstance(it->m_channelAPI);
            it->m_channelAPI->destroy();
            m_channelInstanceRegistrations.erase(it);
            break;
        }
    }
}
