#ifndef _PROBLEM_H
#define _PROBLEM_H

#include <queue>
#include <functional>
#include <limits>
#include <utility>
#include <numeric>
#include <algorithm>

constexpr long long INF = std::numeric_limits<long long>::max() / 4;

//----------------- DEFINITION OF PROBLEM SPECIFIC TYPES -----------------------

struct Edge {
  int to;
  int cost;
};

struct TProblemData
{
    int n; // size of rk vector = p
    int p; // number of facilities
    int nVertices; // vertices
    int nEdges; // edges
    int alpha; // number of neighbors
    std::vector<std::vector<Edge>> aList; // adjacency list
    std::vector<std::vector<long long>> distances; // matrix of smallest distances between vertices
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
    arq = fopen(name,"r");

    if (arq == NULL)
    {
        printf("\nERROR: File (%s) not found!\n",name);
        getchar();
        exit(1);
    }

    // vertices, edges and p
    fscanf(arq, "%d", &data.nVertices);
    fscanf(arq, "%d", &data.nEdges);
    fscanf(arq, "%d", &data.p);

    if (data.nVertices <= 0 || data.nEdges <= 0 || data.p <= 0 || data.p > data.nVertices) {
      printf("\nERROR: Invalid values of vertices, edges or p.\n");
      fclose(arq);
      exit(1);
    }

    data.alpha = data.p / 2;
    data.n = data.p;
    
    data.aList.clear();
    data.aList.resize(data.nVertices);

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
      
      data.aList[o].push_back({d, cost});
      data.aList[d].push_back({o, cost});

    }

    fclose(arq);

    data.distances.clear();
    data.distances.resize(data.nVertices, std::vector<long long>(data.nVertices, INF));

    for (int i = 0; i < data.nVertices; i++) {
      Dijkstra(i, data.aList, data.distances[i]);
    }
}

/************************************************************************************
 Method: Decoder 
 Description: mapping the random-key solution into a problem solution
*************************************************************************************/
double Decoder(TSol &s, const TProblemData &data)
{
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

  for (int vertex = 0; vertex < data.nVertices; vertex++) {
    std::vector<long long> facilityDistances;
    facilityDistances.reserve(data.p);

    for (int facility: facilities) {
      long long distance = data.distances[vertex][facility];
      facilityDistances.push_back(distance);
    }
    
    std::sort(facilityDistances.begin(), facilityDistances.end());

    for (int i = 0; i < data.alpha; i++) {
      totalCost += facilityDistances[i];
    }
  }

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
