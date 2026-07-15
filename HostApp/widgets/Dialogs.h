#ifndef DIALOGS_H
#define DIALOGS_H

#include <QDialog>

#include <QTabWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>

class ConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConfigDialog(QWidget *parent = nullptr);

private slots:
    void onSaveConfig();

private:
    void loadConfig();

    QTabWidget *m_tabWidget;

    // Camera tab
    QLineEdit *m_editSerialLeft;
    QLineEdit *m_editSerialRight;
    QDoubleSpinBox *m_spinGain;
    QSpinBox *m_spinReconnect;
    QSpinBox *m_spinOverlapPixels;

    // PLC tab
    QComboBox *m_comboProtocol;
    QLineEdit *m_editIpAddress;
    QSpinBox *m_spinPort;
    QSpinBox *m_spinHeartbeat;

    // Light Controller tab
    QLineEdit *m_editLcIpAddress;
    QSpinBox *m_spinLcPort;
    QPushButton *m_btnLcConnect;

    // Storage tab
    QComboBox *m_comboSaveStrategy;
    QLineEdit *m_editSavePath;
    QSpinBox *m_spinAutoDeleteDays;

    // General tab
    QCheckBox *m_checkAutoStart;
    QComboBox *m_comboPermission;
    QSpinBox *m_spinGhostMaxValThreshold;
    QDoubleSpinBox *m_spinGhostMeanValThreshold;
};

#include <QListWidget>
#include <QTableWidget>
#include "modules/RecipeManager.h"

class DetectDialog : public QDialog {
    Q_OBJECT
public:
    explicit DetectDialog(QWidget *parent = nullptr);

signals:
    void recipeChanged(const QString& recipeName);

private slots:
    void onRecipeSelected(int currentRow);
    void onAddRecipe();
    void onDeleteRecipe();
    void onSaveRecipe();
    void onSetRoiLeftClicked();
    void onSetRoiRightClicked();

private:
    void loadRecipeList();
    void loadRecipeDetails(const QString& name);
    void setupLightFieldTable(QTableWidget* table);
    void populateTable();
    
    QListWidget *m_recipeList;
    QPushButton *m_btnSetRoiLeft;
    QPushButton *m_btnSetRoiRight;
    QCheckBox *m_checkStitchingMode;
    
    QDoubleSpinBox *m_spinGlobalInferConf;
    
    QTabWidget *m_lfTabWidget; // Main tab widget for light fields
    QMap<QString, QTableWidget*> m_lfTables; // Maps light field name to its parameter table
};

class LogDialog : public QDialog {
    Q_OBJECT
public:
    explicit LogDialog(QWidget *parent = nullptr);
};

#endif // DIALOGS_H
