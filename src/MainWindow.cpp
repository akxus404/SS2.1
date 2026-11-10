#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QStatusBar>
#include <QStyle>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "Platform.h"
#include "SettingsDialog.h"
#include "Theme.h"
#include "UiUtil.h"
#include "Viewer.h"

namespace {
constexpr int kIdRole = Qt::UserRole;
constexpr int kFolderRole = Qt::UserRole + 1;
}  // namespace

MainWindow::MainWindow(Vault& vault, QWidget* parent) : QMainWindow(parent), vault_(vault) {
    setWindowTitle(QStringLiteral("Secure Space"));
    resize(1040, 660);
    setAcceptDrops(true);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(20, 16, 20, 12);
    root->setSpacing(12);

    // Cabecera: logo, título y acciones globales
    auto* head = new QHBoxLayout;
    head->setSpacing(12);
    head->addWidget(new LogoWidget(46));
    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    auto* title = new QLabel(QStringLiteral("Secure Space"));
    title->setObjectName(QStringLiteral("title"));
    pathLabel_ = new QLabel;
    pathLabel_->setObjectName(QStringLiteral("hint"));
    titles->addWidget(title);
    titles->addWidget(pathLabel_);
    head->addLayout(titles);
    head->addStretch();
    auto* lockBtn = new QPushButton(tr("Bloquear"));
    lockBtn->setToolTip(tr("Bloquear la bóveda (Ctrl+L)"));
    auto* settingsBtn = new QPushButton(tr("Configuración"));
    auto* destroyBtn = new QPushButton(tr("Destruir todo"));
    destroyBtn->setObjectName(QStringLiteral("danger"));
    destroyBtn->setToolTip(tr("Borra de forma permanente e irrecuperable todo el contenido"));
    head->addWidget(lockBtn);
    head->addWidget(settingsBtn);
    head->addWidget(destroyBtn);
    root->addLayout(head);

    // Barra de acciones sobre archivos
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    upBtn_ = new QPushButton(tr("Subir"));
    openBtn_ = new QPushButton(tr("Abrir"));
    openBtn_->setToolTip(tr("Ver o editar dentro de la bóveda, sin descifrar nada en el disco"));

    auto* importBtn = new QPushButton(tr("Importar"));
    importBtn->setObjectName(QStringLiteral("primary"));
    auto* importMenu = new QMenu(importBtn);
    connect(importMenu->addAction(tr("Archivos…")), &QAction::triggered, this, &MainWindow::importFiles);
    connect(importMenu->addAction(tr("Carpeta…")), &QAction::triggered, this, &MainWindow::importFolder);
    importBtn->setMenu(importMenu);

    auto* newBtn = new QPushButton(tr("Nuevo"));
    auto* newMenu = new QMenu(newBtn);
    connect(newMenu->addAction(tr("Carpeta")), &QAction::triggered, this, &MainWindow::newFolder);
    connect(newMenu->addAction(tr("Documento de texto")), &QAction::triggered, this, &MainWindow::newTextFile);
    newBtn->setMenu(newMenu);

    exportBtn_ = new QPushButton(tr("Exportar"));
    renameBtn_ = new QPushButton(tr("Renombrar"));
    deleteBtn_ = new QPushButton(tr("Eliminar"));
    for (QPushButton* b : {upBtn_, openBtn_, importBtn, newBtn, exportBtn_, renameBtn_, deleteBtn_}) row->addWidget(b);
    row->addStretch();
    root->addLayout(row);

    // Lista de elementos
    tree_ = new QTreeWidget;
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({tr("Nombre"), tr("Tamaño"), tr("Modificado")});
    tree_->setRootIsDecorated(false);
    tree_->setAlternatingRowColors(true);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setUniformRowHeights(true);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setStretchLastSection(false);
    tree_->setColumnWidth(1, 120);
    tree_->setColumnWidth(2, 160);
    root->addWidget(tree_, 1);

    // Barra de estado
    statusLabel_ = new QLabel;
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    progress_->setFixedWidth(180);
    progress_->hide();
    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->addPermanentWidget(progress_);

    // Conexiones
    connect(lockBtn, &QPushButton::clicked, this, &MainWindow::lockNow);
    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::openSettings);
    connect(destroyBtn, &QPushButton::clicked, this, &MainWindow::destroyAll);
    connect(upBtn_, &QPushButton::clicked, this, &MainWindow::goUp);
    connect(openBtn_, &QPushButton::clicked, this, &MainWindow::openSelected);
    connect(exportBtn_, &QPushButton::clicked, this, &MainWindow::exportSelected);
    connect(renameBtn_, &QPushButton::clicked, this, &MainWindow::renameSelected);
    connect(deleteBtn_, &QPushButton::clicked, this, &MainWindow::deleteSelected);
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it, int) { openItem(it); });
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::updateButtons);

    auto bind = [this](const QKeySequence& k, auto slot) {
        connect(new QShortcut(k, this), &QShortcut::activated, this, slot);
    };
    bind(QKeySequence(QStringLiteral("Ctrl+L")), &MainWindow::lockNow);
    bind(QKeySequence(QStringLiteral("Ctrl+I")), &MainWindow::importFiles);
    bind(QKeySequence(QStringLiteral("Ctrl+N")), &MainWindow::newFolder);
    bind(QKeySequence(Qt::Key_F2), &MainWindow::renameSelected);
    bind(QKeySequence(Qt::Key_Delete), &MainWindow::deleteSelected);
    bind(QKeySequence(Qt::Key_Backspace), &MainWindow::goUp);

    idle_.setSingleShot(true);
    connect(&idle_, &QTimer::timeout, this, [this] {
        for (ViewerDialog* v : findChildren<ViewerDialog*>())  // viendo un vídeo no cuenta como inactividad
            if (v->isPlaying()) {
                idle_.start();
                return;
            }
        lockNow();
    });
    qApp->installEventFilter(this);

    refresh();
    applySettings();
}

