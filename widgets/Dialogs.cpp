#include "Dialogs.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QListWidget>
#include <QTableWidget>
#include <QHeaderView>
#include "modules/RecipeManager.h"
#include "modules/ConfigManager.h"

ConfigDialog::ConfigDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("系统配置");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowMinimizeButtonHint);
    resize(600, 450);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    m_tabWidget = new QTabWidget(this);
    
    // 1. Camera Tab
    QWidget *tabCamera = new QWidget();
    QFormLayout *formCam = new QFormLayout(tabCamera);
    m_comboTriggerMode = new QComboBox(this);
    m_comboTriggerMode->addItems({"行频触发 (Line Rate)", "编码器触发 (Encoder)", "自由采集 (Free Run)"});
    m_spinExposure = new QSpinBox(this);
    m_spinExposure->setRange(10, 100000);
    m_spinGain = new QDoubleSpinBox(this);
    m_spinGain->setRange(0.0, 20.0);
    m_spinReconnect = new QSpinBox(this);
    m_spinReconnect->setRange(0, 10);
    formCam->addRow("触发模式:", m_comboTriggerMode);
    formCam->addRow("曝光时间(us):", m_spinExposure);
    formCam->addRow("增益:", m_spinGain);
    formCam->addRow("掉线重试次数:", m_spinReconnect);
    m_tabWidget->addTab(tabCamera, "📷 相机配置");

    // 2. PLC Tab
    QWidget *tabPlc = new QWidget();
    QFormLayout *formPlc = new QFormLayout(tabPlc);
    m_comboProtocol = new QComboBox(this);
    m_comboProtocol->addItems({"TCP Server", "TCP Client", "Modbus TCP"});
    m_editIpAddress = new QLineEdit(this);
    m_spinPort = new QSpinBox(this);
    m_spinPort->setRange(1, 65535);
    m_spinHeartbeat = new QSpinBox(this);
    m_spinHeartbeat->setRange(100, 10000);
    formPlc->addRow("通信协议:", m_comboProtocol);
    formPlc->addRow("IP 地址:", m_editIpAddress);
    formPlc->addRow("端口号:", m_spinPort);
    formPlc->addRow("心跳包间隔(ms):", m_spinHeartbeat);
    m_tabWidget->addTab(tabPlc, "🔌 通信配置");

    // 3. Storage Tab
    QWidget *tabStorage = new QWidget();
    QFormLayout *formStorage = new QFormLayout(tabStorage);
    m_comboSaveStrategy = new QComboBox(this);
    m_comboSaveStrategy->addItems({"保存所有图片", "仅保存 NG 图片", "最高速模式 (不存图)"});
    m_editSavePath = new QLineEdit(this);
    m_spinAutoDeleteDays = new QSpinBox(this);
    m_spinAutoDeleteDays->setRange(1, 365);
    formStorage->addRow("存图策略:", m_comboSaveStrategy);
    formStorage->addRow("存图根目录:", m_editSavePath);
    formStorage->addRow("自动清理天数:", m_spinAutoDeleteDays);
    m_tabWidget->addTab(tabStorage, "💾 存储管理");

    // 4. General Tab
    QWidget *tabGeneral = new QWidget();
    QFormLayout *formGeneral = new QFormLayout(tabGeneral);
    m_checkAutoStart = new QCheckBox("开机自动启动上位机程序", this);
    m_comboPermission = new QComboBox(this);
    m_comboPermission->addItems({"普通操作员", "技术员", "系统管理员"});
    formGeneral->addRow("运行设置:", m_checkAutoStart);
    formGeneral->addRow("默认权限:", m_comboPermission);
    m_tabWidget->addTab(tabGeneral, "⚙️ 系统通用");
    
    mainLayout->addWidget(m_tabWidget);
    
    QPushButton *btnSave = new QPushButton("保存配置", this);
    btnSave->setMinimumHeight(40);
    btnSave->setCursor(Qt::PointingHandCursor);
    btnSave->setStyleSheet("background-color: #1A73E8; color: white; font-weight: bold; border-radius: 4px;");
    mainLayout->addWidget(btnSave);
    
    connect(btnSave, &QPushButton::clicked, this, &ConfigDialog::onSaveConfig);
    
    loadConfig();
}

