
#include <assert.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <limits>
#include <algorithm>
#include <iomanip>
#include <thread>

#include "cxxopts.hpp"
#include "gbwt/gbwt.h"
#include "sparsepp/spp.h"
#include "vg/io/vpkg.hpp"
#include "vg/io/stream.hpp"
#include "vg/io/basic_stream.hpp"
#include "io/register_libvg_io.hpp"
#include "handlegraph/handle_graph.hpp"
#include "gssw.h"

#include "utils.hpp"
#include "fragment_length_dist.hpp"
#include "paths_index.hpp"
#include "alignment_path.hpp"
#include "alignment_path_finder.hpp"
#include "producer_consumer_queue.hpp"
#include "path_clusters.hpp"
#include "read_path_probabilities.hpp"
#include "probability_matrix_writer.hpp"
#include "path_estimator.hpp"
#include "path_posterior_estimator.hpp"
#include "path_abundance_estimator.hpp"
#include "path_cluster_estimates.hpp"
#include "path_estimates_writer.hpp"

const uint32_t align_path_buffer_size = 10000;
const uint32_t read_path_cluster_probs_buffer_size = 100;
const double prob_precision = pow(10, -8);

typedef spp::sparse_hash_map<vector<AlignmentPath>, uint32_t> align_paths_index_t;
typedef spp::sparse_hash_map<uint32_t, spp::sparse_hash_set<uint32_t> > connected_align_paths_t;

typedef ProducerConsumerQueue<vector<vector<AlignmentPath> > *> align_paths_buffer_queue_t;


void addAlignmentPathsToBufferQueue(vector<vector<AlignmentPath> > * align_paths_buffer, align_paths_buffer_queue_t * align_paths_buffer_queue, const double mean_fragment_length) {

    for (auto & align_paths: *align_paths_buffer) {

        if (!align_paths.empty()) {

            if (align_paths.size() == 1) {

                align_paths.front().seq_length = mean_fragment_length;
                align_paths.front().score_sum = 1;
            } 

            sort(align_paths.begin(), align_paths.end());
        }
    }

    align_paths_buffer_queue->push(align_paths_buffer);
}

template<class AlignmentType> 
void findAlignmentPaths(ifstream & alignments_istream, align_paths_buffer_queue_t * align_paths_buffer_queue, const PathsIndex & paths_index, const FragmentLengthDist & fragment_length_dist, const uint32_t num_threads) {

    AlignmentPathFinder<AlignmentType> align_path_finder(paths_index, fragment_length_dist.maxLength());

    auto threaded_align_path_buffer = vector<vector<vector<AlignmentPath > > *>(num_threads);

    for (auto & align_path_buffer: threaded_align_path_buffer) {

        align_path_buffer = new vector<vector<AlignmentPath > >();
        align_path_buffer->reserve(align_path_buffer_size);
    }
  
    vg::io::for_each_parallel<AlignmentType>(alignments_istream, [&](AlignmentType & alignment) {

        auto align_path_buffer = threaded_align_path_buffer.at(omp_get_thread_num());
        align_path_buffer->emplace_back(align_path_finder.findAlignmentPaths(alignment));

        if (align_path_buffer->size() == align_path_buffer_size) {

            addAlignmentPathsToBufferQueue(align_path_buffer, align_paths_buffer_queue, fragment_length_dist.mean());
            
            threaded_align_path_buffer.at(omp_get_thread_num()) = new vector<vector<AlignmentPath > >();
            threaded_align_path_buffer.at(omp_get_thread_num())->reserve(align_path_buffer_size);
        }
    });

    for (auto & align_path_buffer: threaded_align_path_buffer) {

        addAlignmentPathsToBufferQueue(align_path_buffer, align_paths_buffer_queue, fragment_length_dist.mean());
    }
}

