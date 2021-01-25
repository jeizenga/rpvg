
#ifndef RPVG_SRC_ALIGNMENTPATHFINDER_HPP
#define RPVG_SRC_ALIGNMENTPATHFINDER_HPP

#include <vector>
#include <map>

#include "vg/io/basic_stream.hpp"
#include "paths_index.hpp"
#include "alignment_path.hpp"

using namespace std;


template<class AlignmentType> 
class AlignmentPathFinder {

    public: 
    
       	AlignmentPathFinder(const PathsIndex & paths_index_in, const string library_type_in, const uint32_t max_pair_frag_length_in, const uint32_t max_partial_penalty_in, const uint32_t min_mapq_filter_in, const double min_best_score_filter_in, const double max_softclip_filter_in);

		vector<AlignmentPath> findAlignmentPaths(const AlignmentType & alignment) const;
		vector<AlignmentPath> findPairedAlignmentPaths(const AlignmentType & alignment_1, const AlignmentType & alignment_2) const;

	private:

       	const PathsIndex & paths_index;
       	const string library_type;

       	const uint32_t max_pair_frag_length;
       	const uint32_t max_partial_penalty;

       	const uint32_t min_mapq_filter;
       	const double min_best_score_filter;
       	const double max_softclip_filter;

		bool alignmentHasPath(const vg::Alignment & alignment) const;
		bool alignmentHasPath(const vg::MultipathAlignment & alignment) const;
		
       	bool alignmentStartInGraph(const AlignmentType & alignment) const;

       	int32_t alignmentScore(const char & quality) const;
		int32_t alignmentScore(const string & quality, const uint32_t & start_offset, const uint32_t & length) const;

		uint32_t maxInternalStartOffset(const string & quality, const uint32_t seq_length, const uint32_t init_offset) const;
		uint32_t maxInternalEndOffset(const string & quality, const uint32_t seq_length, const uint32_t init_offset) const;

       	int32_t optimalAlignmentScore(const string & quality, const uint32_t seq_length) const;
		int32_t optimalAlignmentScore(const vg::Alignment & alignment) const;
		int32_t optimalAlignmentScore(const vg::MultipathAlignment & alignment) const;

		vector<AlignmentSearchPath> extendAlignmentPath(const AlignmentSearchPath & align_search_path, const vg::Alignment & alignment) const;
		vector<AlignmentSearchPath> extendAlignmentPath(const AlignmentSearchPath & align_search_path, const vg::Alignment & alignment, const uint32_t subpath_idx) const;

		void extendAlignmentPath(vector<AlignmentSearchPath> * align_search_paths, const vg::Path & path, const bool is_first_path, const bool is_last_path, spp::sparse_hash_set<gbwt::node_type> * internal_node_starts, const string & quality, const uint32_t seq_length) const;
		void extendAlignmentPath(AlignmentSearchPath * align_search_path, const vg::Mapping & mapping) const;

		vector<AlignmentSearchPath> extendAlignmentPath(const AlignmentSearchPath & align_search_path, const vg::MultipathAlignment & alignment) const;
		vector<AlignmentSearchPath> extendAlignmentPath(const AlignmentSearchPath & align_search_path, const vg::MultipathAlignment & alignment, const uint32_t subpath_idx, spp::sparse_hash_set<gbwt::node_type> * internal_node_starts) const;
		void extendAlignmentPaths(vector<AlignmentSearchPath> * align_search_paths, const google::protobuf::RepeatedPtrField<vg::Subpath> & subpaths, const uint32_t subpath_idx, spp::sparse_hash_set<gbwt::node_type> * internal_node_starts, const string & quality, const uint32_t seq_length) const;
		
		void mergeAlignmentPaths(AlignmentSearchPath * main_align_search_path, uint32_t main_path_start_idx, const AlignmentSearchPath & second_align_search_path) const;
		void pairAlignmentPaths(vector<AlignmentSearchPath> * paired_align_search_paths, const AlignmentType & start_alignment, const AlignmentType & end_alignment) const;

		vector<gbwt::node_type> getAlignmentStartNodes(const vg::Alignment & alignment) const;
		vector<gbwt::node_type> getAlignmentStartNodes(const vg::MultipathAlignment & alignment) const;

		uint32_t getMaxAlignmentStartSoftClip(const vg::Alignment & alignment) const;
		uint32_t getMaxAlignmentStartSoftClip(const vg::MultipathAlignment & alignment) const;

		uint32_t getMaxAlignmentEndSoftClip(const vg::Alignment & alignment) const;
		uint32_t getMaxAlignmentEndSoftClip(const vg::MultipathAlignment & alignment) const;

		bool isAlignmentDisconnected(const vg::Alignment & alignment) const;
		bool isAlignmentDisconnected(const vg::MultipathAlignment & alignment) const;

		bool filterAlignmentSearchPaths(const vector<AlignmentSearchPath> & align_search_paths, const vector<int32_t> & optimal_align_scores) const;
};


#endif
