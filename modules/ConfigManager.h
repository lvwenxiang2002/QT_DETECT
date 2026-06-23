#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>

struct CameraConfig {
    int exposureTime = 5000;
    double gain = 1.0;
    int triggerMode = 0; // 0: Line rate, 1: Encoder, 2: Free
    int reconnectRetries = 3;
};

struct PlcConfig {
    int protocol = 0; // 0: TCP Server, 1: TCP Client, 2: Modbus TCP
    QString ipAddress = "192.168.1.100";
    int port = 5000;
    int heartbeatInterval = 1000;
};

struct StorageConfig {
    int saveStrategy = 0; // 0: All, 1: NG only, 2: None
    QString savePath = "D:/Data";
    int autoDeleteDays = 30;
};

struct GeneralConfig {
    bool autoStart = false;
    int permissionLevel = 0;
    QString simulationDir = "E:/BLL_QT_detect/test_images";
};

class ConfigManager {
public:
    static ConfigManager& instance() {
        static ConfigManager instance;
        return instance;
    }

    void load(const QString& filePath = "system_config.json");
    void save(const QString& filePath = "system_config.json");

    CameraConfig camera;
    PlcConfig plc;
    StorageConfig storage;
    GeneralConfig general;

private:
    ConfigManager() = default;
};

#endif // CONFIGMANAGER_H