template<class AlignmentType> 
void findPairedAlignmentPaths(ifstream & alignments_istream, align_paths_buffer_queue_t * align_paths_buffer_queue, const PathsIndex & paths_index, const FragmentLengthDist & fragment_length_dist, const uint32_t num_threads) {

    AlignmentPathFinder<AlignmentType> align_path_finder(paths_index, fragment_length_dist.maxLength());

    auto threaded_align_path_buffer = vector<vector<vector<AlignmentPath > > *>(num_threads);

    for (auto & align_path_buffer: threaded_align_path_buffer) {

        align_path_buffer = new vector<vector<AlignmentPath > >();
        align_path_buffer->reserve(align_path_buffer_size);
    }
  
    vg::io::for_each_interleaved_pair_parallel<AlignmentType>(alignments_istream, [&](AlignmentType & alignment_1, AlignmentType & alignment_2) {

        auto align_path_buffer = threaded_align_path_buffer.at(omp_get_thread_num());
        align_path_buffer->emplace_back(align_path_finder.findPairedAlignmentPaths(alignment_1, alignment_2));

        if (align_path_buffer->size() == align_path_buffer_size) {

            addAlignmentPathsToBufferQueue(align_path_buffer, align_paths_buffer_queue, fragment_length_dist.mean());
            
            threaded_align_path_buffer.at(omp_get_thread_num()) = new vector<vector<AlignmentPath > >();
            threaded_align_path_buffer.at(omp_get_thread_num())->reserve(align_path_buffer_size);
        }
    });

    for (auto & align_path_buffer: threaded_align_path_buffer) {

        addAlignmentPathsToBufferQueue(align_path_buffer, align_paths_buffer_queue, fragment_length_dist.mean());
    }
}

void addAlignmentPathsBufferToIndexes(align_paths_buffer_queue_t * align_paths_buffer_queue, align_paths_index_t * align_paths_index) {

    vector<vector<AlignmentPath> > * align_paths_buffer = nullptr;

    while (align_paths_buffer_queue->pop(&align_paths_buffer)) {

        for (auto & align_paths: *align_paths_buffer) {

            if (!align_paths.empty()) {

                auto threaded_align_paths_index_it = align_paths_index->emplace(align_paths, 0);
                threaded_align_paths_index_it.first->second++;
            }   
        } 

        delete align_paths_buffer;
    }
}

spp::sparse_hash_map<string, string> parsePathTranscriptOrigin(const string & filename) {

    spp::sparse_hash_map<string, string> path_transcript_origin;

    ifstream origin_file(filename);
    
    string line;
    string element;

    while (origin_file.good()) {

        getline(origin_file, line);

        if (line.empty()) {

            continue;
        }

        auto line_ss = stringstream(line);

        getline(line_ss, element, '\t');

        if (element == "Name") {

            continue;
        }

        auto path_transcript_origin_it = path_transcript_origin.emplace(element, "");
        assert(path_transcript_origin_it.second);

        getline(line_ss, element, '\t');        
        getline(line_ss, element, '\t');

        path_transcript_origin_it.first->second = element;

        getline(line_ss, element, '\n');
    }

    origin_file.close();

    return path_transcript_origin;
}

