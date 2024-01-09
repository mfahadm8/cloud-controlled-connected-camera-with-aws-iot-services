#ifndef DEVICEMANAGER_H
#define DEVICEMANAGER_H

#include "MqttClient.h"

class DeviceManager {
public:
    DeviceManager();
    void subscribeToTopics();

private:
    MqttClient mqttClient;

    // Other member variables and functions (without device shadow and servo)
};

#endif // DEVICEMANAGER_H
