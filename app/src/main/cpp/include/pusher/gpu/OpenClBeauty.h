#pragma once

#include <cstdint>

namespace pusher {

/**
 * 基于 OpenCL 的美颜磨皮模块。
 * 优先尝试动态加载 libOpenCL.so 使用 GPU 计算；设备不支持时回退 CPU。
 */
class OpenClBeauty {
public:
    bool init();
    void release();
    bool process(uint8_t* rgba, int width, int height);

private:
    bool processCpu(uint8_t* rgba, int width, int height);
    bool processOpenCl(uint8_t* rgba, int width, int height);

    void* lib_ = nullptr;
    bool clReady_ = false;

    // OpenCL 对象（不透明句柄）。
    void* platform_ = nullptr;
    void* device_ = nullptr;
    void* context_ = nullptr;
    void* queue_ = nullptr;
    void* program_ = nullptr;
    void* kernel_ = nullptr;

    // OpenCL 函数指针。
    using cl_int = int32_t;
    using cl_uint = uint32_t;
    using cl_ulong = uint64_t;
    using cl_bool = uint32_t;
    using cl_mem_flags = uint64_t;
    using cl_device_type = uint64_t;

    cl_int (*clGetPlatformIDs_)(cl_uint, void**, cl_uint*) = nullptr;
    cl_int (*clGetDeviceIDs_)(void*, cl_device_type, cl_uint, void**, cl_uint*) = nullptr;
    void* (*clCreateContext_)(const void*, cl_uint, void**, void*, void*, cl_int*) = nullptr;
    void* (*clCreateCommandQueue_)(void*, void*, cl_ulong, cl_int*) = nullptr;
    void* (*clCreateProgramWithSource_)(void*, cl_uint, const char**, const size_t*, cl_int*) = nullptr;
    cl_int (*clBuildProgram_)(void*, cl_uint, void**, const char*, void*, void*) = nullptr;
    void* (*clCreateKernel_)(void*, const char*, cl_int*) = nullptr;
    void* (*clCreateBuffer_)(void*, cl_mem_flags, size_t, void*, cl_int*) = nullptr;
    cl_int (*clSetKernelArg_)(void*, cl_uint, size_t, const void*) = nullptr;
    cl_int (*clEnqueueNDRangeKernel_)(void*, void*, cl_uint, const size_t*, const size_t*,
                                      const size_t*, cl_uint, const void*, void*) = nullptr;
    cl_int (*clEnqueueReadBuffer_)(void*, void*, cl_bool, size_t, size_t, void*, cl_uint,
                                   const void*, void*) = nullptr;
    cl_int (*clReleaseMemObject_)(void*) = nullptr;
    cl_int (*clReleaseKernel_)(void*) = nullptr;
    cl_int (*clReleaseProgram_)(void*) = nullptr;
    cl_int (*clReleaseCommandQueue_)(void*) = nullptr;
    cl_int (*clReleaseContext_)(void*) = nullptr;
};

}  // namespace pusher
