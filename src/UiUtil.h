#pragma once

// Diálogos con botones en español (los estándar de Qt salen en inglés si no hay traducciones instaladas).

#include <QDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QString>

namespace Dlg {

inline void message(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text) {
    QMessageBox box(icon, title, text, QMessageBox::NoButton, parent);
    box.addButton(QObject::tr("Aceptar"), QMessageBox::AcceptRole);
    box.exec();
}

inline void info(QWidget* p, const QString& title, const QString& text) { message(p, QMessageBox::Information, title, text); }
inline void warn(QWidget* p, const QString& title, const QString& text) { message(p, QMessageBox::Warning, title, text); }
inline void error(QWidget* p, const QString& title, const QString& text) { message(p, QMessageBox::Critical, title, text); }

// Devuelve true si se pulsó «yes». El botón por defecto es siempre «no».
inline bool confirm(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text,
                    const QString& yes, const QString& no, bool dangerous = false) {
    QMessageBox box(icon, title, text, QMessageBox::NoButton, parent);
    QPushButton* y = box.addButton(yes, QMessageBox::AcceptRole);
    QPushButton* n = box.addButton(no, QMessageBox::RejectRole);
    if (dangerous) y->setObjectName(QStringLiteral("danger"));
    box.setDefaultButton(n);
    box.setEscapeButton(n);
    box.exec();
    return box.clickedButton() == y;
}

inline QString askText(QWidget* parent, const QString& title, const QString& label, const QString& initial, bool* ok) {
    QInputDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setLabelText(label);
    dlg.setTextValue(initial);
    dlg.setOkButtonText(QObject::tr("Aceptar"));
    dlg.setCancelButtonText(QObject::tr("Cancelar"));
    dlg.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    const bool accepted = dlg.exec() == QDialog::Accepted;
    if (ok) *ok = accepted;
    return accepted ? dlg.textValue() : QString();
}

}  // namespace Dlg
