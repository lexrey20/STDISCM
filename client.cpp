#include <atomic>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <chrono>

#include <grpcpp/grpcpp.h>
#include "ocr.grpc.pb.h"

// GUI IMPLEMENTATION --------------------------------------------------------
#include <QApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QProgressBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>
#include <QPixmap>
//----------------------------------------------------------------------------

using ocr::OCRService;
using ocr::ProcessImageRequest;
using ocr::ProcessImageResponse;

class ImageTextExtractor {
public:
    ImageTextExtractor(const std::string& server_endpoint)
        : service_stub_(OCRService::NewStub(
            grpc::CreateChannel(server_endpoint, grpc::InsecureChannelCredentials()))) {}

//INTERPROCESS COMMUNICATION ---------------------------------------------------
    ProcessImageResponse extractFromImage(const std::string& session_identifier,
                                         const std::string& job_group_id,
                                         const std::string& file_path,
                                         const std::vector<unsigned char>& image_data,
                                         int max_wait_seconds = 120) {
        ProcessImageRequest extraction_request;
        extraction_request.set_client_id(session_identifier);
        extraction_request.set_batch_id(job_group_id);
        extraction_request.set_filename(file_path);
        extraction_request.set_image(image_data.data(), image_data.size());
        extraction_request.set_lang("eng");

        ProcessImageResponse extraction_response;
        grpc::ClientContext client_context;

        auto timeout_point = std::chrono::system_clock::now() + 
                           std::chrono::seconds(max_wait_seconds);
        client_context.set_deadline(timeout_point);

        grpc::Status operation_status = service_stub_->ProcessImage(
            &client_context, extraction_request, &extraction_response);
        
        if (!operation_status.ok()) {
            extraction_response.set_ok(false);
            extraction_response.set_message(operation_status.error_message());
        }
        return extraction_response;
    }
//----------------------------------------------------------------------------

private:
    std::unique_ptr<OCRService::Stub> service_stub_;
};

static bool loadImageData(const std::string& file_location, 
                         std::vector<unsigned char>& data_buffer) {
    std::ifstream input_file(file_location, std::ios::binary);
    if (!input_file.is_open()) return false;
    
    input_file.seekg(0, std::ios::end);
    std::streampos file_size = input_file.tellg();
    input_file.seekg(0, std::ios::beg);
    
    data_buffer.resize(static_cast<size_t>(file_size));
    if (file_size > 0) {
        input_file.read(reinterpret_cast<char*>(data_buffer.data()), file_size);
    }
    return true;
}

QString filterLettersOnly(const QString& input) {
    QString result;
    for (QChar ch : input) {
        if (ch.isLetter() || ch.isSpace()) {
            result.append(ch);
        }
    }
    return result;
}

