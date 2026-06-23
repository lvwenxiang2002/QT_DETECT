#include "ConfigManager.h"

void ConfigManager::load(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return; // Use defaults if not found
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;

    QJsonObject root = doc.object();

    if (root.contains("camera")) {
        QJsonObject camObj = root["camera"].toObject();
        camera.exposureTime = camObj["exposureTime"].toInt(5000);
        camera.gain = camObj["gain"].toDouble(1.0);
        camera.triggerMode = camObj["triggerMode"].toInt(0);
        camera.reconnectRetries = camObj["reconnectRetries"].toInt(3);
    }

    if (root.contains("plc")) {
        QJsonObject plcObj = root["plc"].toObject();
        plc.protocol = plcObj["protocol"].toInt(0);
        plc.ipAddress = plcObj["ipAddress"].toString("192.168.1.100");
        plc.port = plcObj["port"].toInt(5000);
        plc.heartbeatInterval = plcObj["heartbeatInterval"].toInt(1000);
    }

    if (root.contains("storage")) {
        QJsonObject stObj = root["storage"].toObject();
        storage.saveStrategy = stObj["saveStrategy"].toInt(0);
        storage.savePath = stObj["savePath"].toString("D:/Data");
        storage.autoDeleteDays = stObj["autoDeleteDays"].toInt(30);
    }

    if (root.contains("general")) {
        QJsonObject genObj = root["general"].toObject();
        general.autoStart = genObj["autoStart"].toBool(false);
        general.permissionLevel = genObj["permissionLevel"].toInt(0);
    }
}

void ConfigManager::save(const QString& filePath) {
    QJsonObject root;

    QJsonObject camObj;
    camObj["exposureTime"] = camera.exposureTime;
    camObj["gain"] = camera.gain;
    camObj["triggerMode"] = camera.triggerMode;
    camObj["reconnectRetries"] = camera.reconnectRetries;
    root["camera"] = camObj;

    QJsonObject plcObj;
    plcObj["protocol"] = plc.protocol;
    plcObj["ipAddress"] = plc.ipAddress;
    plcObj["port"] = plc.port;
    plcObj["heartbeatInterval"] = plc.heartbeatInterval;
    root["plc"] = plcObj;

    QJsonObject stObj;
    stObj["saveStrategy"] = storage.saveStrategy;
    stObj["savePath"] = storage.savePath;
    stObj["autoDeleteDays"] = storage.autoDeleteDays;
    root["storage"] = stObj;

    QJsonObject genObj;
    genObj["autoStart"] = general.autoStart;
    genObj["permissionLevel"] = general.permissionLevel;
    root["general"] = genObj;

    QJsonDocument doc(root);
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
    }
}