MainWindow::~MainWindow() { qApp->removeEventFilter(this); }

// --- lista ---------------------------------------------------------------------------------------

void MainWindow::refresh() {
    tree_->clear();
    const QList<VaultEntry> entries = vault_.children(current_);
    for (const VaultEntry& e : entries) {
        auto* it = new QTreeWidgetItem(tree_);
        it->setText(0, e.name);
        it->setIcon(0, style()->standardIcon(e.isFolder ? QStyle::SP_DirIcon : QStyle::SP_FileIcon));
        it->setText(1, e.isFolder ? QString() : QLocale().formattedDataSize(e.size));
        it->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        it->setText(2, QDateTime::fromSecsSinceEpoch(e.modified).toString(QStringLiteral("dd/MM/yyyy HH:mm")));
        it->setData(0, kIdRole, e.id);
        it->setData(0, kFolderRole, e.isFolder);
    }
    pathLabel_->setText(tr("Bóveda") + vault_.pathOf(current_));
    upBtn_->setEnabled(!current_.isEmpty());
    statusLabel_->setText(entries.isEmpty() ? tr("Vacío — arrastra archivos aquí o usa «Importar»")
                                            : tr("%1 elemento(s)").arg(entries.size()));
    updateButtons();
}

void MainWindow::updateButtons() {
    const int n = int(tree_->selectedItems().size());
    openBtn_->setEnabled(n == 1);
    exportBtn_->setEnabled(n > 0);
    renameBtn_->setEnabled(n == 1);
    deleteBtn_->setEnabled(n > 0);
}

QStringList MainWindow::selectedIds() const {
    QStringList ids;
    for (QTreeWidgetItem* it : tree_->selectedItems()) ids << it->data(0, kIdRole).toString();
    return ids;
}

void MainWindow::openItem(QTreeWidgetItem* item) {
    if (!item || busy_ || done_) return;
    if (item->data(0, kFolderRole).toBool()) {
        current_ = item->data(0, kIdRole).toString();
        refresh();
        return;
    }
    VaultEntry e;
    if (!vault_.entry(item->data(0, kIdRole).toString(), &e)) return;
    if (ViewerDialog::kindFor(e.name) == ViewerDialog::Kind::None) {
        Dlg::info(this, QStringLiteral("Secure Space"),
                  tr("No hay vista previa para este tipo de archivo.\n\nPuedes exportarlo, o usar archivos de texto, "
                     "imágenes, audio y vídeo directamente dentro de la bóveda."));
        return;
    }
    ViewerDialog dlg(vault_, e, this);
    dlg.exec();
    if (!done_) refresh();  // por si se guardaron cambios (tamaño y fecha)
}

