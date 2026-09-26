#include "pusher/gpu/OpenClBeauty.h"

#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "pusher/common/Log.h"

namespace pusher {

namespace {

constexpr int kRadius = 2;
constexpr float kRangeSigmaSq = 0.02f;

const char* kBeautyKernel = R"(
__kernel void beauty(__global const uchar4* src, __global uchar4* dst, int w, int h) {
    int x = get_global_id(0);
    int y = get_global_id(1);
    if (x >= w || y >= h) return;
    int idx = y * w + x;
    float4 c = convert_float4(src[idx]);
    float4 sum = (float4)(0.0f, 0.0f, 0.0f, 0.0f);
    float wsum = 0.0f;
    for (int dy = -2; dy <= 2; ++dy) {
        int yy = clamp(y + dy, 0, h - 1);
        for (int dx = -2; dx <= 2; ++dx) {
            int xx = clamp(x + dx, 0, w - 1);
            float4 t = convert_float4(src[yy * w + xx]);
            float sp = (float)(dx * dx + dy * dy) / 2.0f;
            float w = exp(-sp);
            float3 diff = t.xyz - c.xyz;
            float range = dot(diff, diff);
            w *= exp(-range / 0.02f);
            sum += t * w;
            wsum += w;
        }
    }
    float4 blurred = sum / wsum;
    float4 diff = c - blurred;
    float4 out = blurred + diff * 0.75f;
    out.w = 255.0f;
    dst[idx] = convert_uchar4_sat(out);
}
)";

inline float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

}  // namespace

