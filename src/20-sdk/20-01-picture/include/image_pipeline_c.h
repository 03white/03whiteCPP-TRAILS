#ifndef IMGPIPE_C_H
#define IMGPIPE_C_H

#include <stdint.h>

#if defined(_WIN32)
#  ifdef IMGPIPE_C_BUILD
#    define IMGPIPE_API __declspec(dllexport)
#  else
#    define IMGPIPE_API __declspec(dllimport)
#  endif
#else
#  define IMGPIPE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct imgpipe_handle imgpipe_handle;
typedef uint64_t imgpipe_step_id;

#define IMGPIPE_INVALID_STEP_ID ((imgpipe_step_id)-1)

typedef enum imgpipe_step_state {
    IMGPIPE_STEP_PENDING = 0,
    IMGPIPE_STEP_RUNNING = 1,
    IMGPIPE_STEP_SUCCEEDED = 2,
    IMGPIPE_STEP_FAILED = 3,
    IMGPIPE_STEP_SKIPPED = 4,
    IMGPIPE_STEP_CANCELLED = 5
} imgpipe_step_state;

typedef struct imgpipe_error {
    int code;
    const char* message;   /* 仅在回调期间有效，不要保存指针 */
} imgpipe_error;

/* 状态回调；可能在工作线程触发，需自行保证线程安全 */
typedef void (*imgpipe_listener_fn)(imgpipe_step_id id,
                                    imgpipe_step_state state,
                                    imgpipe_error error,
                                    void* user_data);

/* 步骤函数；失败时通过步骤函数内部自行处理，C 层没有异常 */
typedef void (*imgpipe_step_fn)(void* user_data);

IMGPIPE_API imgpipe_handle* imgpipe_create(uint32_t max_concurrency);
IMGPIPE_API void imgpipe_destroy(imgpipe_handle* handle);

IMGPIPE_API imgpipe_step_id imgpipe_add_step(imgpipe_handle* handle,
                                             const char* name,
                                             imgpipe_step_fn fn,
                                             void* user_data);
IMGPIPE_API int imgpipe_add_dependency(imgpipe_handle* handle,
                                       imgpipe_step_id downstream,
                                       imgpipe_step_id upstream);

IMGPIPE_API void imgpipe_set_listener(imgpipe_handle* handle,
                                      imgpipe_listener_fn fn,
                                      void* user_data);

/* 异步执行：立即返回；用 imgpipe_wait 收敛 */
IMGPIPE_API int imgpipe_run(imgpipe_handle* handle);
/* 同步执行：阻塞到全部步骤结束 */
IMGPIPE_API int imgpipe_run_sync(imgpipe_handle* handle);

IMGPIPE_API void imgpipe_cancel(imgpipe_handle* handle);
IMGPIPE_API void imgpipe_wait(imgpipe_handle* handle);

#ifdef __cplusplus
}
#endif

#endif /* IMGPIPE_C_H */
