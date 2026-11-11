#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_ISEBarcodeProject.h"

class ISEBarcodeProject : public QMainWindow
{
    Q_OBJECT

public:
    ISEBarcodeProject(QWidget *parent = nullptr);
    ~ISEBarcodeProject();

private:
    Ui::ISEBarcodeProjectClass ui;
};