void ConfigDialog::loadConfig() {
    ConfigManager::instance().load();
    const auto& c = ConfigManager::instance().camera;
    const auto& p = ConfigManager::instance().plc;
    const auto& s = ConfigManager::instance().storage;
    const auto& g = ConfigManager::instance().general;

    m_spinExposure->setValue(c.exposureTime);
    m_spinGain->setValue(c.gain);
    m_comboTriggerMode->setCurrentIndex(c.triggerMode);
    m_spinReconnect->setValue(c.reconnectRetries);

    m_comboProtocol->setCurrentIndex(p.protocol);
    m_editIpAddress->setText(p.ipAddress);
    m_spinPort->setValue(p.port);
    m_spinHeartbeat->setValue(p.heartbeatInterval);

    m_comboSaveStrategy->setCurrentIndex(s.saveStrategy);
    m_editSavePath->setText(s.savePath);
    m_spinAutoDeleteDays->setValue(s.autoDeleteDays);

    m_checkAutoStart->setChecked(g.autoStart);
    m_comboPermission->setCurrentIndex(g.permissionLevel);
}

void ConfigDialog::onSaveConfig() {
    auto& c = ConfigManager::instance().camera;
    auto& p = ConfigManager::instance().plc;
    auto& s = ConfigManager::instance().storage;
    auto& g = ConfigManager::instance().general;

    c.exposureTime = m_spinExposure->value();
    c.gain = m_spinGain->value();
    c.triggerMode = m_comboTriggerMode->currentIndex();
    c.reconnectRetries = m_spinReconnect->value();

    p.protocol = m_comboProtocol->currentIndex();
    p.ipAddress = m_editIpAddress->text();
    p.port = m_spinPort->value();
    p.heartbeatInterval = m_spinHeartbeat->value();

    s.saveStrategy = m_comboSaveStrategy->currentIndex();
    s.savePath = m_editSavePath->text();
    s.autoDeleteDays = m_spinAutoDeleteDays->value();

    g.autoStart = m_checkAutoStart->isChecked();
    g.permissionLevel = m_comboPermission->currentIndex();

    ConfigManager::instance().save();
    QMessageBox::information(this, "成功", "系统配置保存成功！");
    accept();
}

