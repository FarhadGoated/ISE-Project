#include "SeasonsDialog.h"

#include "SeasonDiscounts.h"

#include <QCheckBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtMath>

namespace shop {
    namespace {

        enum Column { StartDate, EndDate, Percentage, Active, Remove, ColumnCount };

        class SeasonsPage final : public QWidget {
        public:
            SeasonsPage(const QString& dataDir, QWidget* parent)
                : QWidget(parent), dataDir_(dataDir)
            {
                auto* layout = new QVBoxLayout(this);
                layout->setSpacing(12);

                auto* header = new QHBoxLayout;
                auto* title = new QLabel(QStringLiteral("Seasonal Discounts"), this);
                title->setObjectName(QStringLiteral("pageHeading"));
                addButton_ = new QPushButton(QStringLiteral("Add Season"), this);
                header->addWidget(title, 1);
                header->addWidget(addButton_);
                layout->addLayout(header);

                table_ = new QTableWidget(0, ColumnCount, this);
                table_->setHorizontalHeaderLabels({
                    QStringLiteral("Start date"), QStringLiteral("End date"),
                    QStringLiteral("Discount"), QStringLiteral("Active"), QString()
                    });
                table_->setAlternatingRowColors(true);
                table_->setSelectionMode(QAbstractItemView::NoSelection);
                table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
                table_->verticalHeader()->hide();
                table_->verticalHeader()->setDefaultSectionSize(40);
                auto* columns = table_->horizontalHeader();
                columns->setSectionResizeMode(QHeaderView::Fixed);
                columns->setSectionResizeMode(StartDate, QHeaderView::Stretch);
                columns->setSectionResizeMode(EndDate, QHeaderView::Stretch);
                table_->setColumnWidth(Percentage, 104);
                table_->setColumnWidth(Active, 69);
                table_->setColumnWidth(Remove, 90);
                layout->addWidget(table_, 1);

                status_ = new QLabel(this);
                status_->setTextFormat(Qt::PlainText);
                status_->setWordWrap(true);
                status_->setMinimumHeight(status_->fontMetrics().height());
                layout->addWidget(status_);

                auto* actions = new QHBoxLayout;
                discardButton_ = new QPushButton(QStringLiteral("Discard Changes"), this);
                saveButton_ = new QPushButton(QStringLiteral("Save Changes"), this);
                saveButton_->setProperty("role", "primary");
                actions->addStretch();
                actions->addWidget(discardButton_);
                actions->addWidget(saveButton_);
                layout->addLayout(actions);

                for (auto* button : { addButton_, discardButton_, saveButton_ }) {
                    button->setAutoDefault(false);
                }
                connect(addButton_, &QPushButton::clicked, this, [this] {
                    const QDate today = QDate::currentDate();
                    addRow({ today, today, 1000, true });
                    table_->scrollToBottom();
                    markChanged();
                    });
                connect(discardButton_, &QPushButton::clicked, this, [this] { loadRows(); });
                connect(saveButton_, &QPushButton::clicked, this, [this] { saveRows(); });
                loadRows();
            }

        private:
            QDateEdit* dateEditor(const QDate& date, const QString& name)
            {
                auto* editor = new QDateEdit(table_);
                editor->setDateRange(QDate(1, 1, 1), QDate(9999, 12, 31));
                editor->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
                editor->setCalendarPopup(true);
                editor->setAccessibleName(name);
                editor->setDate(date);
                return editor;
            }

