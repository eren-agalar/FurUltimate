#include "AiBench.h"
#include "DmlCompat.h"
#include "FrameStats.h"
#include "../gpu/GpuDevice.h"
#include "../gpu/ShaderLibrary.h"
#include "../core/Log.h"
#include "../core/Strings.h"
#include "../core/Paths.h"
#include "../core/FileIO.h"
#include <nlohmann/json.hpp>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace aether {

using Microsoft::WRL::ComPtr;

namespace {

double nowSec() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER n; QueryPerformanceCounter(&n);
    return double(n.QuadPart) / double(f.QuadPart);
}

// Deterministic pseudo-random values in [-1, 1].
float prand(uint32_t& s) { s = s * 1664525u + 1013904223u; return ((s >> 9) & 0xFFFF) / 32767.5f - 1.0f; }

uint16_t floatToHalf(float f) {
    uint32_t x; std::memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000, mant = x & 0x7FFFFF;
    int exp = (int)((x >> 23) & 0xFF) - 127 + 15;
    if (exp <= 0) return (uint16_t)sign;
    if (exp >= 31) return (uint16_t)(sign | 0x7C00);
    return (uint16_t)(sign | (exp << 10) | (mant >> 13));
}

class D3D11Timer {
public:
    bool init(ID3D11Device* d) {
        D3D11_QUERY_DESC dj{D3D11_QUERY_TIMESTAMP_DISJOINT, 0}, ts{D3D11_QUERY_TIMESTAMP, 0}, ev{D3D11_QUERY_EVENT, 0};
        return SUCCEEDED(d->CreateQuery(&dj, m_dj.GetAddressOf())) && SUCCEEDED(d->CreateQuery(&ts, m_b.GetAddressOf())) &&
               SUCCEEDED(d->CreateQuery(&ts, m_e.GetAddressOf())) && SUCCEEDED(d->CreateQuery(&ev, m_ev.GetAddressOf()));
    }
    // Waits until all previously submitted GPU work (e.g. the overlay frame) has finished, so the timed batch
    // starts on an idle queue and the wall-clock plausibility check measures only this batch.
    void drain(ID3D11DeviceContext* c) {
        c->End(m_ev.Get());
        c->Flush();
        BOOL done = FALSE;
        DWORD t0 = GetTickCount();
        while (c->GetData(m_ev.Get(), &done, sizeof(done), 0) == S_FALSE && GetTickCount() - t0 < 5000) Sleep(0);
    }
    template <class F> double measure(ID3D11DeviceContext* c, F&& work) {
        if (!m_qpf.QuadPart) QueryPerformanceFrequency(&m_qpf);
        drain(c);
        LARGE_INTEGER w0; QueryPerformanceCounter(&w0);
        c->Begin(m_dj.Get());
        c->End(m_b.Get());
        work();
        c->End(m_e.Get());
        c->End(m_dj.Get());
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        UINT64 b = 0, e = 0;
        DWORD t0 = GetTickCount();
        while (c->GetData(m_dj.Get(), &dj, sizeof(dj), 0) == S_FALSE) { if (GetTickCount() - t0 > 10000) return -1; Sleep(0); }
        while (c->GetData(m_b.Get(), &b, sizeof(b), 0) == S_FALSE) Sleep(0);
        while (c->GetData(m_e.Get(), &e, sizeof(e), 0) == S_FALSE) Sleep(0);
        LARGE_INTEGER w1; QueryPerformanceCounter(&w1);
        double wallMs = double(w1.QuadPart - w0.QuadPart) * 1000.0 / double(m_qpf.QuadPart);
        if (dj.Disjoint || dj.Frequency == 0 || e < b) return -1;
        double gpuMs = double(e - b) * 1000.0 / double(dj.Frequency);
        // Plausibility guard: the queries resolve only after the work finished, so the GPU interval can never be
        // much shorter than the wall-clock interval around it. Broken timestamp implementations (seen on software
        // rasterizers) report impossible durations; fall back to wall-clock time instead of reporting inflated results.
        if (wallMs > 2.0 && gpuMs < wallMs * 0.5) {
            if (!m_warned && !m_quiet) { LOGW("GPU timestamps inconsistent (%.3f ms GPU vs %.3f ms wall); using wall-clock timing", gpuMs, wallMs); m_warned = true; }
            return wallMs;
        }
        return gpuMs;
    }
public:
    void setQuiet(bool q) { m_quiet = q; }   // warm-up / calibration: first-use driver work is expected
private:
    bool m_quiet = false;
    LARGE_INTEGER m_qpf{};
    bool m_warned = false;
    ComPtr<ID3D11Query> m_dj, m_b, m_e, m_ev;
};

} // namespace

