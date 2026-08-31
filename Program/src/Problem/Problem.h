#ifndef _PROBLEM_H
#define _PROBLEM_H

#include <queue>
#include <functional>
#include <limits>
#include <utility>
#include <numeric>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <omp.h>

constexpr long long INF = std::numeric_limits<long long>::max() / 4;

//----------------- DEFINITION OF PROBLEM SPECIFIC TYPES -----------------------

struct Edge {
  int to;
  int cost;
};

struct InputEdge {
  int origin;
  int destination;
  int cost;
};

#ifdef ENABLE_RUNTIME_METRICS
inline std::atomic<long long> decoderElapsedNanoseconds{0};
inline std::atomic<unsigned long long> decoderCallCount{0};

void ResetDecoderMetrics() {
  decoderElapsedNanoseconds.store(0, std::memory_order_relaxed);
  decoderCallCount.store(0, std::memory_order_relaxed);
}

double GetDecoderElapsedSeconds() {
  return static_cast<double>(
      decoderElapsedNanoseconds.load(std::memory_order_relaxed)) / 1.0e9;
}

unsigned long long GetDecoderCallCount() {
  return decoderCallCount.load(std::memory_order_relaxed);
}
#endif

struct TProblemData
{
    int n; // size of rk vector = p
    int p; // number of facilities
    int nVertices; // vertices
    int nEdges; // edges
    int alpha; // number of neighbors
    std::vector<std::vector<Edge>> aList; // adjacency list
    std::vector<std::vector<long long>> distances; // matrix of smallest distances between vertices
    double fileReadTime = 0.0;
    double adjacencyListTime = 0.0;
    double distanceMatrixTime = 0.0;
};


//-------------------------- FUNCTIONS OF SPECIFIC PROBLEM --------------------------


/************************************************************************************
 Method: ReadData
 Description: read the input data
*************************************************************************************/

void Dijkstra(int origin, const std::vector<std::vector<Edge>>& aList, std::vector<long long>& distances) {
  int vertices = static_cast<int>(aList.size());

  if (origin < 0 || origin >= vertices) {
     printf("\nERROR: invalid origin\n");
     exit(1);
  }
  
  distances.assign(vertices, INF);
  distances[origin] = 0;

  using QueueItem = std::pair<long long, int>;
  std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> minQueue;
  
  minQueue.push({0, origin});

  while(!minQueue.empty()) {
    auto [currentDistance, currentVertex] = minQueue.top();
    minQueue.pop();

    if(currentDistance != distances[currentVertex]) {
      continue;
    }

    for (std::size_t i = 0; i < aList[currentVertex].size(); i++) {
      const Edge& edge = aList[currentVertex][i];

      int neighbor = edge.to;
      int cost = edge.cost;

      long long newDistance = currentDistance + cost;

      if (newDistance < distances[neighbor]) {
        distances[neighbor] = newDistance;
        minQueue.push({newDistance, neighbor});
      }
    }
  }

  for (int i = 0; i < vertices; i++) {
    if (distances[i] == INF) {
      printf("\nERROR: Graph is not connected.\n");
      exit(1);
    }
  }

 }

