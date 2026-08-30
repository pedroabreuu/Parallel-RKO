#include <gurobi_c++.h>

#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr long long INF = std::numeric_limits<long long>::max() / 4;

struct Edge {
    int to;
    int cost;
};

struct Instance {
    int vertices;
    int edges;
    int p;
    int alpha;
    std::vector<std::vector<Edge>> adjacency;
    std::vector<std::vector<long long>> distances;
};

Instance readInstance(const std::string& path) {
    std::ifstream input(path);
    Instance instance{};

    if (!(input >> instance.vertices >> instance.edges >> instance.p)) {
        throw std::runtime_error("Could not read instance header: " + path);
    }

    instance.alpha = instance.p / 2;  // Same integer division used by Decoder.
    instance.adjacency.resize(instance.vertices);

    for (int edge = 0; edge < instance.edges; ++edge) {
        int origin;
        int destination;
        int cost;

        if (!(input >> origin >> destination >> cost)) {
            throw std::runtime_error("Could not read every edge from: " + path);
        }

        --origin;
        --destination;
        instance.adjacency[origin].push_back({destination, cost});
        instance.adjacency[destination].push_back({origin, cost});
    }

    return instance;
}

void calculateDistances(Instance& instance) {
    const int n = instance.vertices;
    instance.distances.assign(n, std::vector<long long>(n, INF));

    using QueueItem = std::pair<long long, int>;

    for (int source = 0; source < n; ++source) {
        auto& distance = instance.distances[source];
        std::priority_queue<QueueItem, std::vector<QueueItem>,
                            std::greater<QueueItem>> queue;

        distance[source] = 0;
        queue.push({0, source});

        while (!queue.empty()) {
            const auto [currentDistance, vertex] = queue.top();
            queue.pop();

            if (currentDistance != distance[vertex]) {
                continue;
            }

            for (const Edge& edge : instance.adjacency[vertex]) {
                const long long candidate = currentDistance + edge.cost;
                if (candidate < distance[edge.to]) {
                    distance[edge.to] = candidate;
                    queue.push({candidate, edge.to});
                }
            }
        }

        for (long long value : distance) {
            if (value == INF) {
                throw std::runtime_error("The instance graph is disconnected");
            }
        }
    }
}

std::string statusName(int status) {
    if (status == GRB_OPTIMAL) {
        return "OPTIMAL";
    }
    if (status == GRB_TIME_LIMIT) {
        return "TIME_LIMIT";
    }
    if (status == GRB_INFEASIBLE) {
        return "INFEASIBLE";
    }
    if (status == GRB_INF_OR_UNBD) {
        return "INF_OR_UNBD";
    }
    return std::to_string(status);
}

void solve(const std::string& path, double timeLimit) {
    Instance instance = readInstance(path);
    calculateDistances(instance);

    const int n = instance.vertices;

    GRBEnv environment(true);
    environment.set(GRB_IntParam_OutputFlag, 0);
    environment.start();

    GRBModel model(environment);
    model.set(GRB_DoubleParam_TimeLimit, timeLimit);
    model.set(GRB_DoubleParam_MIPGap, 0.0);

    // y[j] = 1 when a facility is opened at vertex j.
    std::vector<GRBVar> y(n);
    for (int j = 0; j < n; ++j) {
        y[j] = model.addVar(0.0, 1.0, 0.0, GRB_BINARY,
                            "y_" + std::to_string(j + 1));
    }

    // x[i,j] assigns vertex i to facility j. These variables may be
    // continuous without changing the integer optimum: after y is fixed,
    // each assignment row chooses its alpha cheapest available facilities.
    std::vector<GRBVar> x(static_cast<std::size_t>(n) * n);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            const std::size_t index = static_cast<std::size_t>(i) * n + j;
            x[index] = model.addVar(0.0, 1.0, instance.distances[i][j],
                                    GRB_CONTINUOUS,
                                    "x_" + std::to_string(i + 1) + "_" +
                                        std::to_string(j + 1));
        }
    }

    GRBLinExpr openFacilities = 0.0;
    for (const GRBVar& facility : y) {
        openFacilities += facility;
    }
    model.addConstr(openFacilities == instance.p, "open_exactly_p");

    for (int i = 0; i < n; ++i) {
        GRBLinExpr assignedFacilities = 0.0;

        for (int j = 0; j < n; ++j) {
            const std::size_t index = static_cast<std::size_t>(i) * n + j;
            assignedFacilities += x[index];
            model.addConstr(x[index] <= y[j]);
        }

        model.addConstr(assignedFacilities == instance.alpha,
                        "assign_" + std::to_string(i + 1));
    }

    model.set(GRB_IntAttr_ModelSense, GRB_MINIMIZE);
    model.optimize();

    const int status = model.get(GRB_IntAttr_Status);
    const int solutionCount = model.get(GRB_IntAttr_SolCount);
    const double runtime = model.get(GRB_DoubleAttr_Runtime);
    const double lowerBound = model.get(GRB_DoubleAttr_ObjBound);

    std::cout << std::fixed << std::setprecision(6)
              << "instance=" << path << ",n=" << n << ",p=" << instance.p
              << ",alpha=" << instance.alpha
              << ",status=" << statusName(status)
              << ",runtime=" << runtime << ",lower_bound=" << lowerBound;

    if (solutionCount > 0) {
        std::cout << ",objective=" << model.get(GRB_DoubleAttr_ObjVal)
                  << ",gap=" << model.get(GRB_DoubleAttr_MIPGap)
                  << ",facilities=";

        bool first = true;
        for (int j = 0; j < n; ++j) {
            if (y[j].get(GRB_DoubleAttr_X) > 0.5) {
                if (!first) {
                    std::cout << ';';
                }
                std::cout << j + 1;  // Restore the instance's 1-based index.
                first = false;
            }
        }
    } else {
        std::cout << ",objective=NA,gap=NA,facilities=NA";
    }

    std::cout << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0]
                  << " INSTANCE_FILE [TIME_LIMIT_SECONDS]\n";
        return 1;
    }

    try {
        const double timeLimit = argc == 3 ? std::stod(argv[2]) : 60.0;
        solve(argv[1], timeLimit);
    } catch (const GRBException& error) {
        std::cerr << "Gurobi error " << error.getErrorCode() << ": "
                  << error.getMessage() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 3;
    }

    return 0;
}
