#pragma once
#include "datumloop/numerics/AnalysisMesh.hpp"
namespace datumloop {
struct GmshWorkerSettings {
    std::string pythonExecutable;
    std::string workerScript;
    std::uint32_t timeoutMilliseconds = 120000;
    std::uint32_t maximumMemoryMiB = 2048;
};
class GmshProcessMesher final : public IAnalysisMesher {
public:
    explicit GmshProcessMesher(GmshWorkerSettings settings);
    [[nodiscard]] AnalysisMesh generate(const DatumDocument& document,
                                        const AnalysisMeshingRequest& request) const override;
private:
    GmshWorkerSettings worker_;
};
} // namespace datumloop
