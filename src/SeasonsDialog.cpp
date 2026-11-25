#include "SeasonsDialog.h"
#include "BillCalculator.h"
#include "SeasonDiscounts.h"

#include <QDate>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QVector>

namespace shop {

namespace {

class SeasonsDialog : public QDialog {
public:
    SeasonsDialog(QWidget* parent, const QString& dataDir)
        : QDialog(parent), dataDir_(dataDir)
    {
        setWindowTitle(QStringLiteral("Seasonal discounts"));
        resize(560, 360);

        auto* layout = new QVBoxLayout(this);

        table_ = new QTableWidget(0, 4, this);
        table_->setHorizontalHeaderLabels(QStringList{QStringLiteral("Start date"),
                                                      QStringLiteral("End date"),
                                                      QStringLiteral("Percent"),
                                                      QStringLiteral("Active")});
        table_->horizontalHeader()->setStretchLastSection(true);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        layout->addWidget(table_);

        statusLabel_ = new QLabel(this);
        layout->addWidget(statusLabel_);

        auto* buttons = new QHBoxLayout();
        auto* addButton = new QPushButton(QStringLiteral("Add season"), this);
        auto* removeButton = new QPushButton(QStringLiteral("Remove selected"), this);
        auto* saveButton = new QPushButton(QStringLiteral("Save"), this);
        auto* cancelButton = new QPushButton(QStringLiteral("Cancel"), this);
        buttons->addWidget(addButton);
        buttons->addWidget(removeButton);
        buttons->addStretch(1);
        buttons->addWidget(saveButton);
        buttons->addWidget(cancelButton);
        layout->addLayout(buttons);

        connect(addButton, &QPushButton::clicked, this, [this] { onAdd(); });
        connect(removeButton, &QPushButton::clicked, this, [this] { onRemove(); });
        connect(saveButton, &QPushButton::clicked, this, [this] { onSave(); });
        connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

        loadRows();
    }

private:
    void loadRows()
    {
        QVector<SeasonDiscount> seasons;
        QString error;
        if (!loadSeasons(dataDir_, seasons, error)) {
            statusLabel_->setText(error);
            return;
        }
        for (const SeasonDiscount& season : seasons) {
            addRow(season);
        }
    }

    void addRow(const SeasonDiscount& season)
    {
        const int row = table_->rowCount();
        table_->insertRow(row);
        table_->setItem(row, 0, new QTableWidgetItem(season.startDate.toString(Qt::ISODate)));
        table_->setItem(row, 1, new QTableWidgetItem(season.endDate.toString(Qt::ISODate)));
        table_->setItem(row, 2, new QTableWidgetItem(
                                    BillCalculator::formatPercentBasisPoints(season.basisPoints)));

        auto* activeItem = new QTableWidgetItem();
        activeItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        activeItem->setCheckState(season.active ? Qt::Checked : Qt::Unchecked);
        table_->setItem(row, 3, activeItem);
    }

    void onAdd()
    {
        SeasonDiscount season;
        season.startDate = QDate::currentDate();
        season.endDate = QDate::currentDate();
        season.basisPoints = 1000;
        season.active = true;
        addRow(season);
        statusLabel_->clear();
    }

    void onRemove()
    {
        const int row = table_->currentRow();
        if (row >= 0) {
            table_->removeRow(row);
        }
        statusLabel_->clear();
    }

    void onSave()
    {
        QVector<SeasonDiscount> seasons;

        for (int row = 0; row < table_->rowCount(); ++row) {
            const QTableWidgetItem* start = table_->item(row, 0);
            const QTableWidgetItem* end = table_->item(row, 1);
            const QTableWidgetItem* percent = table_->item(row, 2);
            const QTableWidgetItem* active = table_->item(row, 3);
            if (!start || !end || !percent || !active) {
                statusLabel_->setText(QStringLiteral("Row %1 is incomplete.").arg(row + 1));
                return;
            }

            SeasonDiscount season;
            season.startDate = QDate::fromString(start->text().trimmed(), Qt::ISODate);
            season.endDate = QDate::fromString(end->text().trimmed(), Qt::ISODate);
            if (!BillCalculator::parsePercentBasisPoints(percent->text(), season.basisPoints)) {
                statusLabel_->setText(QStringLiteral("Row %1: the percentage is not a number.").arg(row + 1));
                return;
            }
            season.active = (active->checkState() == Qt::Checked);
            seasons.append(season);
        }

        QString error;
        if (!saveSeasons(dataDir_, seasons, error)) {
            statusLabel_->setText(error);
            return;
        }
        accept();
    }

    QString dataDir_;
    QTableWidget* table_ = nullptr;
    QLabel* statusLabel_ = nullptr;
};

}  // namespace

bool editSeasons(QWidget* parent, const QString& dataDir)
{
    SeasonsDialog dialog(parent, dataDir);
    return dialog.exec() == QDialog::Accepted;
}

} // namespace shop