DetectDialog::DetectDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("检测配置 / 型号管理");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowMinimizeButtonHint);
    resize(1200, 700);
    
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    
    // Left panel: List of recipes
    QVBoxLayout *leftLayout = new QVBoxLayout();
    QLabel *lblList = new QLabel("可用型号列表:", this);
    m_recipeList = new QListWidget(this);
    
    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *btnAdd = new QPushButton("新建型号", this);
    QPushButton *btnDelete = new QPushButton("删除型号", this);
    btnLayout->addWidget(btnAdd);
    btnLayout->addWidget(btnDelete);
    
    leftLayout->addWidget(lblList);
    leftLayout->addWidget(m_recipeList);
    leftLayout->addLayout(btnLayout);
    
    // Right panel: Parameters
    QVBoxLayout *rightLayout = new QVBoxLayout();
    
    // 全局推理库置信度
    QHBoxLayout *confLayout = new QHBoxLayout();
    QLabel *lblGlobalConf = new QLabel("推理库全局置信度:", this);
    m_spinGlobalInferConf = new QDoubleSpinBox(this);
    m_spinGlobalInferConf->setRange(0.01, 1.0);
    m_spinGlobalInferConf->setSingleStep(0.05);
    m_spinGlobalInferConf->setValue(0.25);
    confLayout->addWidget(lblGlobalConf);
    confLayout->addWidget(m_spinGlobalInferConf);
    confLayout->addStretch();
    rightLayout->addLayout(confLayout);
    
    m_lfTabWidget = new QTabWidget(this);
    
    QStringList lightFields = {"暗场", "背光"};
    for (const QString& lfName : lightFields) {
        QWidget *lfTab = new QWidget();
        QVBoxLayout *lfLayout = new QVBoxLayout(lfTab);
        
        QTableWidget *table = new QTableWidget();
        table->setColumnCount(7);
        table->setHorizontalHeaderLabels({"缺陷类型", "最小置信度", "最大置信度", "最小面积(mm²)", "最大面积(mm²)", "最小对角线(mm)", "最大对角线(mm)"});
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_lfTables[lfName] = table;
        lfLayout->addWidget(table);
        
        m_lfTabWidget->addTab(lfTab, lfName);
    }
    rightLayout->addWidget(m_lfTabWidget);
    
    QPushButton *btnSave = new QPushButton("保存并应用该型号", this);
    btnSave->setMinimumHeight(40);
    btnSave->setCursor(Qt::PointingHandCursor);
    btnSave->setStyleSheet("background-color: #1A73E8; color: white; font-weight: bold; border-radius: 4px;");
    
    rightLayout->addWidget(btnSave);
    
    mainLayout->addLayout(leftLayout, 1);
    mainLayout->addLayout(rightLayout, 3);
    
    connect(m_recipeList, &QListWidget::currentRowChanged, this, &DetectDialog::onRecipeSelected);
    connect(btnAdd, &QPushButton::clicked, this, &DetectDialog::onAddRecipe);
    connect(btnDelete, &QPushButton::clicked, this, &DetectDialog::onDeleteRecipe);
    connect(btnSave, &QPushButton::clicked, this, &DetectDialog::onSaveRecipe);
    
    loadRecipeList();
}

void DetectDialog::loadRecipeList() {
    m_recipeList->clear();
    QStringList names = RecipeManager::instance().getRecipeNames();
    m_recipeList->addItems(names);
    
    QString current = RecipeManager::instance().getCurrentRecipeName();
    for (int i = 0; i < m_recipeList->count(); ++i) {
        if (m_recipeList->item(i)->text() == current) {
            m_recipeList->setCurrentRow(i);
            break;
        }
    }
}

void DetectDialog::onRecipeSelected(int currentRow) {
    if (currentRow < 0) return;
    populateTable();
}

void DetectDialog::populateTable() {
    if (m_recipeList->currentRow() < 0) return;
    QString name = m_recipeList->item(m_recipeList->currentRow())->text();
    Recipe r = RecipeManager::instance().getRecipe(name);
    
    m_spinGlobalInferConf->setValue(r.globalInferConfidence);
    
    for (const QString& lfName : m_lfTables.keys()) {
        LightFieldParam lfParam = r.lightFields.value(lfName);
        
        QTableWidget *table = m_lfTables[lfName];
        table->setRowCount(0);
        
        QStringList fixedDefects = {"崩边", "划伤", "黑划伤", "凹凸点", "不可擦拭脏污", "点伤", "异色"};
        table->setRowCount(fixedDefects.size());
        for (int i = 0; i < fixedDefects.size(); ++i) {
            QString def = fixedDefects[i];
            DefectParam dp = lfParam.defectParams.value(def, DefectParam());
            
            QTableWidgetItem *itemType = new QTableWidgetItem(def);
            itemType->setFlags(itemType->flags() & ~Qt::ItemIsEditable);
            table->setItem(i, 0, itemType);
            
            table->setItem(i, 1, new QTableWidgetItem(QString::number(dp.minConfidence, 'f', 2)));
            table->setItem(i, 2, new QTableWidgetItem(QString::number(dp.maxConfidence, 'f', 2)));
            table->setItem(i, 3, new QTableWidgetItem(QString::number(dp.minArea,     'f', 2)));  // mm²
            table->setItem(i, 4, new QTableWidgetItem(QString::number(dp.maxArea,     'f', 2)));  // mm², 0=不限
            table->setItem(i, 5, new QTableWidgetItem(QString::number(dp.minDiagonal, 'f', 2)));  // mm
            table->setItem(i, 6, new QTableWidgetItem(QString::number(dp.maxDiagonal, 'f', 2)));  // mm, 0=不限
        }
    }
}

