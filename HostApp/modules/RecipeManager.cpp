#include "RecipeManager.h"
#include <QDebug>

void RecipeManager::load(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        // If no file exists, create a default recipe
        Recipe defaultRecipe;
        defaultRecipe.name = "默认型号";
        LightFieldParam lf1, lf2;
        defaultRecipe.lightFields["暗场"] = lf1;
        defaultRecipe.lightFields["背光"] = lf2;
        m_recipes.append(defaultRecipe);
        m_currentRecipeName = "默认型号";
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;

    QJsonObject root = doc.object();
    m_currentRecipeName = root["currentRecipe"].toString();
    
    QJsonArray recipesArray = root["recipes"].toArray();
    m_recipes.clear();
    for (int i = 0; i < recipesArray.size(); ++i) {
        QJsonObject obj = recipesArray[i].toObject();
        Recipe r;
        r.name = obj["name"].toString();
        r.globalInferConfidence = obj["globalInferConfidence"].toDouble(0.25);
        r.enableStitchingMode = obj["enableStitchingMode"].toBool(false);
        r.roiWidthLeft = obj["roiWidthLeft"].toInt(16384);
        r.offsetXLeft = obj["offsetXLeft"].toInt(0);
        r.offsetYLeft = obj["offsetYLeft"].toInt(0);

        r.roiWidthRight = obj["roiWidthRight"].toInt(16384);
        r.offsetXRight = obj["offsetXRight"].toInt(0);
        r.offsetYRight = obj["offsetYRight"].toInt(0);
        
        QJsonObject lfObj = obj["lightFields"].toObject();
        for (auto lfIt = lfObj.begin(); lfIt != lfObj.end(); ++lfIt) {
            LightFieldParam lfParam;
            QJsonObject paramsObj = lfIt.value().toObject();
            for (auto it = paramsObj.begin(); it != paramsObj.end(); ++it) {
                QJsonObject pObj = it.value().toObject();
                DefectParam dp;
                dp.minConfidence = pObj["minConfidence"].toDouble(0.25);
                dp.maxConfidence = pObj["maxConfidence"].toDouble(0.0);
                dp.minArea       = pObj["minArea"].toDouble(0.0);
                dp.maxArea       = pObj["maxArea"].toDouble(0.0);
                dp.minDiagonal   = pObj["minDiagonal"].toDouble(0.0);
                dp.maxDiagonal   = pObj["maxDiagonal"].toDouble(0.0);
                dp.uiState       = pObj["uiState"].toInt(1);
                lfParam.defectParams[it.key()] = dp;
            }
            r.lightFields[lfIt.key()] = lfParam;
        }
        
        m_recipes.append(r);
    }
}

void RecipeManager::save(const QString& filePath) {
    QJsonObject root;
    root["currentRecipe"] = m_currentRecipeName;
    
    QJsonArray recipesArray;
    for (const auto& r : m_recipes) {
        QJsonObject obj;
        obj["name"] = r.name;
        obj["globalInferConfidence"] = r.globalInferConfidence;
        obj["enableStitchingMode"] = r.enableStitchingMode;
        obj["roiWidthLeft"] = r.roiWidthLeft;
        obj["offsetXLeft"] = r.offsetXLeft;
        obj["offsetYLeft"] = r.offsetYLeft;

        obj["roiWidthRight"] = r.roiWidthRight;
        obj["offsetXRight"] = r.offsetXRight;
        obj["offsetYRight"] = r.offsetYRight;
        
        QJsonObject lfObj;
        for (auto lfIt = r.lightFields.begin(); lfIt != r.lightFields.end(); ++lfIt) {
            QJsonObject paramsObj;
            for (auto it = lfIt.value().defectParams.begin(); it != lfIt.value().defectParams.end(); ++it) {
                QJsonObject pObj;
                pObj["minConfidence"] = it.value().minConfidence;
                pObj["maxConfidence"] = it.value().maxConfidence;
                pObj["minArea"] = it.value().minArea;
                pObj["maxArea"] = it.value().maxArea;
                pObj["minDiagonal"] = it.value().minDiagonal;
                pObj["maxDiagonal"] = it.value().maxDiagonal;
                pObj["uiState"] = it.value().uiState;
                paramsObj[it.key()] = pObj;
            }
            lfObj[lfIt.key()] = paramsObj;
        }
        obj["lightFields"] = lfObj;
        
        recipesArray.append(obj);
    }
    root["recipes"] = recipesArray;

    QJsonDocument doc(root);
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
    }
}

QStringList RecipeManager::getRecipeNames() const {
    QStringList names;
    for (const auto& r : m_recipes) {
        names.append(r.name);
    }
    return names;
}

Recipe RecipeManager::getRecipe(const QString& name) const {
    for (const auto& r : m_recipes) {
        if (r.name == name) return r;
    }
    return Recipe();
}

void RecipeManager::addOrUpdateRecipe(const Recipe& recipe) {
    for (int i = 0; i < m_recipes.size(); ++i) {
        if (m_recipes[i].name == recipe.name) {
            m_recipes[i] = recipe;
            return;
        }
    }
    m_recipes.append(recipe);
}

void RecipeManager::deleteRecipe(const QString& name) {
    for (int i = 0; i < m_recipes.size(); ++i) {
        if (m_recipes[i].name == name) {
            m_recipes.removeAt(i);
            break;
        }
    }
}

QString RecipeManager::getCurrentRecipeName() const {
    return m_currentRecipeName;
}

void RecipeManager::setCurrentRecipeName(const QString& name) {
    m_currentRecipeName = name;
}
