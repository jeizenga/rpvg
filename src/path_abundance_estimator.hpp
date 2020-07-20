
#ifndef RPVG_SRC_PATHABUNDANCEESTIMATOR_HPP
#define RPVG_SRC_PATHABUNDANCEESTIMATOR_HPP

#include <vector>
#include <random>

#include <Eigen/Dense>

#include "path_estimator.hpp"
#include "path_cluster_estimates.hpp"
#include "read_path_probabilities.hpp"
#include "utils.hpp"

using namespace std;


class PathAbundanceEstimator : public PathEstimator {

    public:

        PathAbundanceEstimator(const uint32_t max_em_its_in, const double min_em_conv, const double prob_precision);
        virtual ~PathAbundanceEstimator() {};

        void estimate(PathClusterEstimates * path_cluster_estimates, const vector<ReadPathProbabilities> & cluster_probs);

    protected: 

        const uint32_t max_em_its;

        const double em_conv_min_exp;
        const double em_conv_max_rel_diff;

        void EMAbundanceEstimator(PathClusterEstimates * path_cluster_estimates, const Eigen::ColMatrixXd & read_path_probs, const Eigen::RowVectorXui & read_counts) const;
        void removeNoiseAndRenormalizeAbundances(PathClusterEstimates * path_cluster_estimates) const;    
};

class MinimumPathAbundanceEstimator : public PathAbundanceEstimator {

    public:

        MinimumPathAbundanceEstimator(const uint32_t max_em_its, const double min_em_conv, const double prob_precision);
        ~MinimumPathAbundanceEstimator() {};

        void estimate(PathClusterEstimates * path_cluster_estimates, const vector<ReadPathProbabilities> & cluster_probs);

        vector<uint32_t> weightedMinimumPathCover(const Eigen::ColMatrixXb & read_path_cover, const Eigen::RowVectorXui & read_counts, const Eigen::RowVectorXd & path_weights);
};

class NestedPathAbundanceEstimator : public PathAbundanceEstimator {

    public:

        NestedPathAbundanceEstimator(const uint32_t num_nested_its_in, const uint32_t ploidy_in, const bool use_exact_in, const uint32_t rng_seed, const uint32_t max_em_its, const double min_em_conv, const double prob_precision);
        ~NestedPathAbundanceEstimator() {};

        void estimate(PathClusterEstimates * path_cluster_estimates, const vector<ReadPathProbabilities> & cluster_probs);

    private:

        const uint32_t num_nested_its;
        const uint32_t ploidy;
        const bool use_exact;

        mt19937 mt_rng;

        vector<vector<uint32_t> > findPathOriginGroups(const vector<PathInfo> & paths) const;
        
        void samplePloidyPathIndices(vector<vector<uint32_t> > * ploidy_path_indices_samples, const PathClusterEstimates & group_path_cluster_estimates, const vector<uint32_t> & group);
                
        void updateEstimates(PathClusterEstimates * path_cluster_estimates, const PathClusterEstimates & new_path_cluster_estimates, const vector<uint32_t> & path_indices, const uint32_t sample_count) const;
};

 
#endif