void DetectDialog::onAddRecipe() {
    bool ok;
    QString name = QInputDialog::getText(this, "新建型号", "请输入新型号名称:", QLineEdit::Normal, "", &ok);
    if (ok && !name.isEmpty()) {
        if (RecipeManager::instance().getRecipeNames().contains(name)) {
            QMessageBox::warning(this, "错误", "该型号名称已存在！");
            return;
        }
        Recipe r;
        r.name = name;
        LightFieldParam lf1, lf2;
        r.lightFields["暗场"] = lf1;
        r.lightFields["背光"] = lf2;
        RecipeManager::instance().addOrUpdateRecipe(r);
        RecipeManager::instance().save("recipes.json");
        loadRecipeList();
    }
}

void DetectDialog::onDeleteRecipe() {
    int row = m_recipeList->currentRow();
    if (row < 0) return;
    QString name = m_recipeList->item(row)->text();
    
    if (RecipeManager::instance().getRecipeNames().size() <= 1) {
        QMessageBox::warning(this, "错误", "至少需要保留一个型号！");
        return;
    }
    
    auto reply = QMessageBox::question(this, "确认", QString("确定要删除型号 '%1' 吗？").arg(name));
    if (reply == QMessageBox::Yes) {
        RecipeManager::instance().deleteRecipe(name);
        if (RecipeManager::instance().getCurrentRecipeName() == name) {
            RecipeManager::instance().setCurrentRecipeName(RecipeManager::instance().getRecipeNames().first());
        }
        RecipeManager::instance().save("recipes.json");
        loadRecipeList();
    }
}

void DetectDialog::onSaveRecipe() {
    int row = m_recipeList->currentRow();
    if (row < 0) return;
    QString name = m_recipeList->item(row)->text();
    
    Recipe r = RecipeManager::instance().getRecipe(name);
    r.globalInferConfidence = m_spinGlobalInferConf->value();
    
    for (const QString& lfName : m_lfTables.keys()) {
        QTableWidget *table = m_lfTables[lfName];
        for (int i = 0; i < table->rowCount(); ++i) {
            QString def = table->item(i, 0)->text();
            
            // 获取已有的 dp（这样能保留内部未被表格展现的配置，如 uiState）
            DefectParam dp = r.lightFields[lfName].defectParams[def];
            
            dp.minConfidence = table->item(i, 1)->text().toDouble();
            dp.maxConfidence = table->item(i, 2)->text().toDouble();
            dp.minArea = table->item(i, 3)->text().toInt();
            dp.maxArea = table->item(i, 4)->text().toInt();
            dp.minDiagonal = table->item(i, 5)->text().toInt();
            dp.maxDiagonal = table->item(i, 6)->text().toInt();
            r.lightFields[lfName].defectParams[def] = dp;
        }
    }
    
    RecipeManager::instance().addOrUpdateRecipe(r);
    RecipeManager::instance().setCurrentRecipeName(name);
    RecipeManager::instance().save("recipes.json");
    
    QMessageBox::information(this, "成功", "参数保存并应用成功！");
    emit recipeChanged(name);
}

LogDialog::LogDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("日志");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowMinimizeButtonHint);
    resize(600, 400);
    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *lbl = new QLabel("这里是系统日志版块。\n即将接入运行日志、报错与警告详情。", this);
    lbl->setAlignment(Qt::AlignCenter);
    layout->addWidget(lbl);
}
