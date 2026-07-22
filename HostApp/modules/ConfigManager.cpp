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
        camera.serialNumberLeft = camObj["serialNumberLeft"].toString("");
        camera.serialNumberRight = camObj["serialNumberRight"].toString("");
        camera.ccfPath = camObj["ccfPath"].toString("");
        camera.gain = camObj["gain"].toDouble(1.0);
        camera.reconnectRetries = camObj["reconnectRetries"].toInt(3);
        camera.overlapPixels = camObj["overlapPixels"].toInt(100);
    }

    if (root.contains("plc")) {
        QJsonObject plcObj = root["plc"].toObject();
        plc.protocol = plcObj["protocol"].toInt(0);
        plc.ipAddress = plcObj["ipAddress"].toString("192.168.1.100");
        plc.port = plcObj["port"].toInt(5000);
        plc.heartbeatInterval = plcObj["heartbeatInterval"].toInt(1000);
    }

    if (root.contains("lightCtrl")) {
        QJsonObject lcObj = root["lightCtrl"].toObject();
        lightCtrl.ipAddress = lcObj["ipAddress"].toString("192.168.1.88");
        lightCtrl.port = lcObj["port"].toInt(4000);
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
        general.simulationDir = genObj["simulationDir"].toString("E:/BLL_QT_detect/test_images");
        general.ghostMaxValThreshold = genObj["ghostMaxValThreshold"].toInt(3);
        general.ghostMeanValThreshold = genObj["ghostMeanValThreshold"].toDouble(2.0);
    }
}

void ConfigManager::save(const QString& filePath) {
    QJsonObject root;

    QJsonObject camObj;
    camObj["serialNumberLeft"] = camera.serialNumberLeft;
    camObj["serialNumberRight"] = camera.serialNumberRight;
    camObj["ccfPath"] = camera.ccfPath;
    camObj["gain"] = camera.gain;
    camObj["reconnectRetries"] = camera.reconnectRetries;
    camObj["overlapPixels"] = camera.overlapPixels;
    root["camera"] = camObj;

    QJsonObject plcObj;
    plcObj["protocol"] = plc.protocol;
    plcObj["ipAddress"] = plc.ipAddress;
    plcObj["port"] = plc.port;
    plcObj["heartbeatInterval"] = plc.heartbeatInterval;
    root["plc"] = plcObj;

    QJsonObject lcObj;
    lcObj["ipAddress"] = lightCtrl.ipAddress;
    lcObj["port"] = lightCtrl.port;
    root["lightCtrl"] = lcObj;

    QJsonObject stObj;
    stObj["saveStrategy"] = storage.saveStrategy;
    stObj["savePath"] = storage.savePath;
    stObj["autoDeleteDays"] = storage.autoDeleteDays;
    root["storage"] = stObj;

    QJsonObject genObj;
    genObj["autoStart"] = general.autoStart;
    genObj["permissionLevel"] = general.permissionLevel;
    genObj["simulationDir"] = general.simulationDir;
    genObj["ghostMaxValThreshold"] = general.ghostMaxValThreshold;
    genObj["ghostMeanValThreshold"] = general.ghostMeanValThreshold;
    root["general"] = genObj;

    QJsonDocument docObj(root);
    QFile fileObj(filePath);
    if (fileObj.open(QIODevice::WriteOnly)) {
        fileObj.write(docObj.toJson());
        fileObj.close();
    }
}
