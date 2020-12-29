
#include "alignment_path_finder.hpp"

#include <assert.h>

#include "utils.hpp"

//#define debug


template<class AlignmentType>
AlignmentPathFinder<AlignmentType>::AlignmentPathFinder(const PathsIndex & paths_index_in, const string library_type_in, const uint32_t max_pair_seq_length_in, const double min_mapq_value_in, const double min_rel_score_in) : paths_index(paths_index_in), library_type(library_type_in), max_pair_seq_length(max_pair_seq_length_in), min_mapq_value(min_mapq_value_in), min_rel_score(min_rel_score_in) {}

template<class AlignmentType>
void AlignmentPathFinder<AlignmentType>::setMaxPairSeqLength(const uint32_t max_pair_seq_length_in) {

    max_pair_seq_length = max_pair_seq_length_in;
}
        
template<class AlignmentType>
bool AlignmentPathFinder<AlignmentType>::alignmentHasPath(const vg::Alignment & alignment) const {

    return alignment.has_path();
}

template<class AlignmentType>
bool AlignmentPathFinder<AlignmentType>::alignmentHasPath(const vg::MultipathAlignment & alignment) const {

    return (alignment.subpath_size() > 0);
}

template<class AlignmentType>
bool AlignmentPathFinder<AlignmentType>::alignmentStartInGraph(const AlignmentType & alignment) const {

    auto alignment_start_nodes_index = getAlignmentStartNodesIndex(alignment);

    for (auto & start_node: alignment_start_nodes_index) {

        if (!paths_index.hasNodeId(gbwt::Node::id(start_node.first))) {

            return false;
        } 
    } 

    return true;
}

template<class AlignmentType>
vector<AlignmentPath> AlignmentPathFinder<AlignmentType>::findAlignmentPaths(const AlignmentType & alignment) const {

#ifdef debug

    cerr << endl;
    cerr << pb2json(alignment) << endl;

#endif

    if (!alignmentHasPath(alignment)) {

        return vector<AlignmentPath>();
    }

    if (!alignmentStartInGraph(alignment)) {

        return vector<AlignmentPath>();
    }

    vector<AlignmentSearchPath> align_search_paths;

    function<size_t(const uint32_t)> node_length_func = [&](const uint32_t node_id) { return paths_index.nodeLength(node_id); };

    if (library_type == "fr") {

        align_search_paths = extendAlignmentPath(AlignmentSearchPath(), alignment);

    } else if (library_type == "rf") {

        AlignmentType alignment_rc = lazy_reverse_complement_alignment(alignment, node_length_func);
        align_search_paths = extendAlignmentPath(AlignmentSearchPath(), alignment_rc);

    } else {

        assert(library_type == "unstranded");
        align_search_paths = extendAlignmentPath(AlignmentSearchPath(), alignment);

        if (!paths_index.index().bidirectional()) {

            AlignmentType alignment_rc = lazy_reverse_complement_alignment(alignment, node_length_func);
            auto align_search_paths_rc = extendAlignmentPath(AlignmentSearchPath(), alignment_rc);

            align_search_paths.reserve(align_search_paths.size() + align_search_paths_rc.size());
            align_search_paths.insert(align_search_paths.end(), align_search_paths_rc.begin(), align_search_paths_rc.end());
        }  
    }

    if (filterAlignmentSearchPaths(align_search_paths)) {

        return vector<AlignmentPath>();
    }

    auto align_paths = AlignmentPath::alignmentSearchPathsToAlignmentPaths(align_search_paths, isAlignmentDisconnected(alignment));

#ifdef debug

    cerr << endl;
    cerr << align_search_paths << endl;
    cerr << align_paths << endl;
    cerr << endl;

#endif

    return align_paths;
}

template<class AlignmentType>
vector<AlignmentSearchPath> AlignmentPathFinder<AlignmentType>::extendAlignmentPath(const AlignmentSearchPath & align_search_path, const vg::Alignment & alignment) const {

    return extendAlignmentPath(align_search_path, alignment, 0);
}

