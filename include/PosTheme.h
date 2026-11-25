#pragma once

#include <QApplication>
#include <QColor>
#include <QPalette>

namespace shop {

inline void applyPosTheme(QApplication& app)
{
    app.setStyle(QStringLiteral("Fusion"));

    QPalette palette = app.palette();
    palette.setColor(QPalette::Window, QColor("#1a1b16"));
    palette.setColor(QPalette::WindowText, QColor("#f1efe4"));
    palette.setColor(QPalette::Base, QColor("#1f2119"));
    palette.setColor(QPalette::AlternateBase, QColor("#26291f"));
    palette.setColor(QPalette::Text, QColor("#f1efe4"));
    palette.setColor(QPalette::PlaceholderText, QColor("#a9ad98"));
    palette.setColor(QPalette::Button, QColor("#2d3024"));
    palette.setColor(QPalette::ButtonText, QColor("#f1efe4"));
    palette.setColor(QPalette::Highlight, QColor("#424a2f"));
    palette.setColor(QPalette::HighlightedText, QColor("#f6f4e8"));
    palette.setColor(QPalette::Accent, QColor("#c2cf93"));
    palette.setColor(QPalette::Link, QColor("#c2cf93"));
    palette.setColor(QPalette::ToolTipBase, QColor("#2d3024"));
    palette.setColor(QPalette::ToolTipText, QColor("#f1efe4"));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#939686"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#939686"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#939686"));
    app.setPalette(palette);

    app.setStyleSheet(QStringLiteral(R"(
        QMainWindow, QDialog, QWidget#posHome { background: #1a1b16; }
        QLabel { color: #f1efe4; }
        QLabel[muted="true"], QLabel#scanHint { color: #b8bba7; }
        QLabel#pageTitle, QLabel#managerHeading, QLabel#scanTitle {
            color: #faf7ed; font-size: 26px; font-weight: 700;
        }
        QLabel[heading="true"], QLabel#scanResult {
            color: #faf7ed; font-size: 16px; font-weight: 600;
        }
        QWidget#posHome QLabel#pageTitle {
            text-decoration: underline;
        }
        QLabel#cashierRole {
            color: #d6dfa9; background: #2c3322; border: 1px solid #4b5739;
            border-radius: 7px; padding: 6px 12px; font-weight: 600;
        }
        QFrame#managerAccessCard, QFrame#orderSummary { background: #292d21; }
        QFrame#payablePanel {
            background: #353d2a; border: 1px solid #535e41; border-radius: 8px;
        }
        QListWidget#managerNavigation {
            background: #23261c; padding: 5px; outline: none;
        }
        QListWidget#managerNavigation::item {
            border-radius: 5px; padding: 11px 9px;
        }
        QListWidget#managerNavigation::item:selected {
            background: #444e33; color: #f6f4e8;
        }
        QListWidget#managerNavigation::item:hover:!selected {
            background: #31372a;
        }
        QLabel#pageHeading { color: #faf7ed; font-size: 19px; font-weight: 600; }
        QLabel#managerPinError { color: #f0aba0; }
        QLabel#payableAmount { color: #d6dfa9; font-size: 24px; font-weight: 700; }
        QFrame[panel="true"], QFrame#scanFooter {
            background: #24261d; border: 1px solid #414536; border-radius: 12px;
        }
        QLabel#barcodePreview {
            background: #fbfaf3; color: #4a4b3d; border-radius: 8px;
        }
        QLineEdit {
            background: #1e2018; color: #f1efe4; border: 1px solid #535844;
            border-radius: 7px; padding: 8px 11px;
            selection-background-color: #535f39; selection-color: #faf7ed;
        }
        QPushButton {
            background: #2d3024; color: #f1efe4; border: 1px solid #4b503c;
            border-radius: 8px; padding: 8px 13px; font-weight: 600;
        }
        QPushButton:enabled:hover { background: #383e2a; border-color: #929f6b; }
        QPushButton:enabled:pressed { background: #464e33; }
        QPushButton:focus, QLineEdit:focus { border-color: #c2cf93; }
        QPushButton[role="primary"]:enabled {
            background: #c2cf93; color: #202717; border-color: #c2cf93;
        }
        QPushButton[role="primary"]:enabled:hover { background: #d5dfa8; }
        QPushButton[role="primary"]:enabled:pressed { background: #aebd79; }
        QPushButton[role="primary"]:focus { border-color: #f5f4d7; }
        QPushButton[role="tile"] {
            text-align: left; padding: 14px; font-size: 14px; border-radius: 12px;
        }
        QPushButton#newOrder:enabled {
            background: #343e27; border-color: #879762; color: #ebf0d6; font-size: 16px;
        }
        QPushButton#newOrder:enabled:hover { background: #465334; }
        QPushButton#newOrder:enabled:pressed { background: #52623d; }
        QPushButton[role="row"] { padding: 5px 8px; margin: 3px; }
        QPushButton[role="remove"] { padding: 5px 8px; margin: 3px; color: #f0aba0; }
        QPushButton:disabled {
            background: #25271e; color: #939686; border-color: #3b3e31;
        }
        QListWidget, QTableWidget {
            background: #1f2119; alternate-background-color: #26291f; color: #f1efe4;
            gridline-color: #414536; border: 1px solid #414536; border-radius: 8px;
            selection-background-color: #424a2f; selection-color: #f6f4e8;
        }
        QListWidget::item { padding: 9px; }
        QTableWidget::item { padding: 6px 10px; }
        QHeaderView::section {
            background: #303426; color: #dedfcf; border: 0; padding: 9px 10px;
        }
    )"));
}

} // namespace shop
