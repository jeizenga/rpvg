
#include "catch.hpp"

#include "sparsepp/spp.h"

#include "../path_clusters.hpp"
#include "../utils.hpp"


TEST_CASE("Connected paths can be clustered") {

    REQUIRE(false);

	// spp::sparse_hash_map<uint32_t, spp::sparse_hash_set<uint32_t> > connected_paths;
	
	// connected_paths[1].emplace(2);
	// connected_paths[1].emplace(5);
	// connected_paths[2].emplace(1);
	// connected_paths[5].emplace(1);

	// connected_paths[6].emplace(3);
	// connected_paths[3].emplace(6);

	// PathClusters path_clusters(connected_paths, 7);

 //    REQUIRE(path_clusters.path_to_cluster_index.size() == 7);
 //    REQUIRE(path_clusters.path_to_cluster_index == vector<uint32_t>({0, 1, 1, 2, 3, 1, 2}));

 //    REQUIRE(path_clusters.cluster_to_paths_index.size() == 4);
 //    REQUIRE(path_clusters.cluster_to_paths_index.at(0) == vector<uint32_t>({0}));
 //    REQUIRE(path_clusters.cluster_to_paths_index.at(1) == vector<uint32_t>({1, 2, 5}));
 //    REQUIRE(path_clusters.cluster_to_paths_index.at(2) == vector<uint32_t>({3, 6}));
 //    REQUIRE(path_clusters.cluster_to_paths_index.at(3) == vector<uint32_t>({4}));
}