class TextExtractionUI : public QMainWindow {
    Q_OBJECT
public:
    TextExtractionUI(const std::string& server_endpoint, QWidget* parent = nullptr)
        : QMainWindow(parent), extractor_(server_endpoint), 
          client_session_id_("session_1"), job_sequence_(0),
          total_tasks_(0), completed_tasks_(0) {
        
        QWidget* main_container = new QWidget(this);
        QVBoxLayout* vertical_layout = new QVBoxLayout(main_container);

        QHBoxLayout* button_container = new QHBoxLayout();
        QPushButton* add_images_button = new QPushButton("+ Add Image Files", this);
        add_images_button->setMinimumHeight(40);
        QPushButton* clear_results_button = new QPushButton("Clear All", this);
        clear_results_button->setMinimumHeight(40);
        button_container->addWidget(add_images_button);
        button_container->addWidget(clear_results_button);
        vertical_layout->addLayout(button_container);

        status_label = new QLabel("Ready to process images", this);
        status_label->setAlignment(Qt::AlignCenter);
        status_label->setStyleSheet("font-weight: bold; padding: 5px;");
        vertical_layout->addWidget(status_label);

        task_progress = new QProgressBar(this);
        task_progress->setRange(0, 100);
        task_progress->setValue(0);
        task_progress->setTextVisible(true);      
        task_progress->setFormat("%p%");
        task_progress->setMinimumHeight(25);
        task_progress->setStyleSheet(
            "QProgressBar { text-align: center; color: black; }"
            "QProgressBar::chunk { background-color: #0078d4; }"
        );
        vertical_layout->addWidget(task_progress);

        results_display = new QTableWidget(this);
        results_display->setColumnCount(3);
        QStringList column_titles = {"Thumbnail", "Processing Status", "Extracted Text Preview"};
        results_display->setHorizontalHeaderLabels(column_titles);
        results_display->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        results_display->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        results_display->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        results_display->setAlternatingRowColors(true);
        vertical_layout->addWidget(results_display);

        setCentralWidget(main_container);
        setWindowTitle("Image Text Extraction Client");
        resize(1000, 650);

        connect(add_images_button, &QPushButton::clicked, 
                this, &TextExtractionUI::handleAddImages);
        connect(clear_results_button, &QPushButton::clicked,
                this, &TextExtractionUI::resetDisplay);
    }

private slots:
    void handleAddImages() {
        QStringList selected_files = QFileDialog::getOpenFileNames(
            this, "Choose Images to Process", "", 
            "Image Files (*.png *.jpg *.jpeg *.bmp);;All Files (*)");

        if (selected_files.isEmpty()) return;

        int new_files = selected_files.size();
        total_tasks_ += new_files;   
        updateProgressBar();

        status_label->setText(QString("Processing %1 image(s)...").arg(total_tasks_));

        for (const QString& file_path_qt : selected_files) {
            std::string full_path = file_path_qt.toStdString();
            int current_row = results_display->rowCount();
            results_display->insertRow(current_row);

            QLabel* thumbnail_label = new QLabel();
            QPixmap pix(file_path_qt);
            if (!pix.isNull()) {
                pix = pix.scaled(100, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                thumbnail_label->setPixmap(pix);
            }
            thumbnail_label->setAlignment(Qt::AlignCenter);
            thumbnail_label->setScaledContents(true);

            QWidget* thumb_widget = new QWidget();
            QHBoxLayout* thumb_layout = new QHBoxLayout(thumb_widget);
            thumb_layout->addWidget(thumbnail_label);
            thumb_layout->setContentsMargins(0, 0, 0, 0);
            results_display->setCellWidget(current_row, 0, thumb_widget);
            results_display->setRowHeight(current_row, 110);

            results_display->setItem(current_row, 1, new QTableWidgetItem("Waiting..."));
            results_display->setItem(current_row, 2, new QTableWidgetItem(""));


            std::vector<unsigned char> image_content;
            if (!loadImageData(full_path, image_content)) {
                results_display->setItem(current_row, 1, new QTableWidgetItem("Failed to read file"));
                completed_tasks_++;
                updateProgressBar();
                continue;
            }
            
            std::string current_batch_id = std::to_string(job_sequence_);

            std::thread([this, current_row, full_path, image_content, current_batch_id]() {

                QMetaObject::invokeMethod(this, [this, current_row]() {
                    results_display->setItem(current_row, 1, new QTableWidgetItem("Processing..."));
                }, Qt::QueuedConnection);

                const int simulation_steps = 20;
                for (int step = 0; step < simulation_steps; ++step) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    QMetaObject::invokeMethod(this, [this, step, simulation_steps]() {
                        updateSmoothProgress(step, simulation_steps);
                    }, Qt::QueuedConnection);
                }

                ProcessImageResponse extraction_result =
                    extractor_.extractFromImage(client_session_id_, current_batch_id, full_path, image_content, 120);

                QMetaObject::invokeMethod(this, [this, current_row, extraction_result]() {
                    if (extraction_result.ok()) {
                        results_display->setItem(current_row, 1, new QTableWidgetItem("Completed"));
                        QString extracted_text = QString::fromStdString(extraction_result.text());
                        extracted_text = filterLettersOnly(extracted_text);
                        if (extracted_text.length() > 350) {
                            extracted_text = extracted_text.left(350) + "...";
                        }
                        results_display->setItem(current_row, 2, new QTableWidgetItem(extracted_text));
                    } else {
                        results_display->setItem(current_row, 1,
                            new QTableWidgetItem("Error: " + QString::fromStdString(extraction_result.message())));
                        results_display->setItem(current_row, 2, new QTableWidgetItem(""));
                    }

                    completed_tasks_++;
                    updateProgressBar();

                    if (completed_tasks_ >= total_tasks_) {
                        status_label->setText("Processing complete");
                    }
                }, Qt::QueuedConnection);

            }).detach();
        }
    }

    void resetDisplay() {
        results_display->setRowCount(0);
        total_tasks_ = 0;
        completed_tasks_ = 0;
        task_progress->setValue(0);
        status_label->setText("Ready to process images");
    }

private:
    void updateSmoothProgress(int step, int steps_total) {
        if (total_tasks_ == 0) return;
        double base = (completed_tasks_.load() * 100.0) / total_tasks_;
        double inc = ((step + 1) / double(steps_total)) * (100.0 / total_tasks_);
        int percent = int(base + inc);
        if (percent > 100) percent = 100;
        task_progress->setValue(percent);
    }

    void updateProgressBar() {
        if (total_tasks_ == 0) {
            task_progress->setValue(0);
            return;
        }

        int percent = int((completed_tasks_.load() * 100.0) / total_tasks_);
        if (percent > 100) percent = 100;
        task_progress->setValue(percent);
    }

private:
    ImageTextExtractor extractor_;
    std::string client_session_id_;
    int job_sequence_;
    int total_tasks_;
    std::atomic<int> completed_tasks_;

    QLabel* status_label;
    QProgressBar* task_progress;
    QTableWidget* results_display;
};

#include "client.moc"

int main(int argc, char** argv) {
    QApplication extraction_app(argc, argv);
    
    std::string server_endpoint = "192.168.1.146:50051";
    if (argc >= 2) server_endpoint = argv[1];
    
    TextExtractionUI main_interface(server_endpoint);
    main_interface.show();
    
    return extraction_app.exec();
}