template<class AlignmentType>
vector<AlignmentSearchPath> AlignmentPathFinder<AlignmentType>::extendAlignmentPath(const AlignmentSearchPath & align_search_path, const vg::Alignment & alignment, const uint32_t subpath_start_idx) const {

    assert(alignment.mapping_quality() >= 0);

    vector<AlignmentSearchPath> extended_align_search_path(1, align_search_path);
    extended_align_search_path.front().min_mapq = min(extended_align_search_path.front().min_mapq, static_cast<uint32_t>(alignment.mapping_quality()));
    extended_align_search_path.front().scores.emplace_back(alignment.score(), 0);
    
    extendAlignmentPath(&extended_align_search_path.front(), alignment.path());

    if (extended_align_search_path.front().search_state.empty()) {

        return vector<AlignmentSearchPath>();
    
    } else {

        return extended_align_search_path;
    }
}

template<class AlignmentType>
void AlignmentPathFinder<AlignmentType>::extendAlignmentPath(AlignmentSearchPath * align_search_path, const vg::Path & path) const {

    assert(align_search_path->path_end_idx <= align_search_path->path.size());
    
    auto mapping_it = path.mapping().cbegin();
    assert(mapping_it != path.mapping().cend());

    if (!align_search_path->path.empty() && align_search_path->path_end_idx == 0) {

        if (mapping_it->position().offset() < align_search_path->seq_start_offset) {

            align_search_path->search_state = gbwt::SearchState();
            assert(align_search_path->search_state.empty());

            return;  
        }
    }

    while (align_search_path->path_end_idx < align_search_path->path.size() && mapping_it != path.mapping().cend()) {

        auto cur_node = mapping_to_gbwt(*mapping_it);

        bool is_multi_visit = false;
        bool is_cycle_visit = false;

        if (mapping_it != path.mapping().cbegin()) {

            auto prev_mapping_it = mapping_it;
            --prev_mapping_it;

            if (mapping_to_gbwt(*prev_mapping_it) == cur_node) {

                if (mapping_it->position().offset() == align_search_path->seq_end_offset) {

                    is_multi_visit = true;
                                    
                } else {

                    if (mapping_it->position().offset() != 0) {

                        cerr << "\nOverlap" << endl;
                        cerr << *align_search_path << endl;
                        cerr << pb2json(path) << endl;
                        cerr << pb2json(*mapping_it) << endl;
                        cerr << pb2json(*prev_mapping_it) << endl;
                    }

                    // assert(mapping_it->position().offset() == 0);                    
                    is_cycle_visit = true;
                }
            } 
        }

        if (align_search_path->path.at(align_search_path->path_end_idx) == cur_node || is_multi_visit || is_cycle_visit) {

            align_search_path->seq_length -= align_search_path->seq_end_offset;
            align_search_path->seq_end_offset = mapping_it->position().offset() + mapping_from_length(*mapping_it);

            align_search_path->seq_length += mapping_it->position().offset() + mapping_to_length(*mapping_it);
            align_search_path->scores.back().second += mapping_to_length(*mapping_it);

            if (!is_multi_visit) {

                ++align_search_path->path_end_idx;
            }
    
        } else {

            align_search_path->search_state = gbwt::SearchState();
            assert(align_search_path->search_state.empty());  
    
            return;  
        } 

        ++mapping_it;
    }

    while (mapping_it != path.mapping().cend()) {

        auto cur_node = mapping_to_gbwt(*mapping_it);

        if (align_search_path->path.empty()) {

            assert(align_search_path->search_state.node == gbwt::ENDMARKER);
            assert(align_search_path->seq_length == 0);

            align_search_path->seq_start_offset = mapping_it->position().offset();
            align_search_path->search_state = paths_index.index().find(cur_node);
      
            align_search_path->path.emplace_back(cur_node);
            ++align_search_path->path_end_idx;

        } else {

            bool is_cycle_visit = false;

            if (align_search_path->path.back() == cur_node && mapping_it->position().offset() != align_search_path->seq_end_offset) {

                assert(mapping_it->position().offset() == 0);
                is_cycle_visit = true;      
            }

            if (align_search_path->path.back() != cur_node || is_cycle_visit) {

                align_search_path->search_state = paths_index.index().extend(align_search_path->search_state, cur_node);

                align_search_path->path.emplace_back(cur_node);
                ++align_search_path->path_end_idx;
            }
        }

        align_search_path->seq_end_offset = mapping_it->position().offset() + mapping_from_length(*mapping_it);

        align_search_path->seq_length += mapping_to_length(*mapping_it);
        align_search_path->scores.back().second += mapping_to_length(*mapping_it);

        if (align_search_path->search_state.empty()) {

            break;
        }

        ++mapping_it;
    }
}

