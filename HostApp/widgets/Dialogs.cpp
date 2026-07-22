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
#include <QFileDialog>
#include "modules/RecipeManager.h"
#include "modules/RecipeManager.h"
#include "modules/ConfigManager.h"
#include "RoiSettingDialog.h"

ConfigDialog::ConfigDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("系统配置");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowMinimizeButtonHint);
    resize(600, 450);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    m_tabWidget = new QTabWidget(this);
    
    // 1. Camera Tab
    QWidget *tabCamera = new QWidget();
    QFormLayout *formCam = new QFormLayout(tabCamera);
    m_editSerialLeft = new QLineEdit(this);
    m_editSerialLeft->setPlaceholderText("左相机 SN");
    m_editSerialRight = new QLineEdit(this);
    m_editSerialRight->setPlaceholderText("右相机 SN");
    m_spinGain = new QDoubleSpinBox(this);
    m_spinGain->setRange(0.0, 20.0);
    m_spinReconnect = new QSpinBox(this);
    m_spinReconnect->setRange(0, 10);
    m_spinOverlapPixels = new QSpinBox(this);
    m_spinOverlapPixels->setRange(0, 16384);
    
    m_editCcfPath = new QLineEdit(this);
    m_editCcfPath->setPlaceholderText("相机配置文件 (.ccf)");
    m_btnSelectCcf = new QPushButton("...", this);
    m_btnSelectCcf->setFixedWidth(40);
    QHBoxLayout *ccfLayout = new QHBoxLayout();
    ccfLayout->addWidget(m_editCcfPath);
    ccfLayout->addWidget(m_btnSelectCcf);
    connect(m_btnSelectCcf, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "选择CCF文件", "", "CCF Files (*.ccf);;All Files (*.*)");
        if (!path.isEmpty()) m_editCcfPath->setText(path);
    });

    formCam->addRow("左相机 SN:", m_editSerialLeft);
    formCam->addRow("右相机 SN:", m_editSerialRight);
    formCam->addRow("相机配置文件(CCF):", ccfLayout);
    formCam->addRow("相机重叠区(像素):", m_spinOverlapPixels);
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
    m_spinHeartbeat->setRange(100, 60000);
    formPlc->addRow("协议类型:", m_comboProtocol);
    formPlc->addRow("PLC IP 地址:", m_editIpAddress);
    formPlc->addRow("PLC 端口号:", m_spinPort);
    formPlc->addRow("心跳间隔(ms):", m_spinHeartbeat);
    m_tabWidget->addTab(tabPlc, "🔌 PLC配置");

    // 2.5 Light Controller Tab
    QWidget *tabLc = new QWidget();
    QFormLayout *formLc = new QFormLayout(tabLc);
    m_editLcIpAddress = new QLineEdit(this);
    m_spinLcPort = new QSpinBox(this);
    m_spinLcPort->setRange(1, 65535);
    formLc->addRow("光源 IP:", m_editLcIpAddress);
    formLc->addRow("光源端口:", m_spinLcPort);
    m_btnLcConnect = new QPushButton("测试连接", this);
    formLc->addRow("", m_btnLcConnect);
    m_tabWidget->addTab(tabLc, "💡 光源控制器");

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
    m_spinGhostMaxValThreshold = new QSpinBox(this);
    m_spinGhostMaxValThreshold->setRange(0, 255);
    m_spinGhostMaxValThreshold->setToolTip("暗场黑图伪影过滤阈值(0-255)。若发现边缘内存乱码或者误检，可适当调高此值。");
    
    m_spinGhostMeanValThreshold = new QDoubleSpinBox(this);
    m_spinGhostMeanValThreshold->setRange(0.0, 255.0);
    m_spinGhostMeanValThreshold->setSingleStep(0.1);
    m_spinGhostMeanValThreshold->setToolTip("黑图平均亮度阈值。只有最高亮度和平均亮度都低于阈值时，才会触发斩杀。");

    formGeneral->addRow("运行设置:", m_checkAutoStart);
    formGeneral->addRow("默认权限:", m_comboPermission);
    formGeneral->addRow("黑图伪影过滤阈值(MaxVal):", m_spinGhostMaxValThreshold);
    formGeneral->addRow("黑图伪影过滤阈值(MeanVal):", m_spinGhostMeanValThreshold);
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

    m_editSerialLeft->setText(c.serialNumberLeft);
    m_editSerialRight->setText(c.serialNumberRight);
    m_editCcfPath->setText(c.ccfPath);
    m_spinGain->setValue(c.gain);
    m_spinReconnect->setValue(c.reconnectRetries);
    m_spinOverlapPixels->setValue(c.overlapPixels);

    m_comboProtocol->setCurrentIndex(p.protocol);
    m_editIpAddress->setText(p.ipAddress);
    m_spinPort->setValue(p.port);
    m_spinHeartbeat->setValue(p.heartbeatInterval);

    auto& lc = ConfigManager::instance().lightCtrl;
    m_editLcIpAddress->setText(lc.ipAddress);
    m_spinLcPort->setValue(lc.port);

    m_comboSaveStrategy->setCurrentIndex(s.saveStrategy);
    m_editSavePath->setText(s.savePath);
    m_spinAutoDeleteDays->setValue(s.autoDeleteDays);

    m_checkAutoStart->setChecked(g.autoStart);
    m_comboPermission->setCurrentIndex(g.permissionLevel);
    m_spinGhostMaxValThreshold->setValue(g.ghostMaxValThreshold);
    m_spinGhostMeanValThreshold->setValue(g.ghostMeanValThreshold);
}