void MainWindow::openSelected() {
    const auto items = tree_->selectedItems();
    if (items.size() == 1) openItem(items.first());
}

void MainWindow::goUp() {
    VaultEntry e;
    if (!current_.isEmpty() && vault_.entry(current_, &e)) {
        current_ = e.parentId;
        refresh();
    }
}

// --- operaciones largas --------------------------------------------------------------------------

void MainWindow::beginBusy() {
    busy_ = true;
    idle_.stop();
    progress_->setValue(0);
    progress_->show();
}

void MainWindow::endBusy() {
    busy_ = false;
    progress_->hide();
    if (vault_.settings().autoLockEnabled) idle_.start(vault_.settings().autoLockMinutes * 60 * 1000);
    if (lockPending_) {
        lockPending_ = false;
        lockNow();
    }
}

Vault::Progress MainWindow::progressFn() {
    return [this](qint64 done, qint64 total) {
        progress_->setValue(total > 0 ? int(done * 100 / total) : 100);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    };
}

// --- importar / exportar -------------------------------------------------------------------------

int MainWindow::askImportAction() {
    int action = vault_.settings().importOriginal;
    if (action == 1 || action == 2) return action;

    QMessageBox box(QMessageBox::Question, tr("Importar a la bóveda"),
                    tr("¿Qué hacemos con los archivos originales?"), QMessageBox::NoButton, this);
    box.setInformativeText(
        tr("Dejar copia: el original sigue donde estaba, sin cifrar.\n\n"
           "Destruir original: se comprueba que la copia cifrada se lee correctamente y entonces el original se "
           "elimina de forma definitiva. Solo quedará el que está dentro de la bóveda."));
    QPushButton* keep = box.addButton(tr("Dejar copia sin cifrar"), QMessageBox::AcceptRole);
    QPushButton* shred = box.addButton(tr("Destruir original"), QMessageBox::DestructiveRole);
    QPushButton* cancel = box.addButton(tr("Cancelar"), QMessageBox::RejectRole);
    box.setDefaultButton(keep);
    box.setEscapeButton(cancel);
    auto* remember = new QCheckBox(tr("Recordar mi elección"));
    box.setCheckBox(remember);
    box.exec();

    if (box.clickedButton() == keep)
        action = 1;
    else if (box.clickedButton() == shred)
        action = 2;
    else
        return -1;
    if (remember->isChecked() && !done_) {
        vault_.settings().importOriginal = action;
        vault_.saveSettings();
    }
    return action;
}

void MainWindow::importFiles() {
    if (busy_ || done_) return;
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Importar archivos a la bóveda"));
    if (!files.isEmpty() && !done_) importPaths(files);
}

void MainWindow::importFolder() {
    if (busy_ || done_) return;
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Importar carpeta a la bóveda"));
    if (!dir.isEmpty() && !done_) importPaths({dir});
}

void MainWindow::importPaths(const QStringList& paths) {
    if (busy_ || done_) return;
    const int action = askImportAction();
    if (action < 0 || done_) return;
    const bool shred = action == 2;

    int ok = 0, bad = 0, kept = 0;
    QString err;
    beginBusy();
    for (const QString& p : paths) importOne(p, current_, shred, ok, bad, kept, err);
    endBusy();
    if (done_) return;
    refresh();
    statusLabel_->setText(shred ? tr("%1 archivo(s) importado(s) y originales destruidos").arg(ok - kept)
                                : tr("%1 archivo(s) importado(s)").arg(ok));

    QStringList problems;
    if (bad > 0) problems << tr("%1 elemento(s) no se pudieron importar: %2").arg(bad).arg(err);
    if (kept > 0)
        problems << tr("%1 original(es) no se pudieron destruir (¿están en uso o protegidos?). Siguen en su "
                       "ubicación original sin cifrar.")
                        .arg(kept);
    if (!problems.isEmpty()) Dlg::warn(this, QStringLiteral("Secure Space"), problems.join(QStringLiteral("\n\n")));
}

