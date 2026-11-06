#define IMGPIPE_C_BUILD
#include "image_pipeline_c.h"

#include "image_pipeline.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace {

using imgpipe::Error;
using imgpipe::Pipeline;
using imgpipe::StepId;
using imgpipe::StepState;

imgpipe_step_state toC(StepState state) {
    switch (state) {
        case StepState::Pending:   return IMGPIPE_STEP_PENDING;
        case StepState::Running:   return IMGPIPE_STEP_RUNNING;
        case StepState::Succeeded: return IMGPIPE_STEP_SUCCEEDED;
        case StepState::Failed:    return IMGPIPE_STEP_FAILED;
        case StepState::Skipped:   return IMGPIPE_STEP_SKIPPED;
        case StepState::Cancelled: return IMGPIPE_STEP_CANCELLED;
    }
    return IMGPIPE_STEP_FAILED;
}

}  // namespace

// 不透明句柄：C 侧只能拿到指针。
struct imgpipe_handle {
    std::mutex listenerMutex;
    imgpipe_listener_fn listener = nullptr;
    void* listenerData = nullptr;

    // 最后声明：析构时最先销毁，先 join worker 再销毁其他成员
    std::unique_ptr<Pipeline> pipeline;
};

extern "C" {

imgpipe_handle* imgpipe_create(uint32_t maxConcurrency) {
    try {
        auto handle = std::make_unique<imgpipe_handle>();

        imgpipe::Config cfg;
        cfg.maxConcurrency = maxConcurrency;
        handle->pipeline = Pipeline::Create(cfg);

        // C++ 监听器转发到 C 回调；每次从 handle 里取最新的 C 回调
        auto* raw = handle.get();
        handle->pipeline->SetListener([raw](StepId id, StepState state, const Error& err) {
            imgpipe_listener_fn fn = nullptr;
            void* data = nullptr;
            {
                std::lock_guard<std::mutex> lk(raw->listenerMutex);
                fn = raw->listener;
                data = raw->listenerData;
            }
            if (!fn) return;

            imgpipe_error cErr{err.code, err.message.c_str()};
            fn(id, toC(state), cErr, data);
        });

        return handle.release();
    } catch (...) {
        return nullptr;
    }
}

void imgpipe_destroy(imgpipe_handle* handle) {
    if (!handle) return;

    // 先注销 C 回调，避免析构过程中的 Cancelled/Skipped 通知打到用户
    {
        std::lock_guard<std::mutex> lk(handle->listenerMutex);
        handle->listener = nullptr;
        handle->listenerData = nullptr;
    }
    delete handle;
}

imgpipe_step_id imgpipe_add_step(imgpipe_handle* handle,
                                 const char* name,
                                 imgpipe_step_fn fn,
                                 void* user_data) {
    if (!handle || !handle->pipeline || !fn) return IMGPIPE_INVALID_STEP_ID;

    try {
        std::string stepName = name ? name : "";
        return handle->pipeline->AddStep(
            std::move(stepName),
            [fn, user_data] { fn(user_data); });
    } catch (...) {
        return IMGPIPE_INVALID_STEP_ID;
    }
}

int imgpipe_add_dependency(imgpipe_handle* handle,
                           imgpipe_step_id downstream,
                           imgpipe_step_id upstream) {
    if (!handle || !handle->pipeline) return -1;

    try {
        handle->pipeline->AddDependency(downstream, upstream);
        return 0;
    } catch (...) {
        return -1;
    }
}

void imgpipe_set_listener(imgpipe_handle* handle,
                          imgpipe_listener_fn fn,
                          void* user_data) {
    if (!handle) return;

    std::lock_guard<std::mutex> lk(handle->listenerMutex);
    handle->listener = fn;
    handle->listenerData = fn ? user_data : nullptr;
}

int imgpipe_run(imgpipe_handle* handle) {
    if (!handle || !handle->pipeline) return -1;

    try {
        return handle->pipeline->RunAsync() ? 0 : 1;
    } catch (...) {
        return -1;
    }
}

int imgpipe_run_sync(imgpipe_handle* handle) {
    if (!handle || !handle->pipeline) return -1;

    try {
        return handle->pipeline->Run() ? 0 : 1;
    } catch (...) {
        return -1;
    }
}

void imgpipe_cancel(imgpipe_handle* handle) {
    if (handle && handle->pipeline) handle->pipeline->Cancel();
}

void imgpipe_wait(imgpipe_handle* handle) {
    if (handle && handle->pipeline) handle->pipeline->Wait();
}

}  // extern "C"
