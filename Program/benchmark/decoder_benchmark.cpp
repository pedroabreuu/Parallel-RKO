#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "../src/Data.h"
#include "../src/Problem/Problem.h"

namespace {

using Clock = std::chrono::steady_clock;

struct PhaseTimes {
    std::uint64_t candidateList = 0;
    std::uint64_t facilitySelection = 0;
    std::uint64_t distanceCollection = 0;
    std::uint64_t ordering = 0;
    std::uint64_t alphaSum = 0;

    std::uint64_t total() const {
        return candidateList + facilitySelection + distanceCollection + ordering + alphaSum;
    }
};

struct DecodeResult {
    long long objective = 0;
    std::vector<int> facilities;
};

struct Workload {
    std::vector<std::vector<double>> keys;
    std::uint64_t hash = 1469598103934665603ULL;
};

struct SeedFile {
    std::vector<std::uint32_t> uniqueSeeds;
    std::size_t duplicateCount = 0;
};

struct Measurements {
    std::vector<double> originalSamples;
    std::vector<double> optimizedSamples;
    double originalMedian = 0.0;
    double optimizedMedian = 0.0;
    double originalCv = 0.0;
    double optimizedCv = 0.0;
    std::size_t repetitions = 0;
    std::size_t evaluationsPerSample = 0;
    std::uint64_t checksum = 0;
    PhaseTimes originalPhases;
    PhaseTimes optimizedPhases;
};

std::uint64_t elapsedNanoseconds(Clock::time_point start) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
}

template <bool Measure>
DecodeResult decodeOriginal(const std::vector<double>& keys, const TProblemData& data, PhaseTimes* phases = nullptr) {
    Clock::time_point start;
    if constexpr (Measure) start = Clock::now();

    std::vector<int> candidates(data.nVertices);
    std::iota(candidates.begin(), candidates.end(), 0);

    if constexpr (Measure) {
        phases->candidateList += elapsedNanoseconds(start);
        start = Clock::now();
    }

    std::vector<int> facilities;
    facilities.reserve(data.p);
    for (int i = 0; i < data.p; ++i) {
        const std::size_t position = static_cast<std::size_t>(keys[i] * candidates.size());
        facilities.push_back(candidates[position]);
        candidates.erase(candidates.begin() + position);
    }

    if constexpr (Measure) phases->facilitySelection += elapsedNanoseconds(start);

    long long objective = 0;
    for (int vertex = 0; vertex < data.nVertices; ++vertex) {
        if constexpr (Measure) start = Clock::now();

        std::vector<long long> facilityDistances;
        facilityDistances.reserve(data.p);
        for (int facility : facilities) {
            facilityDistances.push_back(data.distances[vertex][facility]);
        }

        if constexpr (Measure) {
            phases->distanceCollection += elapsedNanoseconds(start);
            start = Clock::now();
        }

        std::sort(facilityDistances.begin(), facilityDistances.end());

        if constexpr (Measure) {
            phases->ordering += elapsedNanoseconds(start);
            start = Clock::now();
        }

        for (int i = 0; i < data.alpha; ++i) objective += facilityDistances[i];

        if constexpr (Measure) phases->alphaSum += elapsedNanoseconds(start);
    }

    return {objective, std::move(facilities)};
}

template <bool Measure>
DecodeResult decodeOptimized(const std::vector<double>& keys, const TProblemData& data, PhaseTimes* phases = nullptr) {
    Clock::time_point start;
    if constexpr (Measure) start = Clock::now();

    std::vector<int> candidates(data.nVertices);
    std::iota(candidates.begin(), candidates.end(), 0);

    if constexpr (Measure) {
        phases->candidateList += elapsedNanoseconds(start);
        start = Clock::now();
    }

    std::vector<int> facilities;
    facilities.reserve(data.p);
    for (int i = 0; i < data.p; ++i) {
        const std::size_t position = static_cast<std::size_t>(keys[i] * candidates.size());
        facilities.push_back(candidates[position]);
        candidates.erase(candidates.begin() + position);
    }

    if constexpr (Measure) {
        phases->facilitySelection += elapsedNanoseconds(start);
        start = Clock::now();
    }

    std::vector<long long> facilityDistances(data.p);
    if constexpr (Measure) phases->distanceCollection += elapsedNanoseconds(start);

    long long objective = 0;
    for (int vertex = 0; vertex < data.nVertices; ++vertex) {
        if constexpr (Measure) start = Clock::now();

        for (int i = 0; i < data.p; ++i) {
            facilityDistances[i] = data.distances[vertex][facilities[i]];
        }

        if constexpr (Measure) {
            phases->distanceCollection += elapsedNanoseconds(start);
            start = Clock::now();
        }

        std::nth_element(facilityDistances.begin(), facilityDistances.begin() + data.alpha, facilityDistances.end());

        if constexpr (Measure) {
            phases->ordering += elapsedNanoseconds(start);
            start = Clock::now();
        }

        for (int i = 0; i < data.alpha; ++i) objective += facilityDistances[i];

        if constexpr (Measure) phases->alphaSum += elapsedNanoseconds(start);
    }

    return {objective, std::move(facilities)};
}

