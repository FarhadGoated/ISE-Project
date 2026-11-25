#pragma once

#include <QImage>
#include <QMainWindow>

namespace shop {

    void openProductLookup(QWidget* parent, const QString& dataDir);

    class MainWindow : public QMainWindow {
    public:
        explicit MainWindow(QWidget* parent = nullptr);

    private:
        QImage previewImage_;
    };

}