template<class AlignmentType>
vector<AlignmentSearchPath> AlignmentPathFinder<AlignmentType>::extendAlignmentPath(const AlignmentSearchPath & align_search_path, const vg::MultipathAlignment & alignment) const {

    vector<AlignmentSearchPath> extended_align_search_paths;

    for (auto & start_idx: alignment.start()) {

        auto cur_extended_align_search_paths = extendAlignmentPath(align_search_path, alignment, start_idx);
        extended_align_search_paths.insert(extended_align_search_paths.end(), cur_extended_align_search_paths.begin(), cur_extended_align_search_paths.end());
    }

    return extended_align_search_paths;
}

template<class AlignmentType>
vector<AlignmentSearchPath> AlignmentPathFinder<AlignmentType>::extendAlignmentPath(const AlignmentSearchPath & align_search_path, const vg::MultipathAlignment & alignment, const uint32_t subpath_start_idx) const {

    assert(alignment.mapping_quality() >= 0);

    vector<AlignmentSearchPath> extended_align_search_path(1, align_search_path);
    extended_align_search_path.front().min_mapq = min(extended_align_search_path.front().min_mapq, static_cast<uint32_t>(alignment.mapping_quality()));
    extended_align_search_path.front().scores.emplace_back(0, 0);

    extendAlignmentPaths(&extended_align_search_path, alignment.subpath(), subpath_start_idx);
            
    return extended_align_search_path;
}

template<class AlignmentType>
void AlignmentPathFinder<AlignmentType>::extendAlignmentPaths(vector<AlignmentSearchPath> * align_search_paths, const google::protobuf::RepeatedPtrField<vg::Subpath> & subpaths, const uint32_t subpath_start_idx) const {

    std::queue<pair<AlignmentSearchPath, uint32_t> > align_search_paths_queue;

    for (auto & align_search_path: *align_search_paths) {

        align_search_paths_queue.push(make_pair(align_search_path, subpath_start_idx));
    }

    align_search_paths->clear();

    // Perform depth-first alignment path extension.
    while (!align_search_paths_queue.empty()) {

        auto & cur_align_search_path = align_search_paths_queue.front();

        const vg::Subpath & subpath = subpaths.Get(cur_align_search_path.second);

        cur_align_search_path.first.scores.back().first += subpath.score();
        extendAlignmentPath(&cur_align_search_path.first, subpath.path());

        if (cur_align_search_path.first.path.empty() || !cur_align_search_path.first.search_state.empty()) {

            if (subpath.next_size() > 0 || subpath.connection_size() > 0) {

                for (auto & next_subpath_idx: subpath.next()) {

                    align_search_paths_queue.push(make_pair(cur_align_search_path.first, next_subpath_idx));
                }

                for (auto & connection: subpath.connection()) {

                    assert(connection.score() <= 0);
                    cur_align_search_path.first.scores.back().first += connection.score();

                    align_search_paths_queue.push(make_pair(cur_align_search_path.first, connection.next()));
                }

            } else {

                align_search_paths->emplace_back(cur_align_search_path.first);
            }
        }

        align_search_paths_queue.pop();
    }
}

// Debug start

char quality_short_to_char(short i) {
    return static_cast<char>(i + 33);
}

string string_quality_short_to_char(const string& quality) {
    string buffer; buffer.resize(quality.size());
    for (int i = 0; i < quality.size(); ++i) {
        buffer[i] = quality_short_to_char(quality[i]);
    }
    return buffer;
}

// Debug end

