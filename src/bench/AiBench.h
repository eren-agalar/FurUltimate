#pragma once
#include <d3d11.h>
#include <windows.h>
#include <cstdint>
#include <functional>
#include <string>

namespace aether {

// Progress callback: (fraction 0..1, current median TOPS) -> false to cancel.
using AiTick = std::function<bool(double, double)>;

struct AiMeasurement {
    bool ok = false;
    double tops = 0;          // median tera-operations per second (1 multiply-add = 2 operations)
    std::string precision;    // "FP32" | "FP16"
    std::string error;
};

// Path A: tiled FP32 matrix multiplication on Direct3D 11 compute shaders (runs on every GPU).
AiMeasurement runShaderGemm(ID3D11Device* dev, ID3D11DeviceContext* ctx, double seconds, const AiTick& tick);

// Path B: DirectML GEMM (Windows' machine-learning runtime). Uses FP16 when supported, which engages
// tensor / matrix units (e.g. NVIDIA Tensor Cores) through the driver. Returns ok=false when DirectML,
// Direct3D 12 or FP16 tensors are unavailable.
AiMeasurement runDirectMLGemm(const LUID& adapter, double seconds, const AiTick& tick);

// Path B, process-isolated: runs runDirectMLGemm() in a child process of the current executable
// (`--ai-dml-worker`), so a crash inside DirectML or the driver cannot take the application down.
// Falls back to ok=false with a descriptive error if the worker fails, crashes or times out.
AiMeasurement runDirectMLIsolated(const LUID& adapter, double seconds, const AiTick& tick);
// Entry point of the worker process. Returns the process exit code. `argv` excludes the program name.
bool isAiDmlWorkerCommand(int argc, wchar_t** argv);
int aiDmlWorkerMain(int argc, wchar_t** argv);

// Manufacturer peak dense FP16 matrix throughput (TFLOPS) for well-known GPUs; <= 0 when unknown.
double theoreticalAiTops(uint32_t vendorId, const std::string& gpuName);

} // namespace aether
