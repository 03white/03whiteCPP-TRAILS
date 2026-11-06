#include "mainwindow.h"

#include <QBuffer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMetaObject>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <stdexcept>

namespace {

QString stateName(imgpipe::StepState state) {
    using imgpipe::StepState;
    switch (state) {
        case StepState::Pending:   return "等待";
        case StepState::Running:   return "执行中";
        case StepState::Succeeded: return "成功";
        case StepState::Failed:    return "失败";
        case StepState::Skipped:   return "跳过";
        case StepState::Cancelled: return "已取消";
    }
    return "未知";
}

QColor stateColor(imgpipe::StepState state) {
    using imgpipe::StepState;
    switch (state) {
        case StepState::Pending:   return QColor(0xF0, 0xF0, 0xF0);
        case StepState::Running:   return QColor(0xFF, 0xF3, 0xCD);
        case StepState::Succeeded: return QColor(0xD4, 0xED, 0xDA);
        case StepState::Failed:    return QColor(0xF8, 0xD7, 0xDA);
        case StepState::Skipped:   return QColor(0xE2, 0xE3, 0xE5);
        case StepState::Cancelled: return QColor(0xD6, 0xD8, 0xDB);
    }
    return Qt::white;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    auto* central = new QWidget(this);

    chooseButton_ = new QPushButton("选择图片...", central);
    startButton_ = new QPushButton("开始", central);
    cancelButton_ = new QPushButton("取消", central);
    startButton_->setEnabled(false);
    cancelButton_->setEnabled(false);

    table_ = new QTableWidget(0, 2, central);
    table_->setHorizontalHeaderLabels({"步骤", "状态"});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);

    preview_ = new QLabel("预览", central);
    preview_->setMinimumSize(360, 360);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setStyleSheet("border: 1px solid #cccccc; background: #fafafa;");

    progress_ = new QProgressBar(central);
    progress_->setRange(0, 1);
    progress_->setValue(0);

    log_ = new QPlainTextEdit(central);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(500);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(chooseButton_);
    buttons->addWidget(startButton_);
    buttons->addWidget(cancelButton_);
    buttons->addStretch(1);

    auto* middle = new QHBoxLayout;
    middle->addWidget(table_, 1);
    middle->addWidget(preview_, 1);

    auto* root = new QVBoxLayout(central);
    root->addLayout(buttons);
    root->addLayout(middle, 1);
    root->addWidget(progress_);
    root->addWidget(log_, 1);

    setCentralWidget(central);
    setWindowTitle("imgpipe Qt Demo");
    resize(960, 680);

    connect(chooseButton_, &QPushButton::clicked, this, &MainWindow::chooseImage);
    connect(startButton_, &QPushButton::clicked, this, &MainWindow::startRun);
    connect(cancelButton_, &QPushButton::clicked, this, &MainWindow::cancelRun);
}

MainWindow::~MainWindow() = default;