// ============================================================================ Path A: shader GEMM
AiMeasurement runShaderGemm(ID3D11Device* dev, ID3D11DeviceContext* ctx, double seconds, const AiTick& tick) {
    AiMeasurement m;
    m.precision = "FP32";
    const uint32_t size = 2048;
    std::string err;
    ComPtr<ID3D11ComputeShader> cs = ShaderLibrary::instance().cs(dev, ShaderSource::AiGemm, "CSGemm", err);
    if (!cs) {   // toolchains without group-shared barriers: same work through the texture cache
        LOGW("Tiled GEMM kernel unavailable (%s); using the cache-based kernel", err.c_str());
        err.clear();
        cs = ShaderLibrary::instance().cs(dev, ShaderSource::AiGemmSimple, "CSGemmSimple", err);
    }
    if (!cs) { m.error = "AI shader: " + err; return m; }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = size / 4; td.Height = size; td.MipLevels = 1; td.ArraySize = 1; td.SampleDesc.Count = 1;
    td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<float> data((size_t)size * size);
    uint32_t seed = 12345;
    for (auto& v : data) v = prand(seed);
    D3D11_SUBRESOURCE_DATA init{data.data(), size * 4, 0};
    ComPtr<ID3D11Texture2D> ta, tb, tc;
    ComPtr<ID3D11ShaderResourceView> sa, sb;
    ComPtr<ID3D11UnorderedAccessView> uc;
    if (FAILED(dev->CreateTexture2D(&td, &init, ta.GetAddressOf())) || FAILED(dev->CreateTexture2D(&td, &init, tb.GetAddressOf()))) {
        m.error = "AI matrisleri ayrılamadı"; return m;
    }
    td.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, tc.GetAddressOf())) || FAILED(dev->CreateShaderResourceView(ta.Get(), nullptr, sa.GetAddressOf())) ||
        FAILED(dev->CreateShaderResourceView(tb.Get(), nullptr, sb.GetAddressOf())) ||
        FAILED(dev->CreateUnorderedAccessView(tc.Get(), nullptr, uc.GetAddressOf()))) {
        m.error = "AI matrisleri ayrılamadı"; return m;
    }
    auto bind = [&] {
        ID3D11ShaderResourceView* srvs[2] = {sa.Get(), sb.Get()};
        ctx->CSSetShader(cs.Get(), nullptr, 0);
        ctx->CSSetShaderResources(0, 2, srvs);
        ctx->CSSetUnorderedAccessViews(0, 1, uc.GetAddressOf(), nullptr);
    };
    D3D11Timer timer;
    if (!timer.init(dev)) { m.error = "GPU zaman damgası desteklenmiyor"; return m; }
    const double flopsPerDispatch = 2.0 * size * size * (double)size;
    auto batch = [&](int n) {
        bind();
        return timer.measure(ctx, [&] { for (int i = 0; i < n; ++i) ctx->Dispatch(size / 64, size / 64, 1); });
    };
    // Warm-up and calibration: about 25 ms of work per timed batch.
    timer.setQuiet(true);
    double ms = batch(1);
    timer.setQuiet(false);
    int count = 1;
    if (ms > 0 && ms < 25.0) count = std::clamp((int)(25.0 / std::max(ms, 0.05)), 1, 64);
    std::vector<double> samples;
    double start = nowSec();
    while (nowSec() - start < seconds) {
        double t = batch(count);
        if (t > 0) samples.push_back(flopsPerDispatch * count / (t / 1000.0) / 1e12);
        if (tick && !tick((nowSec() - start) / seconds, median(samples))) { m.error = "cancelled"; break; }
    }
    ID3D11ShaderResourceView* nulls[2] = {};
    ID3D11UnorderedAccessView* nullu = nullptr;
    ctx->CSSetShaderResources(0, 2, nulls);
    ctx->CSSetUnorderedAccessViews(0, 1, &nullu, nullptr);
    if (samples.empty()) { if (m.error.empty()) m.error = "Ölçüm alınamadı"; return m; }
    m.tops = median(samples);
    m.ok = m.error.empty();
    LOGI("AI shader GEMM %u: %.2f TFLOPS (%zu samples, %d dispatches/batch)", size, m.tops, samples.size(), count);
    return m;
}

