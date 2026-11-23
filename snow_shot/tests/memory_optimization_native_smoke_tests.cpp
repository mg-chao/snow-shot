#include "snow_shot/runtime/memoryoptimizationcontroller.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <qt_windows.h>
#include <psapi.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
namespace runtime = snow_shot::runtime;
constexpr std::size_t kSampleStride = 1024U * 1024U;
constexpr std::size_t kAllocationBytes = 64U * kSampleStride;
constexpr std::uint64_t kChecksumSeed = 1469598103934665603ULL;
constexpr std::uint64_t kChecksumPrime = 1099511628211ULL;

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

QJsonObject memoryJson(const runtime::ProcessMemorySample& sample) {
    return {{QStringLiteral("private_working_set_bytes"), static_cast<qint64>(sample.memoryBytes)},
            {QStringLiteral("page_fault_count"), static_cast<qint64>(sample.pageFaultCount)}};
}

std::size_t residentSampleCount(void* allocation) {
    std::array<PSAPI_WORKING_SET_EX_INFORMATION, kAllocationBytes / kSampleStride> pages{};
    auto* address = static_cast<unsigned char*>(allocation);
    for (auto& page : pages) {
        page.VirtualAddress = address;
        address += kSampleStride;
    }
    require(QueryWorkingSetEx(GetCurrentProcess(), pages.data(), sizeof(pages)) != FALSE,
            "query residency of pages from the live allocation");
    return static_cast<std::size_t>(std::ranges::count_if(
        pages, [](const auto& page) { return page.VirtualAttributes.Valid != 0; }));
}

int runChild() {
    auto options = runtime::nativeMemoryOptimizationOptions();
    require(options.sample && options.trim && options.lowMemory,
            "the Windows production memory callbacks must be available");
    auto* data = static_cast<std::uint64_t*>(
        VirtualAlloc(nullptr, kAllocationBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    require(data != nullptr, "allocate the isolated child's 64 MiB memory fixture");
    struct AllocationGuard {
        void* address;
        ~AllocationGuard() {
            VirtualFree(address, 0, MEM_RELEASE);
        }
    } guard{data};
    const std::size_t count = kAllocationBytes / sizeof(*data);
    std::uint64_t expectedChecksum = kChecksumSeed;
    for (std::size_t index = 0; index < count; ++index) {
        const auto value = static_cast<std::uint64_t>(index) * 0x9e3779b97f4a7c15ULL + 17U;
        data[index] = value;
        expectedChecksum = (expectedChecksum ^ value) * kChecksumPrime;
    }
    const auto before = options.sample();
    require(before.has_value(), "sample the child's touched allocation");
    const auto pressureBefore = options.lowMemory();
    const auto residentBefore = residentSampleCount(data);
    const auto started = std::chrono::steady_clock::now();
    const auto trimmed = options.trim();
    const double trimMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    // Query residency before checksum reads can bring any evicted pages back in.
    const auto after = options.sample();
    require(trimmed.succeeded && trimmed.error == 0 && after.has_value(),
            "optimize the heap and sample the isolated child");
    const auto residentAfter = residentSampleCount(data);
    // The OS can evict pages independently under memory pressure. Allow some
    // sampling drift while checking for a forced working-set drop when memory is available.
    if (pressureBefore == false && options.lowMemory() == false) {
        require(residentBefore > 0 && residentAfter >= (residentBefore + 1) / 2,
                "heap optimization must preserve live non-heap residency when memory is available");
    }
    const volatile std::uint64_t* retained = data;
    std::uint64_t checksum = kChecksumSeed;
    for (std::size_t index = 0; index < count; ++index)
        checksum = (checksum ^ retained[index]) * kChecksumPrime;
    const auto afterChecksum = options.sample();
    require(checksum == expectedChecksum && afterChecksum.has_value(),
            "verify each byte of the live allocation after heap optimization");
    const QJsonObject report{
        {QStringLiteral("child_process_id"), static_cast<qint64>(GetCurrentProcessId())},
        {QStringLiteral("allocation_bytes"), static_cast<qint64>(kAllocationBytes)},
        {QStringLiteral("before"), memoryJson(*before)},
        {QStringLiteral("after_trim_before_checksum"), memoryJson(*after)},
        {QStringLiteral("after_checksum"), memoryJson(*afterChecksum)},
        {QStringLiteral("resident_samples_before"), static_cast<qint64>(residentBefore)},
        {QStringLiteral("resident_samples_after"), static_cast<qint64>(residentAfter)},
        {QStringLiteral("trim_ms"), trimMs},
        {QStringLiteral("checksum_ok"), true},
        {QStringLiteral("checksum"), QString::number(checksum, 16)},
    };
    std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).constData() << '\n';
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        if (application.arguments().contains(QStringLiteral("--child")))
            return runChild();
        QProcess child;
        child.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child")});
        require(child.waitForStarted(10000), "start the dedicated native memory smoke child");
        if (!child.waitForFinished(30000)) {
            child.kill();
            child.waitForFinished(5000);
            throw std::runtime_error("native memory smoke child timed out");
        }
        require(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                "native memory smoke child failed");
        const QByteArray report = child.readAllStandardOutput();
        require(QJsonDocument::fromJson(report).isObject(), "native smoke child must return JSON");
        std::cout << report.constData();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