            void addRow(const SeasonDiscount& season)
            {
                const int row = table_->rowCount();
                table_->insertRow(row);
                auto* start = dateEditor(season.startDate, QStringLiteral("Start date"));
                auto* end = dateEditor(season.endDate, QStringLiteral("End date"));
                end->setMinimumDate(season.startDate);

                auto* percentage = new QDoubleSpinBox(table_);
                percentage->setRange(0.0, 100.0);
                percentage->setDecimals(2);
                percentage->setSuffix(QStringLiteral(" %"));
                percentage->setAccessibleName(QStringLiteral("Discount percentage"));
                percentage->setValue(season.basisPoints / 100.0);

                auto* activeCell = new QWidget(table_);
                auto* activeLayout = new QHBoxLayout(activeCell);
                activeLayout->setContentsMargins(0, 0, 0, 0);
                auto* active = new QCheckBox(activeCell);
                active->setAccessibleName(QStringLiteral("Season active"));
                active->setChecked(season.active);
                activeLayout->addWidget(active, 0, Qt::AlignCenter);

                auto* remove = new QPushButton(QStringLiteral("Remove"), table_);
                remove->setProperty("role", "remove");
                remove->setAutoDefault(false);
                table_->setCellWidget(row, StartDate, start);
                table_->setCellWidget(row, EndDate, end);
                table_->setCellWidget(row, Percentage, percentage);
                table_->setCellWidget(row, Active, activeCell);
                table_->setCellWidget(row, Remove, remove);

                connect(start, &QDateEdit::dateChanged, this, [this, end](const QDate& date) {
                    end->setMinimumDate(date);
                    markChanged();
                    });
                connect(end, &QDateEdit::dateChanged, this, [this] { markChanged(); });
                connect(percentage, &QDoubleSpinBox::valueChanged, this, [this] { markChanged(); });
                connect(active, &QCheckBox::toggled, this, [this] { markChanged(); });
                connect(remove, &QPushButton::clicked, this, [this, remove] {
                    for (int row = 0; row < table_->rowCount(); ++row) {
                        if (table_->cellWidget(row, Remove) == remove) {
                            table_->removeRow(row);
                            markChanged();
                            return;
                        }
                    }
                    });
            }

            void loadRows()
            {
                QVector<SeasonDiscount> seasons;
                QString error;
                const bool loaded = loadSeasons(dataDir_, seasons, error);
                addButton_->setEnabled(loaded);
                table_->setEnabled(loaded);
                saveButton_->setEnabled(false);
                discardButton_->setEnabled(!loaded);
                discardButton_->setText(loaded ? QStringLiteral("Discard Changes") : QStringLiteral("Reload"));
                if (!loaded) {
                    status_->setText(error);
                    return;
                }

                table_->setRowCount(0);
                for (const SeasonDiscount& season : seasons) {
                    addRow(season);
                }
                status_->setText(seasons.isEmpty() ? QStringLiteral("No seasons configured.") : QString());
            }

            void markChanged()
            {
                saveButton_->setEnabled(true);
                discardButton_->setEnabled(true);
                status_->clear();
            }

            void saveRows()
            {
                QVector<SeasonDiscount> seasons;
                seasons.reserve(table_->rowCount());
                for (int row = 0; row < table_->rowCount(); ++row) {
                    auto* start = static_cast<QDateEdit*>(table_->cellWidget(row, StartDate));
                    auto* end = static_cast<QDateEdit*>(table_->cellWidget(row, EndDate));
                    auto* percentage = static_cast<QDoubleSpinBox*>(table_->cellWidget(row, Percentage));
                    const auto* active = table_->cellWidget(row, Active)->findChild<QCheckBox*>();
                    start->interpretText();
                    end->interpretText();
                    percentage->interpretText();
                    seasons.append(SeasonDiscount{
                        start->date(), end->date(), qRound(percentage->value() * 100.0), active->isChecked()
                        });
                }

                QString error;
                if (!saveSeasons(dataDir_, seasons, error)) {
                    status_->setText(error);
                    return;
                }
                saveButton_->setEnabled(false);
                discardButton_->setEnabled(false);
                status_->setText(QStringLiteral("Seasonal discounts saved."));
            }

            QString dataDir_;
            QTableWidget* table_ = nullptr;
            QLabel* status_ = nullptr;
            QPushButton* addButton_ = nullptr;
            QPushButton* discardButton_ = nullptr;
            QPushButton* saveButton_ = nullptr;
        };

    } // namespace

    QWidget* makeSeasonsPage(const QString& dataDir, QWidget* parent)
    {
        return new SeasonsPage(dataDir, parent);
    }

} // namespace shop