int main(int argc, char* argv[]) {

    cxxopts::Options options("rpvg", "rpvg - infers path posterior probabilities and abundances from variation graph read alignments");

    options.add_options("Required")
      ("g,graph", "xg graph filename", cxxopts::value<string>())
      ("p,paths", "GBWT index filename", cxxopts::value<string>())
      ("a,alignments", "gam(p) alignment filename", cxxopts::value<string>())
      ("i,inference-model", "inference model to use (haplotypes, transcripts, strains or haplotype-transcripts)", cxxopts::value<string>())
      ;

    options.add_options("General")
      ("o,output", "output filename", cxxopts::value<string>()->default_value("stdout"))    
      ("t,threads", "number of compute threads (+= 1 thread)", cxxopts::value<uint32_t>()->default_value("1"))
      ("r,rng-seed", "seed for random number generator (default: unix time)", cxxopts::value<uint64_t>())
      ("h,help", "print help", cxxopts::value<bool>())
      ;

    options.add_options("Alignment")
      ("u,multipath", "alignment input is multipath gamp format (default: gam)", cxxopts::value<bool>())
      ("s,single-end", "alignment input is single-end reads", cxxopts::value<bool>())
      ("l,long-reads", "alignment input is single-molecule long reads (single-end only)", cxxopts::value<bool>())
      ;

    options.add_options("Probability")
      ("m,frag-mean", "mean for fragment length distribution", cxxopts::value<double>())
      ("d,frag-sd", "standard deviation for fragment length distribution", cxxopts::value<double>())
      ("b,prob-output", "write read path probabilities to file", cxxopts::value<string>())
      ;

    options.add_options("Abundance")
      ("y,ploidy", "sample ploidy", cxxopts::value<uint32_t>()->default_value("2"))
      ("j,use-exact", "use slower exact likelihood inference for haplotyping", cxxopts::value<bool>())
      ("n,num-hap-its", "number of haplotyping iterations", cxxopts::value<uint32_t>()->default_value("1000"))
      ("e,max-em-its", "maximum number of EM iterations", cxxopts::value<uint32_t>()->default_value("10000"))
      ("c,min-em-conv", "minimum abundance value used for EM convergence", cxxopts::value<double>()->default_value("0.01"))
      ("f,path-origin", "path transcript origin filename (required for haplotype-transcript inference)", cxxopts::value<string>())
      ;

    if (argc == 1) {

        cerr << options.help({"Required", "General", "Alignment", "Probability", "Abundance"}) << endl;
        return 1;
    }

    auto option_results = options.parse(argc, argv);

    if (option_results.count("help")) {

        cerr << options.help({"Required", "General", "Alignment", "Probability", "Abundance"}) << endl;
        return 1;
    }

    if (!option_results.count("graph")) {

        cerr << "ERROR: Graph (xg format) input required (--graph)." << endl;
        return 1;
    }

    if (!option_results.count("paths")) {

        cerr << "ERROR: Paths (GBWT index) input required (--paths)." << endl;
        return 1;
    }

    if (!option_results.count("alignments")) {

        cerr << "ERROR: Alignments (gam or gamp format) input required (--alignments)." << endl;
        return 1;
    }

    if (!option_results.count("inference-model")) {

        cerr << "ERROR: Inference model required (--inference-model). Options: haplotypes, transcripts, strains or haplotype-transcripts." << endl;
        return 1;
    }

    const string inference_model = option_results["inference-model"].as<string>();

    if (inference_model != "haplotypes" && inference_model != "transcripts" && inference_model != "strains" && inference_model != "haplotype-transcripts") {

        cerr << "ERROR: Inference model provided (--inference-model) not supported. Options: haplotypes, transcripts, strains or haplotype-transcripts." << endl;
        return 1;
    }
    
    const uint32_t ploidy = option_results["ploidy"].as<uint32_t>();

    if (ploidy == 0) {

        cerr << "ERROR: Ploidy (--ploidy) can not be 0." << endl;
        return 1;        
    }

    if (inference_model == "haplotype-transcripts" && !option_results.count("path-origin")) {

        cerr << "ERROR: Path transcript origin information file (--path-origin) needed when running in haplotype-transcript inference mode (--write-info output from vg rna)." << endl;
        return 1;
    }

    uint64_t rng_seed = 0; 

    if (option_results.count("rng-seed")) {

        rng_seed = option_results["rng-seed"].as<uint64_t>();

    } else {

        rng_seed = time(nullptr);
    }


    cerr << "Running rpvg (commit: " << GIT_COMMIT << ")" << endl;
    cerr << "Random number generator seed: " << rng_seed << endl;

    bool is_single_end = option_results.count("single-end");
    bool is_long_reads = option_results.count("long-reads");
    bool is_multipath = option_results.count("multipath");

    if (is_long_reads) {

        is_single_end = true;
    }

    if (option_results.count("frag-mean") != option_results.count("frag-sd")) {

        cerr << "ERROR: Both --frag-mean and --frag-sd needs to be given as input. Alternative, no values can be given for paired-end, non-long read alignments and the parameter estimated during mapping will be used instead (contained in the alignment file)." << endl;
        return 1;
    }

    FragmentLengthDist fragment_length_dist; 

    if (!option_results.count("frag-mean") && !option_results.count("frag-sd")) {

        if (is_single_end && !is_long_reads) {

            cerr << "ERROR: Both --frag-mean and --frag-sd needs to be given as input when using single-end, non-long read alignments." << endl;
            return 1;            
        }

        ifstream frag_alignments_istream(option_results["alignments"].as<string>());
        assert(frag_alignments_istream.is_open());

        fragment_length_dist = FragmentLengthDist(&frag_alignments_istream, is_multipath);

        frag_alignments_istream.close();

        if (!fragment_length_dist.isValid()) {

            cerr << "ERROR: No fragment length distribution parameters found in alignments. Use --frag-mean and --frag-sd instead." << endl;
            return 1;
        
        } else {

            cerr << "Fragment length distribution parameters found in alignment (mean: " << fragment_length_dist.mean() << ", standard deviation: " << fragment_length_dist.sd() << ")" << endl;
        }      

    } else {

        fragment_length_dist = FragmentLengthDist(option_results["frag-mean"].as<double>(), option_results["frag-sd"].as<double>());
        cerr << "Fragment length distribution parameters given as input (mean: " << fragment_length_dist.mean() << ", standard deviation: " << fragment_length_dist.sd() << ")" << endl;
    }

    cerr << endl;

    assert(fragment_length_dist.isValid());

    const uint32_t num_threads = option_results["threads"].as<uint32_t>();

    assert(num_threads > 0);
    omp_set_num_threads(num_threads);

    double time1 = gbwt::readTimer();

    assert(vg::io::register_libvg_io());

    unique_ptr<handlegraph::HandleGraph> graph = vg::io::VPKG::load_one<handlegraph::HandleGraph>(option_results["graph"].as<string>());
    unique_ptr<gbwt::GBWT> gbwt_index = vg::io::VPKG::load_one<gbwt::GBWT>(option_results["paths"].as<string>());

    PathsIndex paths_index(*gbwt_index, *graph);
    graph.reset(nullptr);

    if (paths_index.index().metadata.paths() == 0) {

        cerr << "ERROR: The GBWT index does not contain any paths." << endl;
        return 1;        
    }

    double time2 = gbwt::readTimer();
    cerr << "Loaded graph and GBWT (" << time2 - time1 << " seconds, " << gbwt::inGigabytes(gbwt::memoryUsage()) << " GB)" << endl;

    ifstream alignments_istream(option_results["alignments"].as<string>());
    assert(alignments_istream.is_open());

    align_paths_index_t align_paths_index;
    auto align_paths_buffer_queue = new align_paths_buffer_queue_t(num_threads * 3);

    thread indexing_thread(addAlignmentPathsBufferToIndexes, align_paths_buffer_queue, &align_paths_index);

    if (is_single_end) {

        if (is_multipath) {

            findAlignmentPaths<vg::MultipathAlignment>(alignments_istream, align_paths_buffer_queue, paths_index, fragment_length_dist, num_threads);

        } else {

            findAlignmentPaths<vg::Alignment>(alignments_istream, align_paths_buffer_queue, paths_index, fragment_length_dist, num_threads);
        }

    } else {

        if (is_multipath) {

            findPairedAlignmentPaths<vg::MultipathAlignment>(alignments_istream, align_paths_buffer_queue, paths_index, fragment_length_dist, num_threads);

        } else {

            findPairedAlignmentPaths<vg::Alignment>(alignments_istream, align_paths_buffer_queue, paths_index, fragment_length_dist, num_threads);
        }        
    }

    alignments_istream.close();
    align_paths_buffer_queue->pushedLast();

    indexing_thread.join();
    delete align_paths_buffer_queue;

    cerr << align_paths_index.size() << endl;

    double time3 = gbwt::readTimer();
    cerr << "Found alignment paths (" << time3 - time2 << " seconds, " << gbwt::inGigabytes(gbwt::memoryUsage()) << " GB)" << endl;

    PathClusters path_clusters(num_threads);
    auto node_to_path_index = path_clusters.findPathNodeClusters(paths_index);

    double time6 = gbwt::readTimer();
    cerr << "Created alignment path clusters (" << time6 - time3 << " seconds, " << gbwt::inGigabytes(gbwt::memoryUsage()) << " GB)" << endl;

    vector<vector<align_paths_index_t::iterator> > align_paths_clusters(path_clusters.cluster_to_paths_index.size());

    auto align_paths_index_it = align_paths_index.begin();

    while (align_paths_index_it != align_paths_index.end()) {

        auto node_id = gbwt::Node::id(align_paths_index_it->first.front().search_state.node);

        align_paths_clusters.at(path_clusters.path_to_cluster_index.at(node_to_path_index.at(node_id))).emplace_back(align_paths_index_it);
        ++align_paths_index_it;
    }

    double time7 = gbwt::readTimer();
    cerr << "Clustered alignment paths (" << time7 - time6 << " seconds, " << gbwt::inGigabytes(gbwt::memoryUsage()) << " GB)" << endl;

    ProbabilityMatrixWriter * prob_matrix_writer = nullptr;

    spp::sparse_hash_map<string, string> path_transcript_origin;
   
    if (option_results.count("prob-output")) {

        prob_matrix_writer = new ProbabilityMatrixWriter(false, option_results["prob-output"].as<string>(), prob_precision);
    }

    PathEstimator * path_estimator;

    if (inference_model == "haplotypes") {

        path_estimator = new PathGroupPosteriorEstimator(option_results["num-hap-its"].as<uint32_t>(), ploidy, option_results.count("use-exact"), rng_seed, prob_precision);

    } else if (inference_model == "transcripts") {

        path_estimator = new PathAbundanceEstimator(option_results["max-em-its"].as<uint32_t>(), option_results["min-em-conv"].as<double>(), prob_precision);

    } else if (inference_model == "strains") {

        path_estimator = new MinimumPathAbundanceEstimator(option_results["max-em-its"].as<uint32_t>(), option_results["min-em-conv"].as<double>(), prob_precision);

    } else if (inference_model == "haplotype-transcripts") {

        path_estimator = new NestedPathAbundanceEstimator(option_results["num-hap-its"].as<uint32_t>(), ploidy, option_results.count("use-exact"), rng_seed, option_results["max-em-its"].as<uint32_t>(), option_results["min-em-conv"].as<double>(), prob_precision);
     
        path_transcript_origin = parsePathTranscriptOrigin(option_results["path-origin"].as<string>());

    } else {

        assert(false);
    }

    vector<vector<vector<ReadPathProbabilities> > > threaded_read_path_cluster_probs_buffer(num_threads);
    vector<vector<PathClusterEstimates> > threaded_path_cluster_estimates(num_threads);

    for (size_t i = 0; i < num_threads; ++i) {

        threaded_path_cluster_estimates.at(i).reserve(ceil(align_paths_clusters.size()) / static_cast<float>(num_threads));
    }

    const double score_log_base = gssw_dna_recover_log_base(1, 4, 0.5, double_precision);

    auto align_paths_clusters_indices = vector<pair<uint32_t, uint32_t> >();
    align_paths_clusters_indices.reserve(align_paths_clusters.size());

    for (size_t i = 0; i < align_paths_clusters.size(); ++i) {

        align_paths_clusters_indices.emplace_back(path_clusters.cluster_to_paths_index.at(i).size(), i);
    }

    sort(align_paths_clusters_indices.rbegin(), align_paths_clusters_indices.rend());

    #pragma omp parallel for schedule(dynamic, 1)
    for (size_t i = 0; i < align_paths_clusters_indices.size(); ++i) {

        auto align_paths_cluster_idx = align_paths_clusters_indices.at(i).second;

        auto * read_path_cluster_probs_buffer = &(threaded_read_path_cluster_probs_buffer.at(omp_get_thread_num()));

        read_path_cluster_probs_buffer->emplace_back(vector<ReadPathProbabilities>());            
        read_path_cluster_probs_buffer->back().reserve(align_paths_clusters.at(align_paths_cluster_idx).size());

        unordered_map<uint32_t, uint32_t> clustered_path_index;

        auto * path_cluster_estimates = &(threaded_path_cluster_estimates.at(omp_get_thread_num()));
        path_cluster_estimates->emplace_back(PathClusterEstimates());

        path_cluster_estimates->back().paths.reserve(path_clusters.cluster_to_paths_index.at(align_paths_cluster_idx).size());
        
        for (auto & path_id: path_clusters.cluster_to_paths_index.at(align_paths_cluster_idx)) {

            assert(clustered_path_index.emplace(path_id, clustered_path_index.size()).second);
            path_cluster_estimates->back().paths.emplace_back(PathInfo());

            path_cluster_estimates->back().paths.back().name = paths_index.pathName(path_id);

            auto path_transcript_origin_it = path_transcript_origin.find(path_cluster_estimates->back().paths.back().name);

            if (path_transcript_origin_it != path_transcript_origin.end()) {

                path_cluster_estimates->back().paths.back().origin = path_transcript_origin_it->second;
            }

            path_cluster_estimates->back().paths.back().length = paths_index.pathLength(path_id); 

            if (is_long_reads) {

                path_cluster_estimates->back().paths.back().effective_length = paths_index.pathLength(path_id); 

            } else {

                path_cluster_estimates->back().paths.back().effective_length = paths_index.effectivePathLength(path_id, fragment_length_dist); 
            }
        }

        for (auto & align_paths: align_paths_clusters.at(align_paths_cluster_idx)) {

            vector<vector<gbwt::size_type> > align_paths_ids;
            align_paths_ids.reserve(align_paths->first.size());

            for (auto & align_path: align_paths->first) {

                align_paths_ids.emplace_back(paths_index.locatePathIds(align_path.search_state));
            }

            read_path_cluster_probs_buffer->back().emplace_back(ReadPathProbabilities(align_paths->second, clustered_path_index.size(), score_log_base, fragment_length_dist));
            read_path_cluster_probs_buffer->back().back().calcReadPathProbabilities(align_paths->first, align_paths_ids, clustered_path_index, path_cluster_estimates->back().paths, is_single_end);
        }

        sort(read_path_cluster_probs_buffer->back().begin(), read_path_cluster_probs_buffer->back().end());
        
        path_estimator->estimate(&(path_cluster_estimates->back()), read_path_cluster_probs_buffer->back());

        if (prob_matrix_writer) {

            if (read_path_cluster_probs_buffer->size() == read_path_cluster_probs_buffer_size) {

                assert(path_cluster_estimates->size() % read_path_cluster_probs_buffer_size == 0);

                assert(path_cluster_estimates->size() >= read_path_cluster_probs_buffer->size());
                size_t path_cluster_estimates_idx = path_cluster_estimates->size() - read_path_cluster_probs_buffer->size();

                prob_matrix_writer->lockWriter();

                for (size_t j = 0; j < read_path_cluster_probs_buffer->size(); ++j) {

                    prob_matrix_writer->writeReadPathProbabilityCluster(read_path_cluster_probs_buffer->at(j), path_cluster_estimates->at(path_cluster_estimates_idx).paths);
                    ++path_cluster_estimates_idx;
                }

                prob_matrix_writer->unlockWriter();
                read_path_cluster_probs_buffer->clear();       
            } 

        } else {

            read_path_cluster_probs_buffer->clear();
        }
    }

    if (prob_matrix_writer) {

        for (size_t i = 0; i < num_threads; ++i) {

            assert(threaded_path_cluster_estimates.at(i).size() >= threaded_read_path_cluster_probs_buffer.at(i).size());
            size_t path_cluster_estimates_idx = threaded_path_cluster_estimates.at(i).size() - threaded_read_path_cluster_probs_buffer.at(i).size();

            for (size_t j = 0; j < threaded_read_path_cluster_probs_buffer.at(i).size(); ++j) {

                prob_matrix_writer->writeReadPathProbabilityCluster(threaded_read_path_cluster_probs_buffer.at(i).at(j), threaded_path_cluster_estimates.at(i).at(path_cluster_estimates_idx).paths);
                ++path_cluster_estimates_idx;
            }
        }
    } 

    delete prob_matrix_writer;
    delete path_estimator;

    PathEstimatesWriter path_estimates_writer(option_results["output"].as<string>() == "stdout", option_results["output"].as<string>());

    if (inference_model == "haplotypes") {

        path_estimates_writer.writeThreadedPathClusterPosteriors(threaded_path_cluster_estimates, ploidy); 

    } else {

        path_estimates_writer.writeThreadedPathClusterAbundances(threaded_path_cluster_estimates);    
    }

    double time8 = gbwt::readTimer();
    cerr << "Inferred path posterior probabilities" << ((inference_model != "haplotypes") ? " and abundances" : "") << " (" << time8 - time7 << " seconds, " << gbwt::inGigabytes(gbwt::memoryUsage()) << " GB)" << endl;

	return 0;
}