bool OpenClBeauty::init() {
    release();

    lib_ = dlopen("libOpenCL.so", RTLD_NOW);
    if (lib_ == nullptr) {
        LOGW("OpenClBeauty: libOpenCL.so not found, fallback to CPU");
        return false;
    }

#define LOAD_FN(name) \
    name##_ = reinterpret_cast<decltype(name##_)>(dlsym(lib_, #name)); \
    if (name##_ == nullptr) { \
        LOGW("OpenClBeauty: missing %s", #name); \
        release(); \
        return false; \
    }

    LOAD_FN(clGetPlatformIDs)
    LOAD_FN(clGetDeviceIDs)
    LOAD_FN(clCreateContext)
    LOAD_FN(clCreateCommandQueue)
    LOAD_FN(clCreateProgramWithSource)
    LOAD_FN(clBuildProgram)
    LOAD_FN(clCreateKernel)
    LOAD_FN(clCreateBuffer)
    LOAD_FN(clSetKernelArg)
    LOAD_FN(clEnqueueNDRangeKernel)
    LOAD_FN(clEnqueueReadBuffer)
    LOAD_FN(clReleaseMemObject)
    LOAD_FN(clReleaseKernel)
    LOAD_FN(clReleaseProgram)
    LOAD_FN(clReleaseCommandQueue)
    LOAD_FN(clReleaseContext)

#undef LOAD_FN

    cl_uint platformCount = 0;
    if (clGetPlatformIDs_(0, nullptr, &platformCount) != 0 || platformCount == 0) {
        LOGW("OpenClBeauty: no OpenCL platform");
        release();
        return false;
    }

    void* platforms[1] = {nullptr};
    if (clGetPlatformIDs_(1, platforms, nullptr) != 0) {
        release();
        return false;
    }
    platform_ = platforms[0];

    constexpr cl_device_type kGpu = 1 << 2;  // CL_DEVICE_TYPE_GPU
    cl_uint deviceCount = 0;
    if (clGetDeviceIDs_(platform_, kGpu, 0, nullptr, &deviceCount) != 0 || deviceCount == 0) {
        LOGW("OpenClBeauty: no GPU device");
        release();
        return false;
    }
    void* devices[1] = {nullptr};
    if (clGetDeviceIDs_(platform_, kGpu, 1, devices, nullptr) != 0) {
        release();
        return false;
    }
    device_ = devices[0];

    cl_int err = 0;
    context_ = clCreateContext_(nullptr, 1, &device_, nullptr, nullptr, &err);
    if (err != 0 || context_ == nullptr) {
        release();
        return false;
    }

    queue_ = clCreateCommandQueue_(context_, device_, 0, &err);
    if (err != 0 || queue_ == nullptr) {
        release();
        return false;
    }

    const char* source = kBeautyKernel;
    size_t sourceLen = strlen(source);
    program_ = clCreateProgramWithSource_(context_, 1, &source, &sourceLen, &err);
    if (err != 0 || program_ == nullptr) {
        release();
        return false;
    }
    if (clBuildProgram_(program_, 1, &device_, nullptr, nullptr, nullptr) != 0) {
        LOGW("OpenClBeauty: build program failed");
        release();
        return false;
    }

    kernel_ = clCreateKernel_(program_, "beauty", &err);
    if (err != 0 || kernel_ == nullptr) {
        release();
        return false;
    }

    clReady_ = true;
    LOGI("OpenClBeauty: OpenCL initialized");
    return true;
}

void OpenClBeauty::release() {
    if (kernel_ != nullptr) clReleaseKernel_(kernel_);
    if (program_ != nullptr) clReleaseProgram_(program_);
    if (queue_ != nullptr) clReleaseCommandQueue_(queue_);
    if (context_ != nullptr) clReleaseContext_(context_);
    kernel_ = program_ = queue_ = context_ = nullptr;
    device_ = platform_ = nullptr;
    if (lib_ != nullptr) {
        dlclose(lib_);
        lib_ = nullptr;
    }
    clReady_ = false;
}

bool OpenClBeauty::process(uint8_t* rgba, int width, int height) {
    if (rgba == nullptr || width <= 0 || height <= 0) return false;
    if (clReady_) {
        if (processOpenCl(rgba, width, height)) return true;
        LOGW("OpenClBeauty: OpenCL process failed, fallback to CPU");
    }
    return processCpu(rgba, width, height);
}

bool OpenClBeauty::processOpenCl(uint8_t* rgba, int width, int height) {
    const size_t bytes = static_cast<size_t>(width) * height * 4;
    cl_int err = 0;
    constexpr cl_mem_flags kReadWrite = (1 << 0) | (1 << 1);  // CL_MEM_READ_WRITE

    void* src = clCreateBuffer_(context_, kReadWrite, bytes, nullptr, &err);
    if (err != 0 || src == nullptr) return false;
    void* dst = clCreateBuffer_(context_, kReadWrite, bytes, nullptr, &err);
    if (err != 0 || dst == nullptr) {
        clReleaseMemObject_(src);
        return false;
    }

    // 直接使用 host 指针作为源需要 CL_MEM_COPY_HOST_PTR；这里简化使用入队写。
    if (clSetKernelArg_(kernel_, 0, sizeof(void*), &src) != 0 ||
        clSetKernelArg_(kernel_, 1, sizeof(void*), &dst) != 0 ||
        clSetKernelArg_(kernel_, 2, sizeof(int), &width) != 0 ||
        clSetKernelArg_(kernel_, 3, sizeof(int), &height) != 0) {
        clReleaseMemObject_(dst);
        clReleaseMemObject_(src);
        return false;
    }

    size_t global[2] = {static_cast<size_t>(width), static_cast<size_t>(height)};
    if (clEnqueueNDRangeKernel_(queue_, kernel_, 2, nullptr, global, nullptr, 0, nullptr, nullptr) != 0) {
        clReleaseMemObject_(dst);
        clReleaseMemObject_(src);
        return false;
    }

    if (clEnqueueReadBuffer_(queue_, dst, 1, 0, bytes, rgba, 0, nullptr, nullptr) != 0) {
        clReleaseMemObject_(dst);
        clReleaseMemObject_(src);
        return false;
    }

    clReleaseMemObject_(dst);
    clReleaseMemObject_(src);
    return true;
}

bool OpenClBeauty::processCpu(uint8_t* rgba, int width, int height) {
    std::vector<uint8_t> copy(rgba, rgba + static_cast<size_t>(width) * height * 4);
    const int radius = kRadius;
    const float rangeSq = kRangeSigmaSq;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f, wsum = 0.0f;
            uint8_t* c = &copy[(y * width + x) * 4];
            for (int dy = -radius; dy <= radius; ++dy) {
                int yy = std::clamp(y + dy, 0, height - 1);
                for (int dx = -radius; dx <= radius; ++dx) {
                    int xx = std::clamp(x + dx, 0, width - 1);
                    uint8_t* t = &copy[(yy * width + xx) * 4];
                    float sp = (dx * dx + dy * dy) / 2.0f;
                    float w = std::exp(-sp);
                    float dr = t[0] - c[0];
                    float dg = t[1] - c[1];
                    float db = t[2] - c[2];
                    float range = dr * dr + dg * dg + db * db;
                    w *= std::exp(-range / rangeSq);
                    sumR += t[0] * w;
                    sumG += t[1] * w;
                    sumB += t[2] * w;
                    wsum += w;
                }
            }
            uint8_t* out = &rgba[(y * width + x) * 4];
            float br = sumR / wsum, bg = sumG / wsum, bb = sumB / wsum;
            out[0] = static_cast<uint8_t>(clampf(br + (c[0] - br) * 0.75f, 0.0f, 255.0f));
            out[1] = static_cast<uint8_t>(clampf(bg + (c[1] - bg) * 0.75f, 0.0f, 255.0f));
            out[2] = static_cast<uint8_t>(clampf(bb + (c[2] - bb) * 0.75f, 0.0f, 255.0f));
            out[3] = 255;
        }
    }
    return true;
}

}  // namespace pusher