// ============================================================================ Path B: DirectML GEMM
namespace {

D3D12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE t) { D3D12_HEAP_PROPERTIES p{}; p.Type = t; p.CreationNodeMask = 1; p.VisibleNodeMask = 1; return p; }
D3D12_RESOURCE_DESC bufferDesc(UINT64 size, D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = std::max<UINT64>(size, 256); d.Height = 1; d.DepthOrArraySize = 1;
    d.MipLevels = 1; d.Format = DXGI_FORMAT_UNKNOWN; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags = flags;
    return d;
}

class DmlRunner {
public:
    std::string error;
    HMODULE d3d12 = nullptr, dml = nullptr;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE event = nullptr;
    UINT64 fenceValue = 0;

    ~DmlRunner() {
        if (queue && fence) waitIdle();
        if (event) CloseHandle(event);
        list.Reset(); alloc.Reset(); queue.Reset(); fence.Reset(); device.Reset();
        // DLLs stay loaded for the process lifetime (DirectML keeps internal references).
    }
    bool fail(const std::string& e) { error = e; LOGW("DirectML path unavailable: %s", e.c_str()); return false; }
    void waitIdle() {
        ++fenceValue;
        queue->Signal(fence.Get(), fenceValue);
        if (fence->GetCompletedValue() < fenceValue) {
            fence->SetEventOnCompletion(fenceValue, event);
            WaitForSingleObject(event, 20000);
        }
    }
    bool submit() {
        if (FAILED(list->Close())) return false;
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        waitIdle();
        if (FAILED(device->GetDeviceRemovedReason())) return false;
        alloc->Reset();
        list->Reset(alloc.Get(), nullptr);
        return true;
    }
    bool init(const LUID& luid) {
        d3d12 = LoadLibraryW(L"d3d12.dll");
        if (!d3d12) return fail("d3d12.dll yok");
        wchar_t sys[MAX_PATH]; GetSystemDirectoryW(sys, MAX_PATH);
        dml = LoadLibraryExW((std::wstring(sys) + L"\\DirectML.dll").c_str(), nullptr, 0);
        if (!dml) return fail("DirectML.dll yok (Windows 10 1903+ gerekir)");
        using CreateDev = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
        auto create = (CreateDev)(void*)GetProcAddress(d3d12, "D3D12CreateDevice");
        ComPtr<IDXGIAdapter1> adapter = GpuDeviceManager::findAdapter(luid);
        if (!create || FAILED(create(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)device.GetAddressOf())))
            return fail("Direct3D 12 cihazı oluşturulamadı");
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)queue.GetAddressOf())) ||
            FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)alloc.GetAddressOf())) ||
            FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)list.GetAddressOf())) ||
            FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)fence.GetAddressOf())))
            return fail("Direct3D 12 komut nesneleri oluşturulamadı");
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return true;
    }
    ComPtr<ID3D12Resource> buffer(UINT64 size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags) {
        ComPtr<ID3D12Resource> r;
        D3D12_HEAP_PROPERTIES hp = heapProps(heap);
        D3D12_RESOURCE_DESC rd = bufferDesc(size, flags);
        if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr, __uuidof(ID3D12Resource), (void**)r.GetAddressOf())))
            return nullptr;
        return r;
    }
};

