#pragma once

#include <QImage>
#include <QMainWindow>

namespace shop {

    class MainWindow : public QMainWindow {
    public:
        explicit MainWindow(QWidget* parent = nullptr);

    private:
        QImage previewImage_;
    };

}