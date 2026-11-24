#include "ManagerWindow.h"

#include "ManagerAccess.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace shop {
namespace {

QString askPin(QWidget* parent, const QString& title, const QString& prompt, bool& accepted)
{
    return QInputDialog::getText(parent, title, prompt, QLineEdit::Password, {}, &accepted);
}

void promptPinChange(QWidget* parent)
{
    const QString title = QStringLiteral("Change Manager PIN");
    bool accepted = false;
    const QString current = askPin(parent, title, QStringLiteral("Current PIN"), accepted);
    if (!accepted) {
        return;
    }
    const QString next = askPin(parent, title, QStringLiteral("New four-digit PIN"), accepted);
    if (!accepted) {
        return;
    }
    const QString confirmation = askPin(parent, title, QStringLiteral("Confirm new PIN"), accepted);
    if (!accepted) {
        return;
    }
    if (confirmation != next) {
        QMessageBox::warning(parent, title, QStringLiteral("The new PINs do not match."));
        return;
    }
    QString error;
    if (!changeManagerPin(current, next, error)) {
        QMessageBox::warning(parent, title, error);
        return;
    }
    QMessageBox::information(parent, title, QStringLiteral("Manager PIN updated."));
}

} // namespace

void openManagerWindow(QWidget* parent, const QString&)
{
    const QString title = QStringLiteral("Manager");
    bool accepted = false;
    const QString pin = askPin(parent, title, QStringLiteral("Manager PIN"), accepted);
    if (!accepted) {
        return;
    }
    if (!verifyManagerPin(pin)) {
        QMessageBox::warning(parent, title, QStringLiteral("Incorrect manager PIN."));
        return;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setMinimumWidth(360);
    auto* layout = new QVBoxLayout(&dialog);
    auto* change = new QPushButton(QStringLiteral("Change Manager PIN"), &dialog);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(change);
    layout->addWidget(buttons);
    QObject::connect(change, &QPushButton::clicked, &dialog, [&] { promptPinChange(&dialog); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

} // namespace shop