template<class AlignmentType>
vector<AlignmentPath> AlignmentPathFinder<AlignmentType>::findPairedAlignmentPaths(const AlignmentType & alignment_1, const AlignmentType & alignment_2) const {

#ifdef debug

    cerr << endl;
    findAlignmentPaths(alignment_1);
    findAlignmentPaths(alignment_2);

#endif

    if (!alignmentHasPath(alignment_1) || !alignmentHasPath(alignment_2)) {

        return vector<AlignmentPath>();
    }

    if (!alignmentStartInGraph(alignment_1) || !alignmentStartInGraph(alignment_2)) {

        return vector<AlignmentPath>();
    }

    vector<AlignmentSearchPath> paired_align_search_paths;

    function<size_t(const uint32_t)> node_length_func = [&](const uint32_t node_id) { return paths_index.nodeLength(node_id); };
    AlignmentType alignment_2_rc = lazy_reverse_complement_alignment(alignment_2, node_length_func);

    if (library_type == "fr") {

        pairAlignmentPaths(&paired_align_search_paths, alignment_1, alignment_2_rc);

    } else if (library_type == "rf") {

        AlignmentType alignment_1_rc = lazy_reverse_complement_alignment(alignment_1, node_length_func);
        pairAlignmentPaths(&paired_align_search_paths, alignment_2, alignment_1_rc);

    } else {

        assert(library_type == "unstranded");

        AlignmentType alignment_2_rc = lazy_reverse_complement_alignment(alignment_2, node_length_func);
        pairAlignmentPaths(&paired_align_search_paths, alignment_1, alignment_2_rc);

        if (!paths_index.index().bidirectional()) {

            AlignmentType alignment_1_rc = lazy_reverse_complement_alignment(alignment_1, node_length_func);
            pairAlignmentPaths(&paired_align_search_paths, alignment_2, alignment_1_rc);
        }
    }

    if (filterAlignmentSearchPaths(paired_align_search_paths)) {

        return vector<AlignmentPath>();
    }

    auto paired_align_paths = AlignmentPath::alignmentSearchPathsToAlignmentPaths(paired_align_search_paths, isAlignmentDisconnected(alignment_1) || isAlignmentDisconnected(alignment_2));

    // Debug start

    // string debug_paths = "";
    // int32_t debug_idx = -1;

    // string debug_paths2 = "";
    // int32_t debug_idx2 = -1;

    // for (size_t i = 0; i < paired_align_search_paths.size(); ++i) {

    //     if (paired_align_search_paths.at(i).complete()) {

    //         for (auto & path_id: paths_index.locatePathIds(paired_align_search_paths.at(i).search_state)) {

    //             auto path_name = paths_index.pathName(path_id);

    //             if (
    //                 path_name == "ENST00000580018.3_15" || 
    //                 path_name == "ENST00000374259.7" || 
    //                 path_name == "ENST00000325307.11" || 
    //                 path_name == "ENST00000216252.3_19" || 
    //                 path_name == "ENST00000271638.2"
    //             ) {   

    //                 debug_paths = path_name; 
    //                 debug_idx = i;         
                
    //             } else if (
    //                 path_name == "ENST00000580018.3_16" || 
    //                 path_name == "ENST00000374259.7_19" || 
    //                 path_name == "ENST00000325307.11_24" || 
    //                 path_name == "ENST00000216252.3_24" || 
    //                 path_name == "ENST00000271638.2_8"
    //                 ) {  

    //                 debug_paths2 = path_name; 
    //                 debug_idx2 = i;         
    //             }                
    //         }
    //     }
    // }

    // if (debug_idx != debug_idx2) {

    //     #pragma omp critical
    //     {
    //         cerr << "\n\n###" << endl;
    //         cerr << debug_paths << endl;
    //         cerr << debug_idx << endl;

    //         if (debug_idx >= 0) {

    //             cerr << paired_align_search_paths.at(debug_idx) << endl;
    //         }

    //         cerr << debug_paths2 << endl;
    //         cerr << debug_idx2 << endl;

    //         if (debug_idx2 >= 0) {

    //             cerr << paired_align_search_paths.at(debug_idx2) << endl;
    //         }
            
    //         cerr << endl;
    //         cerr << paired_align_search_paths << endl;
    //         cerr << endl;
    //         cerr << pb2json(alignment_1) << endl;
    //         cerr << string_quality_short_to_char(alignment_1.quality()) << endl;
    //         cerr << endl;
    //         cerr << pb2json(alignment_2) << endl;
    //         cerr << string_quality_short_to_char(alignment_2.quality()) << endl;
    //     }
    // }

    // Debug end

#ifdef debug

    cerr << endl;
    cerr << paired_align_search_paths << endl;
    cerr << paired_align_paths << endl;
    cerr << endl;

#endif

    return paired_align_paths;
}

