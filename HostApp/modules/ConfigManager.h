#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>

struct CameraConfig {
    QString serialNumberLeft = "";
    QString serialNumberRight = "";
    QString ccfFilePath = "";
    double gain = 1.0;
    int reconnectRetries = 3;
    int overlapPixels = 100; // 相机重叠区像素
};

struct PlcConfig {
    int protocol = 0; // 0: TCP Server, 1: TCP Client, 2: Modbus TCP
    QString ipAddress = "192.168.1.100";
    int port = 5000;
    int heartbeatInterval = 1000;
};

struct LightControllerConfig {
    QString ipAddress = "192.168.1.88";
    int port = 4000;
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
    int ghostMaxValThreshold = 3;
    double ghostMeanValThreshold = 2.0;
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
    LightControllerConfig lightCtrl;
    StorageConfig storage;
    GeneralConfig general;

private:
    ConfigManager() = default;
};

#endif // CONFIGMANAGER_H
