#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QTabWidget>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>

class Workbench final : public QMainWindow {
    QLineEdit *name_, *id_, *version_, *source_, *icon_, *license_;
    QComboBox *target_, *ui_;
    QTableWidget *applications_, *environment_, *shortcuts_;
    QPlainTextEdit *json_, *log_;
    QPushButton* build_;
    QLabel* status_;
    QProcess process_;
    QJsonObject project_;
    QString filename_;
    bool updating_ = false;
    QWidget* pathRow(QLineEdit*& edit, bool directory, const QString& filter = {}) {
        auto* row = new QWidget;
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        edit = new QLineEdit;
        auto* browse = new QPushButton(tr("浏览…"));
        layout->addWidget(edit);
        layout->addWidget(browse);
        connect(browse, &QPushButton::clicked, this, [this, edit, directory, filter] {
            QString path = directory ? QFileDialog::getExistingDirectory(this, tr("选择文件夹"))
                                     : QFileDialog::getOpenFileName(this, tr("选择文件"), {}, filter);
            if (!path.isEmpty())
                edit->setText(path);
        });
        return row;
    }
    QTableWidget* table(const QStringList& headers) {
        auto* result = new QTableWidget(0, headers.size());
        result->setHorizontalHeaderLabels(headers);
        result->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        result->setSelectionBehavior(QAbstractItemView::SelectRows);
        return result;
    }
    QWidget* tablePage(QTableWidget* table, const QString& label) {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->addWidget(table);
        auto* row = new QHBoxLayout;
        auto* add = new QPushButton(tr("添加"));
        auto* remove = new QPushButton(tr("删除选中项"));
        row->addWidget(add);
        row->addWidget(remove);
        row->addStretch();
        layout->addLayout(row);
        connect(add, &QPushButton::clicked, this, [this, table, label] {
            updating_ = true;
            int n = table->rowCount();
            table->insertRow(n);
            for (int c = 0; c < table->columnCount(); ++c)
                table->setItem(n, c, new QTableWidgetItem);
            table->item(n, 0)->setText(label + QString::number(n + 1));
            int last = table->columnCount() - 1;
            if (table != applications_) {
                table->item(n, last)->setFlags(table->item(n, last)->flags() | Qt::ItemIsUserCheckable);
                table->item(n, last)->setCheckState(Qt::Checked);
            }
            if (table == environment_) {
                table->item(n, 1)->setText("${install.dir}");
                table->item(n, 2)->setText("setIfMissing");
            }
            if (table == shortcuts_) {
                table->item(n, 1)->setText("main");
                table->item(n, 2)->setText("desktop");
            }
            updating_ = false;
            collect();
        });
        connect(remove, &QPushButton::clicked, this, [this, table] {
            if (table->currentRow() >= 0) {
                updating_ = true;
                table->removeRow(table->currentRow());
                updating_ = false;
                collect();
            }
        });
        connect(table, &QTableWidget::itemChanged, this, [this] { collect(); });
        return page;
    }
    QString cell(QTableWidget* table, int row, int column) const {
        return table->item(row, column) ? table->item(row, column)->text() : QString();
    }
    void populate(QTableWidget* table, const QList<QStringList>& rows, const QList<bool>& checks = {}) {
        table->setRowCount(0);
        for (int i = 0; i < rows.size(); ++i) {
            table->insertRow(i);
            for (int c = 0; c < table->columnCount(); ++c)
                table->setItem(i, c, new QTableWidgetItem(rows[i].value(c)));
            if (!checks.isEmpty()) {
                auto* last = table->item(i, table->columnCount() - 1);
                last->setFlags(last->flags() | Qt::ItemIsUserCheckable);
                last->setCheckState(checks[i] ? Qt::Checked : Qt::Unchecked);
            }
        }
    }
    bool parseEditor() {
        QJsonParseError error;
        auto document = QJsonDocument::fromJson(json_->toPlainText().toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            status_->setText(tr("JSON 配置无效：") + error.errorString());
            return false;
        }
        project_ = document.object();
        return true;
    }
    void render() {
        updating_ = true;
        auto app = project_.value("app").toObject();
        name_->setText(app.value("name").toString());
        id_->setText(app.value("id").toString());
        version_->setText(app.value("version").toString());
        source_->setText(project_.value("inputs").toObject().value(target_->currentText()).toString());
        auto brand = project_.value("branding").toObject();
        icon_->setText(brand.value("applicationIcon").toString());
        license_->setText(brand.value("licenses").toObject().value("zh-CN").toString());
        QList<QStringList> apps, env, links;
        QList<bool> envChecks, linkChecks;
        auto entries = project_.value("applications").toObject();
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            auto v = it.value().toObject();
            apps.append({it.key(), v.value("windows").toString(), v.value("linux").toString(),
                         v.value("xj380").toString()});
        }
        for (const auto& value : project_.value("environment").toArray()) {
            auto v = value.toObject();
            env.append({v.value("name").toString(),
                        v.value("value").toString(),
                        v.value("operation").toString("setIfMissing"),
                        {}});
            envChecks.append(v.value("defaultChecked").toBool(true));
        }
        for (const auto& value : project_.value("shortcuts").toArray()) {
            auto v = value.toObject();
            links.append({v.value("name").toString(app.value("name").toString()),
                          v.value("application").toString(),
                          v.value("location").toString("desktop"),
                          {}});
            linkChecks.append(v.value("defaultChecked").toBool(true));
        }
        populate(applications_, apps);
        populate(environment_, env, envChecks);
        populate(shortcuts_, links, linkChecks);
        for (int i = 0; i < environment_->rowCount(); ++i)
            environment_->item(i, 0)->setData(
                Qt::UserRole, project_.value("environment").toArray()[i].toObject().value("id").toString());
        for (int i = 0; i < shortcuts_->rowCount(); ++i)
            shortcuts_->item(i, 0)->setData(
                Qt::UserRole, project_.value("shortcuts").toArray()[i].toObject().value("id").toString());
        json_->setPlainText(QString::fromUtf8(QJsonDocument(project_).toJson(QJsonDocument::Indented)));
        updating_ = false;
    }
    void collect() {
        if (updating_)
            return;
        updating_ = true;
        auto app = project_.value("app").toObject();
        app["name"] = name_->text();
        app["id"] = id_->text();
        app["version"] = version_->text();
        project_["app"] = app;
        auto inputs = project_.value("inputs").toObject();
        inputs[target_->currentText()] = source_->text();
        project_["inputs"] = inputs;
        auto brand = project_.value("branding").toObject();
        if (!icon_->text().isEmpty())
            brand["applicationIcon"] = icon_->text();
        else
            brand.remove("applicationIcon");
        auto licenses = brand.value("licenses").toObject();
        if (!license_->text().isEmpty())
            licenses["zh-CN"] = license_->text();
        else
            licenses.remove("zh-CN");
        brand["licenses"] = licenses;
        project_["branding"] = brand;
        auto oldApps = project_.value("applications").toObject();
        QJsonObject apps;
        for (int i = 0; i < applications_->rowCount(); ++i) {
            auto entry = oldApps.value(cell(applications_, i, 0)).toObject();
            const char* platforms[] = {"windows", "linux", "xj380"};
            for (int j = 0; j < 3; ++j)
                if (!cell(applications_, i, j + 1).isEmpty())
                    entry[platforms[j]] = cell(applications_, i, j + 1);
                else
                    entry.remove(platforms[j]);
            apps[cell(applications_, i, 0)] = entry;
        }
        project_["applications"] = apps;
        QJsonArray environment;
        auto oldEnv = project_.value("environment").toArray();
        for (int i = 0; i < environment_->rowCount(); ++i) {
            QJsonObject value;
            QString stable = environment_->item(i, 0)->data(Qt::UserRole).toString();
            for (const auto& old : oldEnv)
                if (old.toObject().value("id").toString() == stable)
                    value = old.toObject();
            if (stable.isEmpty()) {
                stable = "env-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
                environment_->item(i, 0)->setData(Qt::UserRole, stable);
            }
            value["id"] = stable;
            value["name"] = cell(environment_, i, 0);
            value["value"] = cell(environment_, i, 1);
            value["operation"] = cell(environment_, i, 2);
            value["defaultChecked"] = environment_->item(i, 3)->checkState() == Qt::Checked;
            environment.append(value);
        }
        project_["environment"] = environment;
        QJsonArray shortcuts;
        auto oldLinks = project_.value("shortcuts").toArray();
        for (int i = 0; i < shortcuts_->rowCount(); ++i) {
            QJsonObject value;
            QString stable = shortcuts_->item(i, 0)->data(Qt::UserRole).toString();
            for (const auto& old : oldLinks)
                if (old.toObject().value("id").toString() == stable)
                    value = old.toObject();
            if (stable.isEmpty()) {
                stable = "shortcut-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
                shortcuts_->item(i, 0)->setData(Qt::UserRole, stable);
            }
            value["id"] = stable;
            value["name"] = cell(shortcuts_, i, 0);
            value["application"] = cell(shortcuts_, i, 1);
            value["location"] = cell(shortcuts_, i, 2);
            value["defaultChecked"] = shortcuts_->item(i, 3)->checkState() == Qt::Checked;
            shortcuts.append(value);
        }
        project_["shortcuts"] = shortcuts;
        json_->setPlainText(QString::fromUtf8(QJsonDocument(project_).toJson(QJsonDocument::Indented)));
        updating_ = false;
    }
    bool save() {
        if (!parseEditor())
            return false;
        if (filename_.isEmpty())
            filename_ =
                QFileDialog::getSaveFileName(this, tr("保存打包项目"), "project.json", "JSON (*.json)");
        if (filename_.isEmpty())
            return false;
        QSaveFile file(filename_);
        if (!file.open(QIODevice::WriteOnly) ||
            file.write(QJsonDocument(project_).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
            status_->setText(tr("保存失败：") + file.errorString());
            return false;
        }
        status_->setText(tr("已保存 ") + filename_);
        return true;
    }
    void build() {
        if (!save())
            return;
        QString suffix = target_->currentText().startsWith("windows") ? ".exe"
                         : target_->currentText().startsWith("xj380") ? ".elf"
                                                                      : ".run";
        QString proposed = QFileInfo(filename_).absolutePath() + "/out/" + id_->text() + "-" +
                           target_->currentText() + "-" + ui_->currentText() + suffix;
        QString output = QFileDialog::getSaveFileName(this, tr("安装程序输出位置"), proposed);
        if (output.isEmpty())
            return;
        QString binary = QCoreApplication::applicationDirPath() + "/apack";
#ifdef Q_OS_WIN
        binary += ".exe";
#endif
        process_.setProgram(binary);
        process_.setArguments({"build", "--project", QFileInfo(filename_).absoluteFilePath(), "--target",
                               target_->currentText(), "--ui", ui_->currentText(), "--output", output});
        process_.setWorkingDirectory(QFileInfo(filename_).absolutePath());
        log_->clear();
        status_->setText(tr("正在构建…"));
        build_->setEnabled(false);
        process_.start();
    }

  public:
    bool smokeCheck() const {
        if (target_->count() != 3 || applications_->rowCount() != 1 || environment_->rowCount() != 2)
            return false;
        for (int i = 0; i < environment_->rowCount(); ++i)
            if (environment_->item(i, 3)->checkState() != Qt::Checked)
                return false;
        return true;
    }
    Workbench() {
        setWindowTitle("Accretion Package");
        resize(1080, 820);
        auto* center = new QWidget;
        auto* layout = new QVBoxLayout(center);
        setCentralWidget(center);
        auto* toolbar = new QHBoxLayout;
        auto* open = new QPushButton(tr("打开项目"));
        auto* saveButton = new QPushButton(tr("保存项目"));
        build_ = new QPushButton(tr("构建安装程序"));
        target_ = new QComboBox;
        target_->addItems({"windows-x64", "linux-x64", "xj380-x64"});
#ifndef Q_OS_WIN
        target_->setCurrentText("linux-x64");
#endif
        ui_ = new QComboBox;
        ui_->addItems({"gui", "tui"});
        toolbar->addWidget(open);
        toolbar->addWidget(saveButton);
        toolbar->addStretch();
        toolbar->addWidget(new QLabel(tr("目标")));
        toolbar->addWidget(target_);
        toolbar->addWidget(new QLabel(tr("界面")));
        toolbar->addWidget(ui_);
        toolbar->addWidget(build_);
        layout->addLayout(toolbar);
        auto* tabs = new QTabWidget;
        layout->addWidget(tabs, 1);
        auto* general = new QWidget;
        auto* form = new QFormLayout(general);
        name_ = new QLineEdit;
        id_ = new QLineEdit;
        version_ = new QLineEdit;
        form->addRow(tr("应用名称"), name_);
        form->addRow(tr("产品标识"), id_);
        form->addRow(tr("版本"), version_);
        form->addRow(tr("当前目标输入文件夹"), pathRow(source_, true));
        form->addRow(tr("应用图标"), pathRow(icon_, false, "Images (*.png *.ico *.svg *.bmp)"));
        form->addRow(tr("中文许可协议"), pathRow(license_, false, "Text (*.txt *.md)"));
        form->addRow(new QLabel(tr("各平台路径、组件、注册表、安装器图标及其他选项可在完整配置中编辑。")));
        tabs->addTab(general, tr("项目与资源"));
        applications_ = table({tr("入口 ID"), "Windows", "Linux", "XJ380"});
        environment_ = table({tr("变量名"), tr("值"), tr("操作"), tr("默认勾选")});
        shortcuts_ = table({tr("快捷方式名称"), tr("程序入口 ID"), tr("位置"), tr("默认勾选")});
        tabs->addTab(tablePage(applications_, "app"), tr("程序入口"));
        tabs->addTab(tablePage(environment_, "APP_VAR"), tr("环境变量"));
        tabs->addTab(tablePage(shortcuts_, tr("快捷方式")), tr("快捷方式"));
        auto* advanced = new QWidget;
        auto* advancedLayout = new QVBoxLayout(advanced);
        json_ = new QPlainTextEdit;
        json_->setLineWrapMode(QPlainTextEdit::NoWrap);
        advancedLayout->addWidget(json_);
        auto* apply = new QPushButton(tr("应用 JSON 配置到表单"));
        advancedLayout->addWidget(apply);
        tabs->addTab(advanced, tr("完整配置"));
        log_ = new QPlainTextEdit;
        log_->setReadOnly(true);
        log_->setMaximumHeight(150);
        layout->addWidget(log_);
        status_ = new QLabel(tr("配置驱动 · Windows / Linux / XJ380 · GUI / TUI"));
        layout->addWidget(status_);
        connect(open, &QPushButton::clicked, this, [this] {
            QString selected = QFileDialog::getOpenFileName(this, tr("打开项目"), {}, "JSON (*.json)");
            if (selected.isEmpty())
                return;
            QFile file(selected);
            if (!file.open(QIODevice::ReadOnly)) {
                status_->setText(file.errorString());
                return;
            }
            json_->setPlainText(QString::fromUtf8(file.readAll()));
            if (parseEditor()) {
                filename_ = selected;
                render();
            }
        });
        connect(saveButton, &QPushButton::clicked, this, [this] { save(); });
        connect(build_, &QPushButton::clicked, this, [this] { build(); });
        connect(apply, &QPushButton::clicked, this, [this] {
            if (parseEditor())
                render();
        });
        connect(target_, &QComboBox::currentTextChanged, this, [this] {
            updating_ = true;
            source_->setText(project_.value("inputs").toObject().value(target_->currentText()).toString());
            updating_ = false;
        });
        for (QLineEdit* edit : {name_, id_, version_, source_, icon_, license_})
            connect(edit, &QLineEdit::textChanged, this, [this] { collect(); });
        connect(&process_, &QProcess::readyReadStandardOutput, this,
                [this] { log_->appendPlainText(QString::fromUtf8(process_.readAllStandardOutput())); });
        connect(&process_, &QProcess::readyReadStandardError, this,
                [this] { log_->appendPlainText(QString::fromUtf8(process_.readAllStandardError())); });
        connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            status_->setText(process_.errorString());
            build_->setEnabled(true);
        });
        connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this](int code, QProcess::ExitStatus status) {
                    status_->setText(code == 0 && status == QProcess::NormalExit
                                         ? tr("构建完成")
                                         : tr("构建失败；请查看日志"));
                    build_->setEnabled(true);
                });
        project_ = QJsonObject{
            {"schemaVersion", 1},
            {"app",
             QJsonObject{{"id", "com.example.application"}, {"name", tr("我的应用")}, {"version", "1.0.0"}}},
            {"inputs", QJsonObject{{target_->currentText(), "./payload"}}},
            {"applications",
             QJsonObject{
                 {"main", QJsonObject{{"windows", "app.exe"}, {"linux", "app"}, {"xj380", "app.elf"}}}}},
            {"ui", QJsonObject{{"modes", QJsonArray{"gui", "tui"}}, {"default", "gui"}}},
            {"install",
             QJsonObject{{"scope", "user"}, {"allowPathChange", true}, {"allowScopeChange", true}}},
            {"environment", QJsonArray{QJsonObject{{"id", "home"},
                                                   {"name", "APP_HOME"},
                                                   {"value", "${install.dir}"},
                                                   {"operation", "setIfMissing"},
                                                   {"defaultChecked", true}},
                                       QJsonObject{{"id", "path"},
                                                   {"name", "PATH"},
                                                   {"value", "${install.dir}"},
                                                   {"operation", "appendUnique"},
                                                   {"defaultChecked", true}}}},
            {"shortcuts", QJsonArray{QJsonObject{{"id", "desktop"},
                                                 {"application", "main"},
                                                 {"location", "desktop"},
                                                 {"defaultChecked", true}}}}};
        project_["completion"] = QJsonObject{{"application", "main"}, {"launchDefault", false}};
        render();
    }
};
int main(int argc, char** argv) {
    QApplication application(argc, argv);
    Workbench window;
    window.show();
    if (application.arguments().contains("--smoke-test")) {
        application.processEvents();
        bool good = window.smokeCheck();
        window.close();
        return good ? 0 : 1;
    }
    return application.exec();
}