template<class AlignmentType>
void AlignmentPathFinder<AlignmentType>::pairAlignmentPaths(vector<AlignmentSearchPath> * paired_align_search_paths, const AlignmentType & start_alignment, const AlignmentType & end_alignment) const {

    auto start_align_search_paths = extendAlignmentPath(AlignmentSearchPath(), start_alignment);
    auto end_alignment_start_nodes_index = getAlignmentStartNodesIndex(end_alignment);

    std::queue<AlignmentSearchPath> paired_align_search_path_queue;

    for (auto & align_search_path: start_align_search_paths) {

        assert(!align_search_path.search_state.empty());
        assert(!align_search_path.path.empty());

        align_search_path.seq_length += (paths_index.nodeLength(gbwt::Node::id(align_search_path.search_state.node)) - align_search_path.seq_end_offset);
        align_search_path.seq_end_offset = paths_index.nodeLength(gbwt::Node::id(align_search_path.search_state.node));

        paired_align_search_path_queue.push(align_search_path);

        for (auto & start_nodes: end_alignment_start_nodes_index) {

            auto path_it = find(align_search_path.path.begin(), align_search_path.path.end(), start_nodes.first); 
            
            auto align_search_path_end = align_search_path.path.end();
            --align_search_path_end;

            while (path_it != align_search_path.path.end()) {

                if (path_it == align_search_path_end) {

                    break;
                }

                align_search_path.path_end_idx = path_it - align_search_path.path.begin();
                auto complete_paired_align_search_paths = extendAlignmentPath(align_search_path, end_alignment, start_nodes.second);

                for (auto & complete_align_search_path: complete_paired_align_search_paths) {

                    if (!complete_align_search_path.search_state.empty() && complete_align_search_path.seq_length <= max_pair_seq_length) {

                        paired_align_search_paths->emplace_back(complete_align_search_path);                         
                    }
                }

                ++path_it;
                path_it = find(path_it, align_search_path.path.end(), start_nodes.first); 
            }
        }
    }

    // Perform depth-first path extension.
    while (!paired_align_search_path_queue.empty()) {

        AlignmentSearchPath * cur_paired_align_search_path = &(paired_align_search_path_queue.front());
        assert(cur_paired_align_search_path->search_state.node != gbwt::ENDMARKER);

        auto end_alignment_start_nodes_index_itp = end_alignment_start_nodes_index.equal_range(cur_paired_align_search_path->search_state.node);

        if (end_alignment_start_nodes_index_itp.first != end_alignment_start_nodes_index_itp.second) {

            while (end_alignment_start_nodes_index_itp.first != end_alignment_start_nodes_index_itp.second) {

                AlignmentSearchPath cur_paired_align_search_path_end = *cur_paired_align_search_path;

                assert(cur_paired_align_search_path_end.path_end_idx == cur_paired_align_search_path_end.path.size());
                --cur_paired_align_search_path_end.path_end_idx;

                auto complete_paired_align_search_paths = extendAlignmentPath(cur_paired_align_search_path_end, end_alignment, end_alignment_start_nodes_index_itp.first->second);

                for (auto & complete_align_search_path: complete_paired_align_search_paths) {

                    if (!complete_align_search_path.search_state.empty() && complete_align_search_path.seq_length <= max_pair_seq_length) {

                        paired_align_search_paths->emplace_back(complete_align_search_path);                         
                    }
                }

                ++end_alignment_start_nodes_index_itp.first;
            }
        }
           
        if (cur_paired_align_search_path->seq_length + end_alignment.sequence().size() > max_pair_seq_length) {

            paired_align_search_path_queue.pop();
            continue;
        }

        auto out_edges = paths_index.index().edges(cur_paired_align_search_path->search_state.node);

        // End current extension if no outgoing edges exist.
        if (out_edges.empty()) {

            paired_align_search_path_queue.pop();
            continue;
        }

        auto out_edges_it = out_edges.begin(); 
        assert(out_edges_it != out_edges.end());
        
        ++out_edges_it;

        while (out_edges_it != out_edges.end()) {

            if (out_edges_it->first != gbwt::ENDMARKER) {

                auto extended_path = paths_index.index().extend(cur_paired_align_search_path->search_state, out_edges_it->first);

                // Add new extension to queue if not empty (path found).
                if (!extended_path.empty()) { 

                    paired_align_search_path_queue.push(*cur_paired_align_search_path);
                    paired_align_search_path_queue.back().path.emplace_back(extended_path.node);
                    ++paired_align_search_path_queue.back().path_end_idx;
                    paired_align_search_path_queue.back().seq_end_offset = paths_index.nodeLength(gbwt::Node::id(extended_path.node));
                    paired_align_search_path_queue.back().search_state = extended_path;
                    paired_align_search_path_queue.back().seq_length += paired_align_search_path_queue.back().seq_end_offset;
                }
            }

            ++out_edges_it;
        }

        if (out_edges.begin()->first != gbwt::ENDMARKER) {
            
            cur_paired_align_search_path->search_state = paths_index.index().extend(cur_paired_align_search_path->search_state, out_edges.begin()->first);

            // End current extension if empty (no haplotypes found). 
            if (cur_paired_align_search_path->search_state.empty()) { 

                paired_align_search_path_queue.pop(); 

            } else {

                cur_paired_align_search_path->path.emplace_back(cur_paired_align_search_path->search_state.node);
                ++cur_paired_align_search_path->path_end_idx;
                cur_paired_align_search_path->seq_end_offset = paths_index.nodeLength(gbwt::Node::id(cur_paired_align_search_path->search_state.node));
                cur_paired_align_search_path->seq_length += cur_paired_align_search_path->seq_end_offset;
            }
    
        } else {

            paired_align_search_path_queue.pop();
        }
    }
}

