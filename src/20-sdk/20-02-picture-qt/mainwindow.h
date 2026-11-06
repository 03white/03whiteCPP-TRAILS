#pragma once

#include <QHash>
#include <QImage>
#include <QMainWindow>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>

#include "image_pipeline.h"

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    struct Job {
        QString inputPath;
        QString outputPath;
        QImage image;
        QByteArray encoded;
    };

    void chooseImage();
    void startRun();
    void cancelRun();

    void buildRows();
    imgpipe::StepId addStep(const QString& name, std::function<void(Job&)> fn);
    void setRowState(int row, imgpipe::StepState state);
    void updateControls();

    void onStep(quint64 runId, imgpipe::StepId id, imgpipe::StepState state,
                int code, const QString& message);

    std::unique_ptr<imgpipe::Pipeline> pipeline_;
    std::shared_ptr<Job> job_;
    QString inputPath_;

    QHash<quint64, int> rowOf_;
    QHash<quint64, QString> nameOf_;
    QVector<bool> terminal_;
    int doneCount_ = 0;
    int total_ = 0;
    bool running_ = false;
    bool failed_ = false;
    quint64 runId_ = 0;

    QPushButton* chooseButton_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    QTableWidget* table_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* preview_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
};