std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

SeedFile readSeedFile(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open seed file: " + path);

    SeedFile result;
    std::unordered_set<std::uint32_t> seen;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        line = trim(line);
        if (line.empty()) continue;
        if (line.find_first_not_of("0123456789") != std::string::npos) {
            throw std::runtime_error("Invalid seed at line " +
                                     std::to_string(lineNumber) + ": " + line);
        }

        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(line, &consumed);
        if (consumed != line.size() ||
            parsed > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("Seed out of uint32 range at line " + std::to_string(lineNumber));
        }

        const auto seed = static_cast<std::uint32_t>(parsed);
        if (seen.insert(seed).second) {
            result.uniqueSeeds.push_back(seed);
        } else {
            ++result.duplicateCount;
        }
    }

    if (result.uniqueSeeds.empty()) {
        throw std::runtime_error("Seed file contains no valid seeds: " + path);
    }
    return result;
}

void addToHash(std::uint64_t& hash, std::uint32_t value) {
    constexpr std::uint64_t fnvPrime = 1099511628211ULL;
    hash ^= value;
    hash *= fnvPrime;
}

Workload createWorkload(const std::vector<std::uint32_t>& seeds, std::size_t vectorsPerSeed, int keyCount) {
    if (seeds.size() > std::numeric_limits<std::size_t>::max() / vectorsPerSeed) {
        throw std::runtime_error("Workload size overflow");
    }

    Workload workload;
    workload.keys.reserve(seeds.size() * vectorsPerSeed);
    constexpr double scale = 1.0 / 4294967296.0;

    addToHash(workload.hash, static_cast<std::uint32_t>(seeds.size()));
    addToHash(workload.hash, static_cast<std::uint32_t>(vectorsPerSeed));
    addToHash(workload.hash, static_cast<std::uint32_t>(keyCount));
    for (std::uint32_t seed : seeds) {
        std::mt19937 generator(seed);
        for (std::size_t vectorIndex = 0; vectorIndex < vectorsPerSeed;
             ++vectorIndex) {
            std::vector<double> keys(static_cast<std::size_t>(keyCount));
            for (double& key : keys) {
                const std::uint32_t raw = generator();
                key = static_cast<double>(raw) * scale;
                addToHash(workload.hash, raw);
            }
            workload.keys.push_back(std::move(keys));
        }
    }
    return workload;
}

template <typename Decoder>
std::pair<double, std::uint64_t> timeBatch(
    Decoder decoder, const Workload& workload, const TProblemData& data,
    std::size_t repetitions) {
    std::uint64_t checksum = 1469598103934665603ULL;
    const auto start = Clock::now();
    for (std::size_t repetition = 0; repetition < repetitions; ++repetition) {
        for (const auto& keys : workload.keys) {
            const DecodeResult result = decoder(keys, data);
            checksum ^= static_cast<std::uint64_t>(result.objective);
            checksum *= 1099511628211ULL;
        }
    }
    return {std::chrono::duration<double>(Clock::now() - start).count(), checksum};
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    return values.size() % 2 == 0 ? (values[middle - 1] + values[middle]) / 2.0 : values[middle];
}

double coefficientOfVariation(const std::vector<double>& values) {
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    double squaredDifference = 0.0;
    for (double value : values) {
        const double difference = value - mean;
        squaredDifference += difference * difference;
    }
    return std::sqrt(squaredDifference / (values.size() - 1)) / mean;
}

