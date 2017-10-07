#ifndef INCLUDE_LoRaDEMODGUI_H
#define INCLUDE_LoRaDEMODGUI_H

#include <plugin/plugininstancegui.h>
#include "gui/rollupwidget.h"
#include "dsp/channelmarker.h"
#include "util/messagequeue.h"

#include "lorademodsettings.h"

class PluginAPI;
class DeviceSourceAPI;
class LoRaDemod;
class SpectrumVis;

namespace Ui {
	class LoRaDemodGUI;
}

class LoRaDemodGUI : public RollupWidget, public PluginInstanceGUI {
	Q_OBJECT

public:
	static LoRaDemodGUI* create(PluginAPI* pluginAPI, DeviceSourceAPI *deviceAPI);
	virtual void destroy();

	void setName(const QString& name);
	QString getName() const;
	virtual qint64 getCenterFrequency() const;
	virtual void setCenterFrequency(qint64 centerFrequency);

	void resetToDefaults();
	QByteArray serialize() const;
	bool deserialize(const QByteArray& data);
	virtual MessageQueue *getInputMessageQueue() { return &m_inputMessageQueue; }
	virtual bool handleMessage(const Message& message);

	static const QString m_channelID;

private slots:
	void viewChanged();
	void on_BW_valueChanged(int value);
	void on_Spread_valueChanged(int value);
	void onWidgetRolled(QWidget* widget, bool rollDown);
	void onMenuDoubleClicked();

private:
	Ui::LoRaDemodGUI* ui;
	PluginAPI* m_pluginAPI;
	DeviceSourceAPI* m_deviceAPI;
	ChannelMarker m_channelMarker;
	LoRaDemodSettings m_settings;
	bool m_basicSettingsShown;
	bool m_doApplySettings;

	LoRaDemod* m_LoRaDemod;
	SpectrumVis* m_spectrumVis;
	MessageQueue m_inputMessageQueue;

	explicit LoRaDemodGUI(PluginAPI* pluginAPI, DeviceSourceAPI *deviceAPI, QWidget* parent = NULL);
	virtual ~LoRaDemodGUI();

    void blockApplySettings(bool block);
	void applySettings(bool force = false);
	void displaySettings();
};

#endif // INCLUDE_LoRaDEMODGUI_H
