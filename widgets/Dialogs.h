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
    QSpinBox *m_spinExposure;
    QDoubleSpinBox *m_spinGain;
    QComboBox *m_comboTriggerMode;
    QSpinBox *m_spinReconnect;

    // PLC tab
    QComboBox *m_comboProtocol;
    QLineEdit *m_editIpAddress;
    QSpinBox *m_spinPort;
    QSpinBox *m_spinHeartbeat;

    // Storage tab
    QComboBox *m_comboSaveStrategy;
    QLineEdit *m_editSavePath;
    QSpinBox *m_spinAutoDeleteDays;

    // General tab
    QCheckBox *m_checkAutoStart;
    QComboBox *m_comboPermission;
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

private:
    void loadRecipeList();
    void populateTable();
    
    QListWidget *m_recipeList;
    
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