void MainWindow::importOne(const QString& path, const QString& parentId, bool shredOriginal, int& ok, int& bad,
                           int& kept, QString& err) {
    const QFileInfo fi(path);
    const int passes = vault_.settings().shredPasses;
    if (fi.isDir()) {
        QString e;
        const QString id = vault_.createFolder(fi.fileName(), parentId, &e);
        if (id.isEmpty()) {
            ++bad;
            err = e;
            return;
        }
        const QFileInfoList kids =
            QDir(path).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks);
        int childBad = 0;
        for (const QFileInfo& k : kids) importOne(k.absoluteFilePath(), id, shredOriginal, ok, childBad, kept, err);
        bad += childBad;
        if (shredOriginal && childBad == 0) QDir().rmdir(path);  // solo se elimina si quedó vacía
        return;
    }
    QString e;
    // Con «destruir original» se verifica la copia cifrada (hash) ANTES de tocar el original.
    if (vault_.importFile(path, parentId, progressFn(), &e, shredOriginal)) {
        ++ok;
        if (shredOriginal && !Vault::shredFile(path, passes)) ++kept;
    } else {
        ++bad;
        err = e;
    }
}

void MainWindow::exportSelected() {
    const QStringList ids = selectedIds();
    if (ids.isEmpty() || busy_ || done_) return;
    if (!exportWarned_) {
        if (!Dlg::confirm(this, QMessageBox::Question, tr("Exportar"),
                          tr("Los archivos exportados quedarán SIN cifrar fuera de la bóveda.\n\n¿Continuar?"),
                          tr("Continuar"), tr("Cancelar")))
            return;
        exportWarned_ = true;
    }
    const QString dest = QFileDialog::getExistingDirectory(this, tr("Elige la carpeta de destino"));
    if (dest.isEmpty() || done_) return;

    QString err;
    bool ok = true;
    beginBusy();
    for (const QString& id : ids)
        if (!vault_.exportItem(id, dest, progressFn(), &err)) {
            ok = false;
            break;
        }
    endBusy();
    if (done_) return;
    if (ok)
        statusLabel_->setText(tr("Exportación completada."));
    else
        Dlg::warn(this, tr("Exportar"), err);
}

// --- carpetas, textos, renombrar, eliminar -------------------------------------------------------

void MainWindow::newFolder() {
    if (busy_ || done_) return;
    bool ok = false;
    const QString name = Dlg::askText(this, tr("Nueva carpeta"), tr("Nombre:"), QString(), &ok);
    if (!ok || done_) return;
    QString err;
    if (vault_.createFolder(name, current_, &err).isEmpty()) Dlg::warn(this, tr("Nueva carpeta"), err);
    refresh();
}

void MainWindow::newTextFile() {
    if (busy_ || done_) return;
    bool ok = false;
    const QString name = Dlg::askText(this, tr("Nuevo documento de texto"), tr("Nombre:"), tr("Nota.txt"), &ok);
    if (!ok || done_) return;
    QString err;
    const QString id = vault_.createFileFromBytes(name, current_, QByteArray(), &err);
    if (id.isEmpty()) {
        Dlg::warn(this, tr("Nuevo documento de texto"), err);
        return;
    }
    refresh();
    VaultEntry e;
    if (vault_.entry(id, &e) && ViewerDialog::kindFor(e.name) == ViewerDialog::Kind::Text) {
        ViewerDialog dlg(vault_, e, this);
        dlg.exec();
        if (!done_) refresh();
    }
}

void MainWindow::renameSelected() {
    const QStringList ids = selectedIds();
    VaultEntry e;
    if (ids.size() != 1 || busy_ || done_ || !vault_.entry(ids.first(), &e)) return;
    bool ok = false;
    const QString name = Dlg::askText(this, tr("Renombrar"), tr("Nuevo nombre:"), e.name, &ok);
    if (!ok || done_) return;
    QString err;
    if (!vault_.rename(e.id, name, &err)) Dlg::warn(this, tr("Renombrar"), err);
    refresh();
}