void printComponents(const std::string& name, const PhaseTimes& phases, std::size_t evaluations) {
    constexpr double scale = 1.0e-9;
    std::cout << name << "_component_evaluations=" << evaluations << '\n'
              << name << "_candidate_list_seconds="
              << phases.candidateList * scale << '\n'
              << name << "_facility_selection_seconds="
              << phases.facilitySelection * scale << '\n'
              << name << "_distance_collection_seconds="
              << phases.distanceCollection * scale << '\n'
              << name << "_ordering_seconds=" << phases.ordering * scale << '\n'
              << name << "_alpha_sum_seconds=" << phases.alphaSum * scale << '\n'
              << name << "_instrumented_component_total_seconds="
              << phases.total() * scale << '\n';
}

void validate(const Workload& workload, const TProblemData& data) {
    for (std::size_t i = 0; i < workload.keys.size(); ++i) {
        const DecodeResult original = decodeOriginal<false>(workload.keys[i], data);
        const DecodeResult optimized = decodeOptimized<false>(workload.keys[i], data);
        TSol productionSolution;
        productionSolution.rk = workload.keys[i];
        const long long production = static_cast<long long>(Decoder(productionSolution, data));

        if (original.facilities != optimized.facilities ||
            original.objective != optimized.objective ||
            optimized.objective != production) {
            throw std::runtime_error("Decoder mismatch at fixed vector " + std::to_string(i));
        }
    }
}

std::string joinSeeds(const std::vector<std::uint32_t>& seeds) {
    std::ostringstream output;
    for (std::size_t i = 0; i < seeds.size(); ++i) {
        if (i != 0) output << ';';
        output << seeds[i];
    }
    return output.str();
}

std::string joinTimes(const std::vector<double>& times) {
    std::ostringstream output;
    output << std::setprecision(17);
    for (std::size_t i = 0; i < times.size(); ++i) {
        if (i != 0) output << ';';
        output << times[i];
    }
    return output.str();
}

std::string hexadecimal(std::uint64_t value) {
    std::ostringstream output;
    output << "0x" << std::hex << value;
    return output.str();
}

std::string csvField(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (char character : value) {
        if (character == '"') escaped.push_back('"');
        escaped.push_back(character);
    }
    escaped.push_back('"');
    return escaped;
}

double seconds(std::uint64_t nanoseconds) {
    return static_cast<double>(nanoseconds) * 1.0e-9;
}

