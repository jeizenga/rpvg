
#include "read_path_probabilities.hpp"

#include <assert.h>
#include <algorithm>
#include <numeric>
#include <limits>
#include <sstream>


ReadPathProbabilities::ReadPathProbabilities(const uint32_t read_count_in, const uint32_t num_paths, const double score_log_base_in, const FragmentLengthDist & fragment_length_dist_in) : read_count(read_count_in), score_log_base(score_log_base_in), fragment_length_dist(fragment_length_dist_in) {

    noise_prob = 1;
    read_path_probs = vector<double>(num_paths, 0);
}

uint32_t ReadPathProbabilities::readCount() const {

    return read_count;
}

double ReadPathProbabilities::noiseProbability() const {

    return noise_prob;
}

const vector<double> & ReadPathProbabilities::probabilities() const {

    return read_path_probs;
}

void ReadPathProbabilities::addReadCount(const uint32_t multiplicity_in) {

    read_count += multiplicity_in;
}

void ReadPathProbabilities::calcReadPathProbabilities(const vector<AlignmentPath> & align_paths, const vector<vector<gbwt::size_type> > & align_paths_ids, const unordered_map<uint32_t, uint32_t> & clustered_path_index, const vector<PathInfo> & cluster_paths, const bool is_single_end) {

    assert(!align_paths.empty());
    assert(align_paths.size() == align_paths_ids.size());

    assert(clustered_path_index.size() == read_path_probs.size());
    assert(cluster_paths.size() == read_path_probs.size());

    if (align_paths.front().mapq_comb > 0) {

        noise_prob = phred_to_prob(align_paths.front().mapq_comb);
        assert(noise_prob < 1);

        vector<double> align_paths_log_probs;
        align_paths_log_probs.reserve(align_paths.size());

        for (auto & align_path: align_paths) {

            align_paths_log_probs.emplace_back(score_log_base * align_path.score_sum);

            if (!is_single_end) {

                align_paths_log_probs.back() += fragment_length_dist.logProb(align_path.seq_length);
            }
        }
        
        vector<double> read_path_log_probs(read_path_probs.size(), numeric_limits<double>::lowest());

        for (size_t i = 0; i < align_paths.size(); ++i) {

            for (auto path_id: align_paths_ids.at(i)) {

                auto clustered_path_index_it = clustered_path_index.find(path_id);

                if (clustered_path_index_it != clustered_path_index.end()) {

                    uint32_t path_idx = clustered_path_index_it->second;

                    if (doubleCompare(cluster_paths.at(path_idx).effective_length, 0)) {

                        assert(doubleCompare(read_path_log_probs.at(path_idx), numeric_limits<double>::lowest()));
                        read_path_log_probs.at(path_idx) = numeric_limits<double>::lowest();

                    } else {

                        // account for really rare cases when a mpmap alignment can have multiple alignments on the same path
                        read_path_log_probs.at(path_idx) = max(read_path_log_probs.at(path_idx), align_paths_log_probs.at(i) - log(cluster_paths.at(path_idx).effective_length));
                    }
                }
            }
        }

        double read_path_log_probs_sum = numeric_limits<double>::lowest();

        for (auto & log_prob: read_path_log_probs) {

            read_path_log_probs_sum = add_log(read_path_log_probs_sum, log_prob);
        }

        assert(read_path_probs.size() == read_path_log_probs.size());
        assert(read_path_log_probs_sum > numeric_limits<double>::lowest());

        for (size_t i = 0; i < read_path_probs.size(); ++i) {

            read_path_probs.at(i) = exp(read_path_log_probs.at(i) - read_path_log_probs_sum);
            read_path_probs.at(i) *= (1 - noise_prob);
        }
    }
}

bool ReadPathProbabilities::mergeIdenticalReadPathProbabilities(const ReadPathProbabilities & probs_2, const double prob_precision) {

    assert(probabilities().size() == probs_2.probabilities().size());

    if (abs(noiseProbability() - probs_2.noiseProbability()) < prob_precision) {

        for (size_t i = 0; i < probabilities().size(); ++i) {

            if (abs(probabilities().at(i) - probs_2.probabilities().at(i)) >= prob_precision) {

                return false;
            }
        }

        addReadCount(probs_2.readCount());
        return true;
    } 

    return false;
}

vector<pair<double, vector<uint32_t> > > ReadPathProbabilities::collapsedProbabilities(const double precision) const {

    vector<pair<double, vector<uint32_t> > > collapsed_probs;

    for (size_t i = 0; i < read_path_probs.size(); ++i) {

        auto collapsed_probs_it = collapsed_probs.begin();

        while (collapsed_probs_it != collapsed_probs.end()) {

            if (abs(collapsed_probs_it->first - read_path_probs.at(i)) < precision) {

                collapsed_probs_it->second.emplace_back(i);
                break;
            }
            
            ++collapsed_probs_it;
        }

        if (collapsed_probs_it == collapsed_probs.end()) {

            collapsed_probs.emplace_back(read_path_probs.at(i), vector<uint32_t>(1, i));
        }
    }

    sort(collapsed_probs.begin(), collapsed_probs.end());
    return collapsed_probs;
}

bool operator==(const ReadPathProbabilities & lhs, const ReadPathProbabilities & rhs) { 

    if (lhs.readCount() == rhs.readCount() && doubleCompare(lhs.noiseProbability(), rhs.noiseProbability())) {

        if (lhs.probabilities().size() == rhs.probabilities().size()) {

            for (size_t i = 0; i < lhs.probabilities().size(); ++i) {

                if (!doubleCompare(lhs.probabilities().at(i), rhs.probabilities().at(i))) {

                    return false;
                }
            }

            return true;
        }
    } 

    return false;
}

bool operator!=(const ReadPathProbabilities & lhs, const ReadPathProbabilities & rhs) { 

    return !(lhs == rhs);
}

bool operator<(const ReadPathProbabilities & lhs, const ReadPathProbabilities & rhs) { 

    if (!doubleCompare(lhs.noiseProbability(), rhs.noiseProbability())) {

        return (lhs.noiseProbability() < rhs.noiseProbability());    
    } 

    assert(lhs.probabilities().size() == rhs.probabilities().size());

    for (size_t i = 0; i < lhs.probabilities().size(); ++i) {

        if (!doubleCompare(lhs.probabilities().at(i), rhs.probabilities().at(i))) {

            return (lhs.probabilities().at(i) < rhs.probabilities().at(i));    
        }         
    }   

    if (lhs.readCount() != rhs.readCount()) {

        return (lhs.readCount() < rhs.readCount());
    }

    return false;
}

ostream & operator<<(ostream & os, const ReadPathProbabilities & read_path_probs) {

    os << read_path_probs.readCount() << " | " << read_path_probs.noiseProbability() << " |";

    for (auto & prob: read_path_probs.probabilities()) {

        os << " " << prob;
    }

    return os;
}