UINT64 tensorBytes(UINT elements, DML_TENSOR_DATA_TYPE type) {
    UINT64 b = (UINT64)elements * (type == DML_TENSOR_DATA_TYPE_FLOAT16 ? 2 : 4);
    return (b + 3) & ~UINT64(3);
}

} // namespace

AiMeasurement runDirectMLGemm(const LUID& luid, double seconds, const AiTick& tick) {
    AiMeasurement m;
    DmlRunner r;
    if (!r.init(luid)) { m.error = r.error; return m; }
    using CreateDml = HRESULT(WINAPI*)(ID3D12Device*, DML_CREATE_DEVICE_FLAGS, REFIID, void**);
    auto createDml = (CreateDml)(void*)GetProcAddress(r.dml, "DMLCreateDevice");
    ComPtr<IDMLDevice> dml;
    if (!createDml || FAILED(createDml(r.device.Get(), DML_CREATE_DEVICE_FLAG_NONE, __uuidof(IDMLDevice), (void**)dml.GetAddressOf()))) {
        m.error = "DirectML cihazı oluşturulamadı";
        return m;
    }
    DML_FEATURE_QUERY_TENSOR_DATA_TYPE_SUPPORT q{DML_TENSOR_DATA_TYPE_FLOAT16};
    DML_FEATURE_DATA_TENSOR_DATA_TYPE_SUPPORT sup{};
    bool fp16 = SUCCEEDED(dml->CheckFeatureSupport(DML_FEATURE_TENSOR_DATA_TYPE_SUPPORT, sizeof(q), &q, sizeof(sup), &sup)) && sup.IsSupported;
    DML_TENSOR_DATA_TYPE type = fp16 ? DML_TENSOR_DATA_TYPE_FLOAT16 : DML_TENSOR_DATA_TYPE_FLOAT32;
    m.precision = fp16 ? "FP16" : "FP32";

    const UINT n = 4096;
    UINT sizes[4] = {1, 1, n, n};
    UINT64 bytes = tensorBytes(n * n, type);
    DML_BUFFER_TENSOR_DESC bt{};
    bt.DataType = type; bt.Flags = DML_TENSOR_FLAG_NONE; bt.DimensionCount = 4; bt.Sizes = sizes; bt.Strides = nullptr;
    bt.TotalTensorSizeInBytes = bytes;
    DML_TENSOR_DESC td{DML_TENSOR_TYPE_BUFFER, &bt};
    DML_GEMM_OPERATOR_DESC gemm{};
    gemm.ATensor = &td; gemm.BTensor = &td; gemm.CTensor = nullptr; gemm.OutputTensor = &td;
    gemm.TransA = DML_MATRIX_TRANSFORM_NONE; gemm.TransB = DML_MATRIX_TRANSFORM_NONE;
    gemm.Alpha = 1.0f; gemm.Beta = 0.0f; gemm.FusedActivation = nullptr;
    DML_OPERATOR_DESC od{DML_OPERATOR_GEMM, &gemm};
    ComPtr<IDMLOperator> op;
    ComPtr<IDMLCompiledOperator> compiled;
    ComPtr<IDMLOperatorInitializer> initializer;
    ComPtr<IDMLCommandRecorder> recorder;
    if (FAILED(dml->CreateOperator(&od, __uuidof(IDMLOperator), (void**)op.GetAddressOf())) ||
        FAILED(dml->CompileOperator(op.Get(), fp16 ? DML_EXECUTION_FLAG_ALLOW_HALF_PRECISION_COMPUTATION : DML_EXECUTION_FLAG_NONE,
                                    __uuidof(IDMLCompiledOperator), (void**)compiled.GetAddressOf()))) {
        m.error = "DirectML GEMM operatörü derlenemedi";
        return m;
    }
    IDMLCompiledOperator* ops[] = {compiled.Get()};
    if (FAILED(dml->CreateOperatorInitializer(1, ops, __uuidof(IDMLOperatorInitializer), (void**)initializer.GetAddressOf())) ||
        FAILED(dml->CreateCommandRecorder(__uuidof(IDMLCommandRecorder), (void**)recorder.GetAddressOf()))) {
        m.error = "DirectML başlatıcı oluşturulamadı";
        return m;
    }
    DML_BINDING_PROPERTIES ip = dmlBindingProperties(initializer.Get()), ep = dmlBindingProperties(compiled.Get());
    LOGI("DirectML binding properties: init %u desc / %llu tmp, exec %u desc / %llu tmp / %llu persistent", ip.RequiredDescriptorCount,
         (unsigned long long)ip.TemporaryResourceSize, ep.RequiredDescriptorCount, (unsigned long long)ep.TemporaryResourceSize,
         (unsigned long long)ep.PersistentResourceSize);
    UINT descCount = std::max(ip.RequiredDescriptorCount, ep.RequiredDescriptorCount);
    UINT64 tempSize = std::max(ip.TemporaryResourceSize, ep.TemporaryResourceSize);
    UINT64 persistSize = ep.PersistentResourceSize;

    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = std::max(1u, descCount); hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;
    if (FAILED(r.device->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)heap.GetAddressOf()))) { m.error = "Tanımlayıcı yığını oluşturulamadı"; return m; }
    DML_BINDING_TABLE_DESC tdsc{initializer.Get(), heap->GetCPUDescriptorHandleForHeapStart(), heap->GetGPUDescriptorHandleForHeapStart(), hd.NumDescriptors};
    ComPtr<IDMLBindingTable> table;
    if (FAILED(dml->CreateBindingTable(&tdsc, __uuidof(IDMLBindingTable), (void**)table.GetAddressOf()))) { m.error = "Bağlama tablosu oluşturulamadı"; return m; }

    const auto uav = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> a = r.buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, uav);
    ComPtr<ID3D12Resource> b = r.buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, uav);
    ComPtr<ID3D12Resource> out = r.buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, uav);
    ComPtr<ID3D12Resource> upload = r.buffer(bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    ComPtr<ID3D12Resource> temp = tempSize ? r.buffer(tempSize, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, uav) : nullptr;
    ComPtr<ID3D12Resource> persist = persistSize ? r.buffer(persistSize, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, uav) : nullptr;
    if (!a || !b || !out || !upload || (tempSize && !temp) || (persistSize && !persist)) { m.error = "AI tamponları ayrılamadı"; return m; }

    // Fill A and B with deterministic pseudo-random data.
    void* mapped = nullptr;
    D3D12_RANGE none{0, 0};
    if (FAILED(upload->Map(0, &none, &mapped))) { m.error = "Yükleme tamponu eşlenemedi"; return m; }
    uint32_t seed = 777;
    if (fp16) { auto* h = (uint16_t*)mapped; for (UINT i = 0; i < n * n; ++i) h[i] = floatToHalf(prand(seed) * 0.5f); }
    else { auto* f = (float*)mapped; for (UINT i = 0; i < n * n; ++i) f[i] = prand(seed) * 0.5f; }
    upload->Unmap(0, nullptr);
    r.list->CopyBufferRegion(a.Get(), 0, upload.Get(), 0, bytes);
    r.list->CopyBufferRegion(b.Get(), 0, upload.Get(), 0, bytes);
    D3D12_RESOURCE_BARRIER tr[2] = {};
    for (int i = 0; i < 2; ++i) {
        tr[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        tr[i].Transition.pResource = i == 0 ? a.Get() : b.Get();
        tr[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        tr[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        tr[i].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    r.list->ResourceBarrier(2, tr);

    // Initialize the operator (persistent resource, if any).
    ID3D12DescriptorHeap* heaps[] = {heap.Get()};
    DML_BUFFER_BINDING tempB{temp.Get(), 0, tempSize}, persistB{persist.Get(), 0, persistSize};
    DML_BINDING_DESC tempD{DML_BINDING_TYPE_BUFFER, &tempB}, persistD{DML_BINDING_TYPE_BUFFER, &persistB};
    if (ip.TemporaryResourceSize) table->BindTemporaryResource(&tempD);
    if (persistSize) table->BindOutputs(1, &persistD);
    r.list->SetDescriptorHeaps(1, heaps);
    recorder->RecordDispatch(r.list.Get(), initializer.Get(), table.Get());
    if (!r.submit()) { m.error = "DirectML başlatma başarısız"; return m; }

    // Bind the compiled operator.
    tdsc.Dispatchable = compiled.Get();
    if (FAILED(table->Reset(&tdsc))) { m.error = "Bağlama tablosu sıfırlanamadı"; return m; }
    DML_BUFFER_BINDING ab{a.Get(), 0, bytes}, bb{b.Get(), 0, bytes}, ob{out.Get(), 0, bytes};
    DML_BINDING_DESC inputs[3] = {{DML_BINDING_TYPE_BUFFER, &ab}, {DML_BINDING_TYPE_BUFFER, &bb}, {DML_BINDING_TYPE_NONE, nullptr}};
    DML_BINDING_DESC output{DML_BINDING_TYPE_BUFFER, &ob};
    table->BindInputs(3, inputs);
    table->BindOutputs(1, &output);
    if (ep.TemporaryResourceSize) table->BindTemporaryResource(&tempD);
    if (persistSize) table->BindPersistentResource(&persistD);

    // GPU timestamps.
    D3D12_QUERY_HEAP_DESC qh{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2, 0};
    ComPtr<ID3D12QueryHeap> queries;
    ComPtr<ID3D12Resource> readback = r.buffer(16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
    UINT64 freq = 0;
    if (FAILED(r.device->CreateQueryHeap(&qh, __uuidof(ID3D12QueryHeap), (void**)queries.GetAddressOf())) || !readback ||
        FAILED(r.queue->GetTimestampFrequency(&freq)) || freq == 0) {
        m.error = "Direct3D 12 zaman damgaları kullanılamıyor";
        return m;
    }
    D3D12_RESOURCE_BARRIER uavb{};
    uavb.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavb.UAV.pResource = nullptr;
    bool tsWarned = false;
    auto batch = [&](int count, bool warmup) -> double {
        double w0 = nowSec();
        r.list->SetDescriptorHeaps(1, heaps);
        r.list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        for (int i = 0; i < count; ++i) {
            recorder->RecordDispatch(r.list.Get(), compiled.Get(), table.Get());
            r.list->ResourceBarrier(1, &uavb);
        }
        r.list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        r.list->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback.Get(), 0);
        if (!r.submit()) return -2;
        UINT64* ts = nullptr;
        D3D12_RANGE rr{0, 16};
        if (FAILED(readback->Map(0, &rr, (void**)&ts))) return -1;
        double ms = ts[1] > ts[0] ? double(ts[1] - ts[0]) * 1000.0 / double(freq) : -1;
        readback->Unmap(0, &none);
        double wallMs = (nowSec() - w0) * 1000.0;
        // Same plausibility guard as the Direct3D 11 timer. Skipped for the warm-up batch: on first use the
        // driver selects/compiles kernels, so wall time is legitimately much longer than GPU time there.
        if (!warmup && ms > 0 && wallMs > 2.0 && ms < wallMs * 0.5) {
            if (!tsWarned) { LOGW("D3D12 timestamps inconsistent (%.3f vs %.3f ms); using wall-clock timing", ms, wallMs); tsWarned = true; }
            ms = wallMs;
        }
        return ms;
    };
    const double flops = 2.0 * n * (double)n * n;
    double ms = batch(1, true);   // warm-up (also first-use kernel selection inside the driver)
    if (ms == -2) { m.error = "GPU cihazı kayboldu"; return m; }
    ms = batch(1, true);          // calibration

    int count = (ms > 0 && ms < 30.0) ? std::clamp((int)(30.0 / std::max(ms, 0.05)), 1, 40) : 1;
    std::vector<double> samples;
    double start = nowSec();
    while (nowSec() - start < seconds) {
        double t = batch(count, false);
        if (t == -2) { m.error = "GPU cihazı kayboldu"; break; }
        if (t > 0) samples.push_back(flops * count / (t / 1000.0) / 1e12);
        if (tick && !tick((nowSec() - start) / seconds, median(samples))) { m.error = "cancelled"; break; }
    }
    if (samples.empty()) { if (m.error.empty()) m.error = "Ölçüm alınamadı"; return m; }
    m.tops = median(samples);
    m.ok = m.error.empty();
    LOGI("AI DirectML GEMM %u %s: %.2f TFLOPS (%zu samples, %d dispatches/batch)", n, m.precision.c_str(), m.tops, samples.size(), count);
    return m;
}

// ============================================================================ process isolation
bool isAiDmlWorkerCommand(int argc, wchar_t** argv) { return argc >= 1 && std::wstring(argv[0]) == L"--ai-dml-worker"; }

// argv: --ai-dml-worker <luidHigh> <luidLow> <seconds> <resultFile> <dataDir>
// A crash inside the worker must end the process immediately with the exception code: no Windows Error
// Reporting dialog and no debugger prompt, so the parent sees the failure at once and continues.
static LONG WINAPI workerCrashFilter(EXCEPTION_POINTERS* ep) {
    DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0xE0000001;
    LOGE("DirectML worker crashed (exception 0x%08lX)", (unsigned long)code);
    Log::shutdown();
    TerminateProcess(GetCurrentProcess(), code);
    return EXCEPTION_EXECUTE_HANDLER;
}

int aiDmlWorkerMain(int argc, wchar_t** argv) {
    if (argc < 5) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(workerCrashFilter);
    LUID luid{};
    luid.HighPart = (LONG)wcstol(argv[1], nullptr, 10);
    luid.LowPart = (DWORD)wcstoul(argv[2], nullptr, 10);
    double seconds = _wtof(argv[3]);
    std::wstring out = argv[4];
    LOGI("DirectML worker started (%.1f s)", seconds);
    char sim[8] = {};
    if (GetEnvironmentVariableA("FNM_SIMULATE_DML_CRASH", sim, sizeof(sim)) && sim[0] == '1') {   // test hook
        LOGW("Simulated DirectML worker crash (fault injection)");
        *(volatile int*)nullptr = 0;
    }
    // Publish the running median so the main window can show live TOPS while the worker measures.
    std::wstring progress = out + L".progress";
    double lastWrite = -1;
    auto publish = [&](double frac, double tops) {
        if (nowSec() - lastWrite >= 0.25) {
            lastWrite = nowSec();
            writeFileAtomic(progress, format("%.4f %.4f", frac, tops));
        }
        return true;
    };
    AiMeasurement m;
    char simRes[8] = {};
    if (GetEnvironmentVariableA("FNM_SIMULATE_DML_RESULT", simRes, sizeof(simRes)) && simRes[0] == '1') {
        // Test hook: publish a synthetic running median (exercises the live-progress channel without DirectML).
        for (double t0 = nowSec(); nowSec() - t0 < 1.5;) { publish((nowSec() - t0) / 1.5, 42.0); Sleep(20); }
        m.ok = true; m.tops = 42.0; m.precision = "FP16";
    } else {
        m = runDirectMLGemm(luid, seconds, publish);
    }
    nlohmann::json j = {{"ok", m.ok}, {"tops", m.tops}, {"precision", m.precision}, {"error", m.error}};
    writeFileAtomic(out, j.dump());
    LOGI("DirectML worker finished: ok=%d %.2f TOPS %s", (int)m.ok, m.tops, m.error.c_str());
    return 0;
}

AiMeasurement runDirectMLIsolated(const LUID& luid, double seconds, const AiTick& tick) {
    AiMeasurement m;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring out = Paths::root() + L"\\dml_result.json";
    DeleteFileW(out.c_str());
    DeleteFileW((out + L".progress").c_str());
    std::wstring cmd = std::wstring(L"\"") + exe + L"\" --ai-dml-worker " + std::to_wstring(luid.HighPart) + L" " +
                       std::to_wstring(luid.LowPart) + L" " + std::to_wstring(seconds) + L" \"" + out + L"\" \"" + Paths::root() + L"\"";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        m.error = format("DirectML çalışanı başlatılamadı (hata %lu)", GetLastError());
        return m;
    }
    double start = nowSec(), limit = seconds + 45.0;   // operator compilation can take a while on first use
    bool cancelled = false, timedOut = false;
    std::wstring progress = out + L".progress";
    double liveTops = 0, lastRead = -1;
    while (WaitForSingleObject(pi.hProcess, 50) == WAIT_TIMEOUT) {
        double el = nowSec() - start;
        if (el - lastRead >= 0.2) {   // live median published by the worker
            lastRead = el;
            std::string p;
            double f = 0, v = 0;
            if (readFile(progress, p) && sscanf(p.c_str(), "%lf %lf", &f, &v) == 2 && v > 0) liveTops = v;
        }
        if (tick && !tick(std::min(0.99, el / (seconds + 3.0)), liveTops)) { cancelled = true; break; }
        if (el > limit) { timedOut = true; break; }
    }
    if (cancelled || timedOut) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (cancelled) { m.error = "cancelled"; return m; }
    if (timedOut) { m.error = "DirectML ölçümü zaman aşımına uğradı"; LOGW("DirectML worker timed out"); return m; }
    std::string text;
    if (code != 0 || !readFile(out, text)) {
        m.error = format("DirectML çalışanı beklenmedik şekilde sonlandı (kod 0x%08lX)", (unsigned long)code);
        LOGE("DirectML worker failed with exit code 0x%08lX — continuing with the shader result", (unsigned long)code);
        return m;
    }
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { m.error = "DirectML sonucu okunamadı"; return m; }
    m.ok = j.value("ok", false);
    m.tops = j.value("tops", 0.0);
    m.precision = j.value("precision", "");
    m.error = j.value("error", "");
    DeleteFileW(out.c_str());
    DeleteFileW((out + L".progress").c_str());
    return m;
}

// ============================================================================ theoretical table
double theoreticalAiTops(uint32_t vendorId, const std::string& nameIn) {
    // Peak dense FP16 tensor/matrix throughput published by the manufacturers (FP16 accumulate, no sparsity).
    struct Row { const char* key; double tops; };
    static const Row nvidia[] = {
        {"RTX 4090", 330.3}, {"RTX 4080 SUPER", 208.9}, {"RTX 4080", 194.9}, {"RTX 4070 TI SUPER", 176.4}, {"RTX 4070 TI", 160.4},
        {"RTX 4070 SUPER", 141.9}, {"RTX 4070", 116.8}, {"RTX 4060 TI", 88.4}, {"RTX 4060", 60.5},
        {"RTX 3090 TI", 160.0}, {"RTX 3090", 142.3}, {"RTX 3080 TI", 136.4}, {"RTX 3080", 119.1}, {"RTX 3070 TI", 87.0},
        {"RTX 3070", 81.3}, {"RTX 3060 TI", 64.8}, {"RTX 3060", 51.2}, {"RTX 3050", 36.4},
        {"RTX 2080 TI", 107.6}, {"RTX 2080 SUPER", 89.2}, {"RTX 2080", 80.5}, {"RTX 2070 SUPER", 72.5}, {"RTX 2070", 59.7},
        {"RTX 2060 SUPER", 57.4}, {"RTX 2060", 51.6},
    };
    static const Row amd[] = {
        {"RX 7900 XTX", 122.8}, {"RX 7900 XT", 103.0}, {"RX 7900 GRE", 91.9}, {"RX 7800 XT", 74.6}, {"RX 7700 XT", 70.3},
        {"RX 7600 XT", 45.1}, {"RX 7600", 43.5},
    };
    std::string n = nameIn;
    for (auto& c : n) c = (char)toupper((unsigned char)c);
    auto find = [&](const Row* rows, size_t count) -> double {
        for (size_t i = 0; i < count; ++i) {   // rows are ordered so that longer / more specific names match first
            size_t p = n.find(rows[i].key);
            if (p == std::string::npos) continue;
            size_t end = p + strlen(rows[i].key);
            if (end < n.size() && isalnum((unsigned char)n[end])) continue;   // "RTX 3080" must not match "RTX 30800"
            return rows[i].tops;
        }
        return 0;
    };
    if (vendorId == 0x10DE) return find(nvidia, sizeof(nvidia) / sizeof(nvidia[0]));
    if (vendorId == 0x1002) return find(amd, sizeof(amd) / sizeof(amd[0]));
    return 0;
}

} // namespace aether