void MainWindow::chooseImage() {
    const QString path = QFileDialog::getOpenFileName(
        this, "选择图片", QString(),
        "图片 (*.png *.jpg *.jpeg *.bmp *.webp);;所有文件 (*)");
    if (path.isEmpty()) return;

    QImage image(path);
    if (image.isNull()) {
        log_->appendPlainText("无法加载图片: " + path);
        return;
    }

    inputPath_ = path;
    log_->appendPlainText("选择图片: " + path);
    preview_->setPixmap(QPixmap::fromImage(image).scaled(
        preview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    updateControls();
}

void MainWindow::startRun() {
    if (running_ || inputPath_.isEmpty()) return;

    ++runId_;
    const quint64 run = runId_;

    job_ = std::make_shared<Job>();
    job_->inputPath = inputPath_;
    const QFileInfo info(inputPath_);
    job_->outputPath =
        info.absolutePath() + "/" + info.completeBaseName() + "_imgpipe.png";

    pipeline_ = imgpipe::Pipeline::Create();
    rowOf_.clear();
    nameOf_.clear();
    terminal_.clear();
    doneCount_ = 0;
    total_ = 0;
    failed_ = false;
    table_->setRowCount(0);
    progress_->setValue(0);
    log_->clear();

    const auto load = addStep("1. 加载", [](Job& job) {
        QImage image(job.inputPath);
        if (image.isNull()) throw std::runtime_error("加载图片失败");
        job.image = image;
    });

    const auto convert = addStep("2. 转换", [](Job& job) {
        job.image = job.image.convertToFormat(QImage::Format_ARGB32);
        if (job.image.isNull()) throw std::runtime_error("格式转换失败");
    });

    const auto resize = addStep("3. 缩放", [](Job& job) {
        job.image = job.image.scaled(640, 640, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
        if (job.image.isNull()) throw std::runtime_error("缩放失败");
    });

    const auto watermark = addStep("4. 水印", [](Job& job) {
        QPainter painter(&job.image);
        painter.setRenderHint(QPainter::Antialiasing);
        QFont font = painter.font();
        font.setPointSize(24);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor(255, 255, 255, 220));
        painter.drawText(job.image.rect().adjusted(16, 16, -16, -16),
                         Qt::AlignRight | Qt::AlignBottom, "imgpipe");
        painter.end();
    });

    const auto encode = addStep("5. 编码", [](Job& job) {
        job.encoded.clear();
        QBuffer buffer(&job.encoded);
        if (!buffer.open(QIODevice::WriteOnly) ||
            !job.image.save(&buffer, "PNG")) {
            throw std::runtime_error("PNG 编码失败");
        }
    });

    const auto save = addStep("6. 保存", [](Job& job) {
        QFile file(job.outputPath);
        if (!file.open(QIODevice::WriteOnly))
            throw std::runtime_error("无法写入输出文件");
        if (file.write(job.encoded) != job.encoded.size())
            throw std::runtime_error("写入输出文件失败");
    });

    pipeline_->AddDependency(convert, load);
    pipeline_->AddDependency(resize, convert);
    pipeline_->AddDependency(watermark, resize);
    pipeline_->AddDependency(encode, watermark);
    pipeline_->AddDependency(save, encode);

    pipeline_->SetListener([this, run](imgpipe::StepId id, imgpipe::StepState state,
                                       const imgpipe::Error& err) {
        // 回调在工作线程：把数据和字符串拷出来，再切回 GUI 线程
        const int code = err.code;
        const QString message = QString::fromUtf8(err.message.c_str());
        QMetaObject::invokeMethod(
            this,
            [this, run, id, state, code, message] {
                onStep(run, id, state, code, message);
            },
            Qt::QueuedConnection);
    });

    progress_->setRange(0, total_);
    running_ = true;
    updateControls();
    log_->appendPlainText("开始执行，输出: " + job_->outputPath);

    if (!pipeline_->RunAsync()) {
        running_ = false;
        log_->appendPlainText("启动失败：已有执行在运行");
        updateControls();
    }
}

void MainWindow::cancelRun() {
    if (pipeline_ && running_) {
        log_->appendPlainText("请求取消...");
        pipeline_->Cancel();
    }
}

imgpipe::StepId MainWindow::addStep(const QString& name,
                                    std::function<void(Job&)> fn) {
    auto job = job_;
    const imgpipe::StepId id =
        pipeline_->AddStep(name.toStdString(), [job, fn] { fn(*job); });

    const int row = table_->rowCount();
    table_->insertRow(row);
    table_->setItem(row, 0, new QTableWidgetItem(name));
    table_->setItem(row, 1, new QTableWidgetItem("等待"));

    rowOf_.insert(id, row);
    nameOf_.insert(id, name);
    terminal_.append(false);
    ++total_;
    return id;
}

void MainWindow::setRowState(int row, imgpipe::StepState state) {
    QTableWidgetItem* item = table_->item(row, 1);
    if (!item) {
        item = new QTableWidgetItem;
        table_->setItem(row, 1, item);
    }
    item->setText(stateName(state));
    item->setBackground(stateColor(state));
}

void MainWindow::onStep(quint64 runId, imgpipe::StepId id,
                        imgpipe::StepState state, int code,
                        const QString& message) {
    if (runId != runId_) return;   // 上一轮残留的通知，忽略

    const int row = rowOf_.value(id, -1);
    if (row < 0) return;

    setRowState(row, state);

    if (state == imgpipe::StepState::Failed) failed_ = true;

    QString line = nameOf_.value(id) + " -> " + stateName(state);
    if (code != 0) line += QString(" (%1: %2)").arg(code).arg(message);
    log_->appendPlainText(line);

    const bool terminal =
        state == imgpipe::StepState::Succeeded ||
        state == imgpipe::StepState::Failed ||
        state == imgpipe::StepState::Skipped ||
        state == imgpipe::StepState::Cancelled;

    if (terminal && !terminal_.value(row, false)) {
        terminal_[row] = true;
        ++doneCount_;
        progress_->setValue(doneCount_);
    }

    if (doneCount_ >= total_ && running_) {
        running_ = false;
        updateControls();

        if (!failed_ && job_ && !job_->image.isNull()) {
            preview_->setPixmap(QPixmap::fromImage(job_->image).scaled(
                preview_->size(), Qt::KeepAspectRatio,
                Qt::SmoothTransformation));
            log_->appendPlainText("输出: " + job_->outputPath);
        }
        log_->appendPlainText(failed_ ? "执行结束（存在失败）" : "执行完成");
    }
}

void MainWindow::updateControls() {
    chooseButton_->setEnabled(!running_);
    startButton_->setEnabled(!running_ && !inputPath_.isEmpty());
    cancelButton_->setEnabled(running_);
}