template<class AlignmentType>
multimap<gbwt::node_type, uint32_t> AlignmentPathFinder<AlignmentType>::getAlignmentStartNodesIndex(const vg::Alignment & alignment) const {

    multimap<gbwt::node_type, uint32_t> alignment_start_nodes_index;

    assert(alignment.path().mapping_size() > 0);
    alignment_start_nodes_index.emplace(mapping_to_gbwt(alignment.path().mapping(0)), 0);

    return alignment_start_nodes_index;
}

template<class AlignmentType>
multimap<gbwt::node_type, uint32_t> AlignmentPathFinder<AlignmentType>::getAlignmentStartNodesIndex(const vg::MultipathAlignment & alignment) const {

    multimap<gbwt::node_type, uint32_t> alignment_start_nodes_index;

    for (auto & start_idx: alignment.start()) {

        assert(alignment.subpath(start_idx).path().mapping_size() > 0);
        alignment_start_nodes_index.emplace(mapping_to_gbwt(alignment.subpath(start_idx).path().mapping(0)), start_idx);
    }

    return alignment_start_nodes_index;
}

template<class AlignmentType>
bool AlignmentPathFinder<AlignmentType>::isAlignmentDisconnected(const vg::Alignment & alignment) const {

    return false;
}

template<class AlignmentType>
bool AlignmentPathFinder<AlignmentType>::isAlignmentDisconnected(const vg::MultipathAlignment & alignment) const {

    bool is_connected = false;

    if (alignment.has_annotation()) {

        auto annotation_it = alignment.annotation().fields().find("disconnected");

        if (annotation_it != alignment.annotation().fields().end()) {

            assert(annotation_it->second.bool_value());
            is_connected = true;
        }
    }

    return is_connected;
}

template<class AlignmentType>
bool AlignmentPathFinder<AlignmentType>::filterAlignmentSearchPaths(const vector<AlignmentSearchPath> & align_search_paths) const {

    double max_min_rel_score = 0;

    for (auto & align_search_path: align_search_paths) {

        if (align_search_path.complete()) {

            if (align_search_path.min_mapq < min_mapq_value) {

                return true;
            }

            max_min_rel_score = max(max_min_rel_score, align_search_path.minRelativeScore());
        }
    }

    if (max_min_rel_score < min_rel_score) {

        return true;
    
    } else {

        return false;
    }
}

template class AlignmentPathFinder<vg::Alignment>;
template class AlignmentPathFinder<vg::MultipathAlignment>;

