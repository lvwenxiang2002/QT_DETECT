#ifndef RECIPEMANAGER_H
#define RECIPEMANAGER_H

#include <QString>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>

#include <QMap>

struct DefectParam {
    double minConfidence = 0.25;
    double maxConfidence = 0.0;   // 0 = no upper limit
    double minArea = 0.0;         // mm², 0 = no lower limit
    double maxArea = 0.0;         // mm², 0 = no upper limit
    double minDiagonal = 0.0;     // mm,  0 = no lower limit
    double maxDiagonal = 0.0;     // mm,  0 = no upper limit
    int uiState = 1;              // 0: Hidden, 1: Shown (Green), 2: Alarm (Red)
};

struct LightFieldParam {
    QMap<QString, DefectParam> defectParams;
};

struct Recipe {
    QString name;
    float globalInferConfidence = 0.25f; // 统一的底层推理库置信度过滤阈值
    bool enableStitchingMode = false; // 是否开启双相机拼图与提取模式
    QMap<QString, LightFieldParam> lightFields;
    
    // Camera Left Settings
    int roiWidthLeft = 16384;
    int offsetXLeft = 0;
    int offsetYLeft = 0;

    // Camera Right Settings
    int roiWidthRight = 16384;
    int offsetXRight = 0;
    int offsetYRight = 0;
};

class RecipeManager {
public:
    static RecipeManager& instance() {
        static RecipeManager instance;
        return instance;
    }

    void load(const QString& filePath);
    void save(const QString& filePath);

    QStringList getRecipeNames() const;
    Recipe getRecipe(const QString& name) const;
    void addOrUpdateRecipe(const Recipe& recipe);
    void deleteRecipe(const QString& name);

    QString getCurrentRecipeName() const;
    void setCurrentRecipeName(const QString& name);

private:
    RecipeManager() = default;
    
    QList<Recipe> m_recipes;
    QString m_currentRecipeName;
};

#endif // RECIPEMANAGER_H