void ConfigDialog::onSaveConfig() {
    auto& c = ConfigManager::instance().camera;
    auto& p = ConfigManager::instance().plc;
    auto& s = ConfigManager::instance().storage;
    auto& g = ConfigManager::instance().general;

    c.serialNumberLeft = m_editSerialLeft->text().trimmed();
    c.serialNumberRight = m_editSerialRight->text().trimmed();
    c.ccfPath = m_editCcfPath->text().trimmed();
    c.gain = m_spinGain->value();
    c.reconnectRetries = m_spinReconnect->value();
    c.overlapPixels = m_spinOverlapPixels->value();

    p.protocol = m_comboProtocol->currentIndex();
    p.ipAddress = m_editIpAddress->text().trimmed();
    p.port = m_spinPort->value();
    p.heartbeatInterval = m_spinHeartbeat->value();

    auto& lc = ConfigManager::instance().lightCtrl;
    lc.ipAddress = m_editLcIpAddress->text().trimmed();
    lc.port = m_spinLcPort->value();

    s.saveStrategy = m_comboSaveStrategy->currentIndex();
    s.savePath = m_editSavePath->text();
    s.autoDeleteDays = m_spinAutoDeleteDays->value();

    g.autoStart = m_checkAutoStart->isChecked();
    g.permissionLevel = m_comboPermission->currentIndex();
    g.ghostMaxValThreshold = m_spinGhostMaxValThreshold->value();
    g.ghostMeanValThreshold = m_spinGhostMeanValThreshold->value();

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
    m_btnSetRoiLeft = new QPushButton("左相机 ROI 设置", this);
    m_btnSetRoiRight = new QPushButton("右相机 ROI 设置", this);
    btnLayout->addWidget(btnAdd);
    btnLayout->addWidget(btnDelete);
    
    leftLayout->addWidget(lblList);
    leftLayout->addWidget(m_recipeList);
    leftLayout->addLayout(btnLayout);
    
    // Right panel: Parameters
    QVBoxLayout *rightLayout = new QVBoxLayout();
    
    QHBoxLayout *roiBtnLayout = new QHBoxLayout();
    roiBtnLayout->addWidget(m_btnSetRoiLeft);
    roiBtnLayout->addWidget(m_btnSetRoiRight);
    rightLayout->addLayout(roiBtnLayout);
    
    // 全局推理库置信度
    QHBoxLayout *confLayout = new QHBoxLayout();
    QLabel *lblGlobalConf = new QLabel("推理库全局置信度:", this);
    m_spinGlobalInferConf = new QDoubleSpinBox(this);
    m_spinGlobalInferConf->setRange(0.01, 1.0);
    m_spinGlobalInferConf->setSingleStep(0.05);
    m_spinGlobalInferConf->setValue(0.25);
    confLayout->addWidget(lblGlobalConf);
    confLayout->addWidget(m_spinGlobalInferConf);
    
    m_checkStitchingMode = new QCheckBox("开启双相机拼图与提取模式", this);
    confLayout->addSpacing(20);
    confLayout->addWidget(m_checkStitchingMode);
    
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
    connect(m_btnSetRoiLeft, &QPushButton::clicked, this, &DetectDialog::onSetRoiLeftClicked);
    connect(m_btnSetRoiRight, &QPushButton::clicked, this, &DetectDialog::onSetRoiRightClicked);
    
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
    m_checkStitchingMode->setChecked(r.enableStitchingMode);
    
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
    r.enableStitchingMode = m_checkStitchingMode->isChecked();
    
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

void DetectDialog::onSetRoiLeftClicked() {
    QListWidgetItem* item = m_recipeList->currentItem();
    if (!item) {
        QMessageBox::warning(this, "警告", "请先选择一个型号");
        return;
    }
    
    QString recipeName = item->text();
    Recipe r = RecipeManager::instance().getRecipe(recipeName);
    
    RoiSettingDialog dlg(0, this);
    dlg.setRoi(r.offsetXLeft, r.offsetYLeft, r.roiWidthLeft, r.roiHeightLeft);
    
    QWidget* p = parentWidget();
    while (p && !p->inherits("MainWindow")) {
        p = p->parentWidget();
    }
    
    if (p) {
        connect(p, SIGNAL(capturedForRoi(std::shared_ptr<ImageFrame>)), &dlg, SLOT(updateImage(std::shared_ptr<ImageFrame>)));
        QMetaObject::invokeMethod(p, "triggerCameraForRoi", Q_ARG(int, 0));
    }
    
    int execResult = dlg.exec();
    if (p) {
        QMetaObject::invokeMethod(p, "stopCameraForRoi", Q_ARG(int, 0));
    }
    
    if (execResult == QDialog::Accepted) {
        r.offsetXLeft = dlg.roiX();
        r.offsetYLeft = dlg.roiY();
        r.roiWidthLeft = dlg.roiWidth();
        r.roiHeightLeft = dlg.roiHeight();
        RecipeManager::instance().addOrUpdateRecipe(r);
        RecipeManager::instance().save("recipes.json");
        emit recipeChanged(recipeName);
    }
}

void DetectDialog::onSetRoiRightClicked() {
    QListWidgetItem* item = m_recipeList->currentItem();
    if (!item) {
        QMessageBox::warning(this, "警告", "请先选择一个型号");
        return;
    }
    
    QString recipeName = item->text();
    Recipe r = RecipeManager::instance().getRecipe(recipeName);
    
    RoiSettingDialog dlg(1, this);
    dlg.setRoi(r.offsetXRight, r.offsetYRight, r.roiWidthRight, r.roiHeightRight);
    
    QWidget* p = parentWidget();
    while (p && !p->inherits("MainWindow")) {
        p = p->parentWidget();
    }
    
    if (p) {
        connect(p, SIGNAL(capturedForRoi(std::shared_ptr<ImageFrame>)), &dlg, SLOT(updateImage(std::shared_ptr<ImageFrame>)));
        QMetaObject::invokeMethod(p, "triggerCameraForRoi", Q_ARG(int, 1));
    }
    
    int execResult = dlg.exec();
    if (p) {
        QMetaObject::invokeMethod(p, "stopCameraForRoi", Q_ARG(int, 1));
    }
    
    if (execResult == QDialog::Accepted) {
        r.offsetXRight = dlg.roiX();
        r.offsetYRight = dlg.roiY();
        r.roiWidthRight = dlg.roiWidth();
        r.roiHeightRight = dlg.roiHeight();
        RecipeManager::instance().addOrUpdateRecipe(r);
        RecipeManager::instance().save("recipes.json");
        emit recipeChanged(recipeName);
    }
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
