#ifndef POLLEN_NVML_HPP
#define POLLEN_NVML_HPP

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pollen {

struct NvmlSample {
    std::string name;
    std::optional<double> percent;
    std::optional<double> temp;
    std::optional<double> mhz;
    std::optional<double> total;
    std::optional<double> used;
    std::vector<std::pair<long, double>> processes;
};

class Nvml {
public:
    Nvml();
    ~Nvml();

    Nvml(const Nvml&) = delete;
    Nvml& operator=(const Nvml&) = delete;

    bool available();
    std::optional<NvmlSample> sample();

    struct Impl;

private:
    std::unique_ptr<Impl> m;
};

} // namespace pollen

#endif // POLLEN_NVML_HPP