void ReadData(char name[], TProblemData &data)
{ 
    FILE *arq;

    double startFileRead = omp_get_wtime();
    arq = fopen(name,"r");

    if (arq == NULL)
    {
        printf("\nERROR: File (%s) not found!\n",name);
        getchar();
        exit(1);
    }

    // vertices, edges and p
    if (fscanf(arq, "%d %d %d", &data.nVertices, &data.nEdges, &data.p) != 3) {
      printf("\nERROR: Invalid instance header.\n");
      fclose(arq);
      exit(1);
    }

    if (data.nVertices <= 0 || data.nEdges <= 0 || data.p <= 0 || data.p > data.nVertices) {
      printf("\nERROR: Invalid values of vertices, edges or p.\n");
      fclose(arq);
      exit(1);
    }

    data.alpha = data.p / 2;
    data.n = data.p;

    std::vector<InputEdge> inputEdges;
    inputEdges.reserve(data.nEdges);

    for (int i = 0; i < data.nEdges; i++) {
      int o, d, cost;
      int valuesRead = fscanf(arq, "%d %d %d", &o, &d, &cost);

      if (valuesRead != 3) {
          printf("\nERROR: Invalid edge at line %d.\n", i + 2);
          fclose(arq);
          exit(1);
      }

      o--;
      d--;

      if (o < 0 || o >= data.nVertices || d < 0 || d >= data.nVertices || cost < 0) {
        printf("\nERROR: Invalid edge at line %d.\n", i + 2);
        fclose(arq);
        exit(1);
      }

      inputEdges.push_back({o, d, cost});
    }

    fclose(arq);

    data.fileReadTime = omp_get_wtime() - startFileRead;

    double startAdjacencyList = omp_get_wtime();
    data.aList.clear();
    data.aList.resize(data.nVertices);

    for (const InputEdge& edge : inputEdges) {
      data.aList[edge.origin].push_back({edge.destination, edge.cost});
      data.aList[edge.destination].push_back({edge.origin, edge.cost});
    }

    data.adjacencyListTime = omp_get_wtime() - startAdjacencyList;

    double startDistanceMatrix = omp_get_wtime();
    data.distances.clear();
    data.distances.resize(data.nVertices, std::vector<long long>(data.nVertices, INF));

    for (int i = 0; i < data.nVertices; i++) {
      Dijkstra(i, data.aList, data.distances[i]);
    }

    data.distanceMatrixTime = omp_get_wtime() - startDistanceMatrix;
}

/************************************************************************************
 Method: Decoder 
 Description: mapping the random-key solution into a problem solution
*************************************************************************************/
double Decoder(TSol &s, const TProblemData &data)
{
#ifdef ENABLE_RUNTIME_METRICS
  const auto decoderStart = std::chrono::steady_clock::now();
#endif

  std::vector<int> candidates(data.nVertices);
  std::iota(candidates.begin(), candidates.end(), 0);

  std::vector<int> facilities;
  facilities.reserve(data.p);

  if (s.rk.size() != static_cast<std::size_t>(data.p)) {
    printf("\nERROR: Random-key vector size does not match number of facilities");
    exit(1);
  }

  for (int i = 0; i < data.p; i++) {
    double key = s.rk[i];

    if (key < 0.0 || key >= 1.0) {
      printf("\nERROR: Invalid random key.\n");
      exit(1);
    }

    std::size_t candidatePosition = static_cast<std::size_t>(key * candidates.size());
    facilities.push_back(candidates[candidatePosition]);
    candidates.erase(candidates.begin() + candidatePosition);

  }

  long long totalCost = 0;
  std::vector<long long> facilityDistances(data.p);

  for (int vertex = 0; vertex < data.nVertices; vertex++) {
    for (int i = 0; i < data.p; i++) {
      facilityDistances[i] = data.distances[vertex][facilities[i]];
    }

    std::nth_element(facilityDistances.begin(), facilityDistances.begin() + data.alpha, facilityDistances.end());

    for (int i = 0; i < data.alpha; i++) {
      totalCost += facilityDistances[i];
    }
  }

#ifdef ENABLE_RUNTIME_METRICS
  const auto decoderEnd = std::chrono::steady_clock::now();
  const auto elapsedNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(decoderEnd - decoderStart).count();

  decoderElapsedNanoseconds.fetch_add(elapsedNanoseconds, std::memory_order_relaxed);
  decoderCallCount.fetch_add(1, std::memory_order_relaxed);
#endif

  return static_cast<double>(totalCost);
}


/************************************************************************************
 Method: FreeMemoryProblem
 Description: Free local memory allocate by Problem
*************************************************************************************/
void FreeMemoryProblem(TProblemData &data){
    data.aList.clear();
    data.distances.clear();
}

#endif