void MainWindow::deleteSelected() {
    const QStringList ids = selectedIds();
    if (ids.isEmpty() || busy_ || done_) return;
    if (vault_.settings().confirmDelete) {
        if (!Dlg::confirm(this, QMessageBox::Warning, tr("Eliminar"),
                          tr("Se destruirán de forma irrecuperable %1 elemento(s).\n\nEsta acción no se puede "
                             "deshacer.")
                              .arg(ids.size()),
                          tr("Eliminar"), tr("Cancelar"), true))
            return;
    }
    QString err;
    bool ok = true;
    beginBusy();
    for (const QString& id : ids)
        if (!vault_.remove(id, progressFn(), &err)) {
            ok = false;
            break;
        }
    endBusy();
    if (done_) return;
    refresh();
    if (!ok) Dlg::warn(this, tr("Eliminar"), err);
}

// --- destrucción total ---------------------------------------------------------------------------

void MainWindow::destroyAll() {
    if (busy_ || done_) return;
    bool ok = false;
    const QString typed = Dlg::askText(
        this, tr("Destruir todo"),
        tr("Esto borrará de forma PERMANENTE e IRRECUPERABLE todos los archivos de la bóveda, la contraseña y la "
           "llave de este equipo. Secure Space volverá a su estado inicial.\n\nEscribe DESTRUIR para confirmar:"),
        QString(), &ok);
    if (!ok || typed.trimmed() != QLatin1String("DESTRUIR") || done_) return;

    beginBusy();
    vault_.destroyEverything(progressFn());
    busy_ = false;
    progress_->hide();
    Dlg::info(this, QStringLiteral("Secure Space"),
              tr("La bóveda fue destruida. Secure Space volverá a su estado inicial."));
    finish(Result::Destroyed);
}

// --- ajustes, bloqueo y ventana ------------------------------------------------------------------

void MainWindow::openSettings() {
    if (busy_ || done_) return;
    SettingsDialog dlg(vault_, this);
    if (dlg.exec() == QDialog::Accepted && !done_) applySettings();
}

void MainWindow::applySettings() {
    const VaultSettings& s = vault_.settings();
    idle_.stop();
    if (s.autoLockEnabled) idle_.start(s.autoLockMinutes * 60 * 1000);
    Platform::setCaptureProtection(this, s.blockScreenCapture);
    qApp->setStyleSheet(Theme::styleSheet(s.uiFontPx));
}

void MainWindow::lockNow() {
    if (done_) return;
    if (busy_) {
        lockPending_ = true;
        return;
    }
    finish(Result::Locked);
}

void MainWindow::finish(Result r) {
    if (done_) return;
    done_ = true;
    result_ = r;
    idle_.stop();
    // Cerrar visores y cuadros de diálogo abiertos (los visores guardan lo pendiente y liberan sus claves).
    const auto dialogs = findChildren<QDialog*>(QString(), Qt::FindDirectChildrenOnly);
    for (QDialog* d : dialogs) {
        if (auto* v = qobject_cast<ViewerDialog*>(d))
            v->forceClose();
        else
            d->reject();
    }
    vault_.lock();
    hide();
    emit finished();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (busy_) {
        e->ignore();
        return;
    }
    finish(Result::Quit);
    e->accept();
}

void MainWindow::changeEvent(QEvent* e) {
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange && isMinimized() && vault_.settings().lockOnMinimize)
        QTimer::singleShot(0, this, &MainWindow::lockNow);
}

void MainWindow::showEvent(QShowEvent* e) {
    QMainWindow::showEvent(e);
    Platform::setCaptureProtection(this, vault_.settings().blockScreenCapture);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls() && !busy_) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    if (!paths.isEmpty() && !busy_ && !done_) importPaths(paths);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* e) {
    switch (e->type()) {  // cualquier actividad reinicia el temporizador de inactividad
        case QEvent::MouseMove:
        case QEvent::MouseButtonPress:
        case QEvent::KeyPress:
        case QEvent::Wheel:
            if (idle_.isActive()) idle_.start();
            break;
        default: break;
    }
    return QMainWindow::eventFilter(obj, e);
}
