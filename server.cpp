#include <chrono>
#include <condition_variable>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <grpcpp/grpcpp.h>
#include "ocr.grpc.pb.h"
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using ocr::OCRService;
using ocr::ProcessImageRequest;
using ocr::ProcessImageResponse;

struct OcrTask {
    std::string file_name;
    std::string language_code;
    std::vector<unsigned char> image_data;
    std::promise<std::string> text_promise;
    std::chrono::steady_clock::time_point task_start_time;
};

// MULTITHREADING -----------------------------------------------------------
class TaskProcessor {
public:
    TaskProcessor(size_t worker_count) : shutdown_requested_(false) {
        for (size_t i = 0; i < worker_count; ++i) {
            workers_.emplace_back(&TaskProcessor::processTasks, this);
        }
    }

    ~TaskProcessor() {
        stopProcessing();
    }
//----------------------------------------------------------------------------

// SYNCHRONIZATION -----------------------------------------------------------
    void submitTask(std::shared_ptr<OcrTask> task) {
        {
            std::lock_guard<std::mutex> guard(queue_mutex_);
            pending_tasks_.push(task);
            std::cout << "[Queue] Task submitted: " << task->file_name
                      << ", Pending tasks: " << pending_tasks_.size() << std::endl;
        }
        task_available_.notify_one();
    }

    void stopProcessing() {
        {
            std::lock_guard<std::mutex> guard(queue_mutex_);
            if (shutdown_requested_) return;
            shutdown_requested_ = true;
        }
        task_available_.notify_all();
        for (auto &worker_thread : workers_) {
            if (worker_thread.joinable()) worker_thread.join();
        }
    }
//----------------------------------------------------------------------------

private:
    void processTasks() {
        tesseract::TessBaseAPI ocr_engine;
        if (ocr_engine.Init("/opt/homebrew/share/tessdata", "eng")) {
            std::cerr << "[Worker " << std::this_thread::get_id()
                      << "] OCR engine initialization failed!" << std::endl;
        }

        while (true) {
            std::shared_ptr<OcrTask> current_task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                task_available_.wait(lock, [&] {
                    return shutdown_requested_ || !pending_tasks_.empty();
                });

                if (shutdown_requested_ && pending_tasks_.empty()) return;

                current_task = pending_tasks_.front();
                pending_tasks_.pop();

                std::cout << "[Queue] Task dequeued: " << current_task->file_name
                          << ", Pending tasks: " << pending_tasks_.size() << std::endl;
            }

            std::cout << "[Worker " << std::this_thread::get_id() 
                      << "] Started processing: " << current_task->file_name << std::endl;

            std::string extracted_text;

            try {
                Pix* image_pix = pixReadMem(current_task->image_data.data(),
                                            current_task->image_data.size());

                if (!image_pix) {
                    extracted_text.clear();
                    std::cout << "[Worker " << std::this_thread::get_id()
                              << "] Failed to read image: " << current_task->file_name << std::endl;
                } else {
                    // PREPROCESSING
                    Pix* gray_pix = pixConvertTo8(image_pix, 0);
                    pixDestroy(&image_pix);

                    Pix* enhanced_pix = pixGammaTRC(nullptr, gray_pix, 1.2f, 50, 180);
                    pixDestroy(&gray_pix);

                    ocr_engine.SetImage(enhanced_pix);

                    char* ocr_result = ocr_engine.GetUTF8Text();
                    if (ocr_result) {
                        extracted_text = std::string(ocr_result);
                        delete [] ocr_result;
                    }

                    pixDestroy(&enhanced_pix);
                }

            } catch (const std::exception& ex) {
                extracted_text = std::string("ERROR: ") + ex.what();
            } catch (...) {
                extracted_text = "ERROR: unknown exception";
            }

            std::cout << "[Worker " << std::this_thread::get_id() 
                      << "] Finished processing: " << current_task->file_name
                      << " (" << extracted_text.size() << " chars)" << std::endl;

            try {
                current_task->text_promise.set_value(extracted_text);
            } catch (...) {}
        }
    }

    std::queue<std::shared_ptr<OcrTask>> pending_tasks_;
    std::mutex queue_mutex_;
    std::condition_variable task_available_;
    std::vector<std::thread> workers_;
    bool shutdown_requested_;
};

// gRPC Service Implementation ----------------------------------------------------
class OCRServiceHandler final : public OCRService::Service {
public:
    OCRServiceHandler(TaskProcessor &processor) : task_processor_(processor) {}

    Status ProcessImage(ServerContext* context,
                        const ProcessImageRequest* request,
                        ProcessImageResponse* response) override {

        std::cout << "[Server] Received request for image: " << request->filename()
                  << " from client: " << request->client_id() << std::endl;

        auto new_task = std::make_shared<OcrTask>();
        new_task->file_name = request->filename();
        new_task->language_code = request->lang();
        new_task->task_start_time = std::chrono::steady_clock::now();
        new_task->image_data.assign(request->image().begin(), request->image().end());

        std::future<std::string> text_future = new_task->text_promise.get_future();
        task_processor_.submitTask(new_task);

        // FAULT TOLERANCE ---------------------------------------------------------
        auto status = text_future.wait_for(std::chrono::seconds(120));
        if (status == std::future_status::timeout) {
            std::cout << "[Server] Timeout processing image: " << request->filename() << std::endl;
            response->set_ok(false);
            response->set_message("Image processing timeout");
            return Status::OK;
        }
        // -------------------------------------------------------------------------

        std::string result_text = text_future.get();
        response->set_ok(true);
        response->set_text(result_text);

        long long processing_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - new_task->task_start_time).count();
        response->set_processing_time_ms(processing_time);

        std::cout << "[Server] Finished request for image: " << request->filename()
                  << ", Processing time: " << processing_time << " ms" << std::endl;

        return Status::OK;
    }

private:
    TaskProcessor &task_processor_;
};

// Main Function --------------------------------------------------------------
int main(int argc, char** argv) {
    size_t worker_threads = 4;
    if (argc >= 2) {
        try { worker_threads = std::stoul(argv[1]); }
        catch (...) { std::cerr << "Invalid worker count, using default 4.\n"; }
    }

    std::string endpoint = "0.0.0.0:50051";

    TaskProcessor processor(worker_threads);
    OCRServiceHandler handler(processor);

    ServerBuilder builder;
    builder.AddListeningPort(endpoint, grpc::InsecureServerCredentials());
    builder.RegisterService(&handler);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "OCR Server running at " << endpoint 
              << " with " << worker_threads << " workers.\n";

    server->Wait();
    processor.stopProcessing();
    return 0;
}
//----------------------------------------------------------------------------