void appendCsv(const std::string& csvPath, const std::string& instancePath,
               const std::string& seedFilePath, const TProblemData& data,
               const SeedFile& seedFile,
               const std::vector<std::uint32_t>& seeds,
               std::size_t vectorsPerSeed, const Workload& workload,
               std::size_t samples, double minimumSeconds,
               const Measurements& measurements) {
    const std::filesystem::path path(csvPath);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    const bool writeHeader = !std::filesystem::exists(path) ||
                             std::filesystem::file_size(path) == 0;
    std::ofstream output(path, std::ios::app);
    if (!output) throw std::runtime_error("Cannot open CSV file: " + csvPath);

    if (writeHeader) {
        output << "instance,vertices,p,alpha,seed_file,unique_seeds_available,"
                  "duplicates_ignored,seeds_used,seed_values,vectors_per_seed,"
                  "fixed_vectors,workload_hash,samples,minimum_seconds_per_version,"
                  "repetitions_per_sample,evaluations_per_sample,checksum,"
                  "original_median_seconds,optimized_median_seconds,speedup,"
                  "original_cv,optimized_cv,stable,original_sample_seconds,"
                  "optimized_sample_seconds,"
                  "original_candidate_list_seconds,"
                  "original_facility_selection_seconds,"
                  "original_distance_collection_seconds,original_ordering_seconds,"
                  "original_alpha_sum_seconds,optimized_candidate_list_seconds,"
                  "optimized_facility_selection_seconds,"
                  "optimized_distance_collection_seconds,optimized_ordering_seconds,"
                  "optimized_alpha_sum_seconds\n";
    }

    const bool stable = measurements.originalCv <= 0.05 &&
                        measurements.optimizedCv <= 0.05;
    output << std::setprecision(17)
           << csvField(instancePath) << ',' << data.nVertices << ',' << data.p << ','
           << data.alpha << ',' << csvField(seedFilePath) << ','
           << seedFile.uniqueSeeds.size() << ',' << seedFile.duplicateCount << ','
           << seeds.size() << ',' << csvField(joinSeeds(seeds)) << ','
           << vectorsPerSeed << ',' << workload.keys.size() << ','
           << csvField(hexadecimal(workload.hash)) << ',' << samples << ','
           << minimumSeconds << ',' << measurements.repetitions << ','
           << measurements.evaluationsPerSample << ','
           << csvField(hexadecimal(measurements.checksum)) << ','
           << measurements.originalMedian << ',' << measurements.optimizedMedian << ','
           << measurements.originalMedian / measurements.optimizedMedian << ','
           << measurements.originalCv << ',' << measurements.optimizedCv << ','
           << (stable ? "YES" : "NO") << ','
           << csvField(joinTimes(measurements.originalSamples)) << ','
           << csvField(joinTimes(measurements.optimizedSamples)) << ','
           << seconds(measurements.originalPhases.candidateList) << ','
           << seconds(measurements.originalPhases.facilitySelection) << ','
           << seconds(measurements.originalPhases.distanceCollection) << ','
           << seconds(measurements.originalPhases.ordering) << ','
           << seconds(measurements.originalPhases.alphaSum) << ','
           << seconds(measurements.optimizedPhases.candidateList) << ','
           << seconds(measurements.optimizedPhases.facilitySelection) << ','
           << seconds(measurements.optimizedPhases.distanceCollection) << ','
           << seconds(measurements.optimizedPhases.ordering) << ','
           << seconds(measurements.optimizedPhases.alphaSum) << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 8) {
        std::cerr
            << "Usage: " << argv[0]
            << " INSTANCE SEED_FILE [SEED_COUNT=30] [VECTORS_PER_SEED=100]"
               " [MIN_SECONDS=10] [SAMPLES=10] [CSV_FILE]\n";
        return 1;
    }

    try {
        std::string instancePath = argv[1];
        const std::string seedFilePath = argv[2];
        const std::size_t seedCount = argc >= 4 ? std::stoull(argv[3]) : 30;
        const std::size_t vectorsPerSeed = argc >= 5 ? std::stoull(argv[4]) : 100;
        const double minimumSeconds = argc >= 6 ? std::stod(argv[5]) : 10.0;
        const std::size_t samples = argc >= 7 ? std::stoull(argv[6]) : 10;
        const std::string csvPath = argc >= 8 ? argv[7] : "";
        if (seedCount == 0 || vectorsPerSeed == 0 || minimumSeconds <= 0.0 ||
            samples < 2) {
            throw std::runtime_error(
                "SEED_COUNT, VECTORS_PER_SEED and MIN_SECONDS must be positive; "
                "SAMPLES >= 2");
        }

        TProblemData data;
        ReadData(instancePath.data(), data);
        const SeedFile seedFile = readSeedFile(seedFilePath);
        if (seedFile.uniqueSeeds.size() < seedCount) {
            throw std::runtime_error(
                "Seed file has only " + std::to_string(seedFile.uniqueSeeds.size()) +
                " unique seeds; requested " + std::to_string(seedCount));
        }
        const std::vector<std::uint32_t> seeds(
            seedFile.uniqueSeeds.begin(), seedFile.uniqueSeeds.begin() + seedCount);
        const Workload workload = createWorkload(seeds, vectorsPerSeed, data.p);
        validate(workload, data);

        const std::size_t warmupCount = std::min<std::size_t>(workload.keys.size(), 100);
        for (std::size_t i = 0; i < warmupCount; ++i) {
            (void)decodeOriginal<false>(workload.keys[i], data);
            (void)decodeOptimized<false>(workload.keys[i], data);
        }

        const double targetSampleSeconds = minimumSeconds / samples;
        std::size_t repetitions = 1;
        while (true) {
            const auto originalCalibration = timeBatch([](const auto& keys, const auto& problem) {
                    return decodeOriginal<false>(keys, problem);
                }, workload, data, repetitions);
            const auto optimizedCalibration = timeBatch([](const auto& keys, const auto& problem) {
                    return decodeOptimized<false>(keys, problem);
                }, workload, data, repetitions);
            if (originalCalibration.first >= targetSampleSeconds &&
                optimizedCalibration.first >= targetSampleSeconds) {
                break;
            }
            if (repetitions > std::numeric_limits<std::size_t>::max() / 2) {
                throw std::runtime_error("Calibration repetition count overflow");
            }
            repetitions *= 2;
        }

        std::vector<double> originalSamples;
        std::vector<double> optimizedSamples;
        std::uint64_t expectedChecksum = 0;
        for (std::size_t sample = 0; sample < samples; ++sample) {
            std::pair<double, std::uint64_t> original;
            std::pair<double, std::uint64_t> optimized;
            auto originalDecoder = [](const auto& keys, const auto& problem) {
                return decodeOriginal<false>(keys, problem);
            };
            auto optimizedDecoder = [](const auto& keys, const auto& problem) {
                return decodeOptimized<false>(keys, problem);
            };

            if (sample % 2 == 0) {
                original = timeBatch(originalDecoder, workload, data, repetitions);
                optimized = timeBatch(optimizedDecoder, workload, data, repetitions);
            } else {
                optimized = timeBatch(optimizedDecoder, workload, data, repetitions);
                original = timeBatch(originalDecoder, workload, data, repetitions);
            }

            if (original.second != optimized.second) {
                throw std::runtime_error("Timed checksums do not match");
            }
            expectedChecksum = original.second;
            originalSamples.push_back(original.first);
            optimizedSamples.push_back(optimized.first);
        }

        Measurements measurements;
        measurements.originalSamples = originalSamples;
        measurements.optimizedSamples = optimizedSamples;
        measurements.repetitions = repetitions;
        measurements.checksum = expectedChecksum;
        measurements.evaluationsPerSample = repetitions * workload.keys.size();
        for (const auto& keys : workload.keys) {
            (void)decodeOriginal<true>(keys, data, &measurements.originalPhases);
            (void)decodeOptimized<true>(keys, data, &measurements.optimizedPhases);
        }

        measurements.originalMedian = median(originalSamples);
        measurements.optimizedMedian = median(optimizedSamples);
        measurements.originalCv = coefficientOfVariation(originalSamples);
        measurements.optimizedCv = coefficientOfVariation(optimizedSamples);
        const bool stable = measurements.originalCv <= 0.05 &&
                            measurements.optimizedCv <= 0.05;

        std::cout << std::fixed << std::setprecision(9)
                  << "correctness=PASS\n"
                  << "instance=" << instancePath << '\n'
                  << "vertices=" << data.nVertices << '\n'
                  << "p=" << data.p << '\n'
                  << "alpha=" << data.alpha << '\n'
                  << "seed_file=" << seedFilePath << '\n'
                  << "unique_seeds_available=" << seedFile.uniqueSeeds.size() << '\n'
                  << "duplicates_ignored=" << seedFile.duplicateCount << '\n'
                  << "seeds_used=" << seeds.size() << '\n'
                  << "seed_values=" << joinSeeds(seeds) << '\n'
                  << "vectors_per_seed=" << vectorsPerSeed << '\n'
                  << "fixed_vectors=" << workload.keys.size() << '\n'
                  << "workload_hash=0x" << std::hex << workload.hash << std::dec
                  << '\n'
                  << "samples=" << samples << '\n'
                  << "minimum_seconds_per_version=" << minimumSeconds << '\n'
                  << "repetitions_per_sample=" << repetitions << '\n'
                  << "evaluations_per_sample="
                  << measurements.evaluationsPerSample << '\n'
                  << "checksum=0x" << std::hex << expectedChecksum << std::dec
                  << '\n'
                  << "original_median_seconds=" << measurements.originalMedian << '\n'
                  << "optimized_median_seconds=" << measurements.optimizedMedian << '\n'
                  << "speedup="
                  << measurements.originalMedian / measurements.optimizedMedian
                  << '\n'
                  << "original_cv=" << measurements.originalCv << '\n'
                  << "optimized_cv=" << measurements.optimizedCv << '\n'
                  << "original_sample_seconds="
                  << joinTimes(measurements.originalSamples) << '\n'
                  << "optimized_sample_seconds="
                  << joinTimes(measurements.optimizedSamples) << '\n'
                  << "stable=" << (stable ? "YES" : "NO") << '\n';

        printComponents("original", measurements.originalPhases,
                        workload.keys.size());
        printComponents("optimized", measurements.optimizedPhases,
                        workload.keys.size());
        if (!csvPath.empty()) {
            appendCsv(csvPath, instancePath, seedFilePath, data, seedFile, seeds,
                      vectorsPerSeed, workload, samples, minimumSeconds,
                      measurements);
            std::cout << "csv_file=" << csvPath << '\n';
        }
        FreeMemoryProblem(data);
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 2;
    }
    return 0;
}
