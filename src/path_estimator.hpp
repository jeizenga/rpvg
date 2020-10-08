
#ifndef RPVG_SRC_PATHESTIMATOR_HPP
#define RPVG_SRC_PATHESTIMATOR_HPP

#include <vector>

#include <Eigen/Dense>

#include "path_cluster_estimates.hpp"
#include "read_path_probabilities.hpp"
#include "utils.hpp"

using namespace std;


class PathEstimator {

    public:

        PathEstimator(const double prob_precision_in);
        virtual ~PathEstimator() {};

        virtual void estimate(PathClusterEstimates * path_cluster_estimates, const vector<ReadPathProbabilities> & cluster_probs) = 0;

    protected:
       
        const double prob_precision;
 
        void constructProbabilityMatrix(Eigen::ColMatrixXd * read_path_probs, Eigen::ColVectorXd * noise_probs, Eigen::RowVectorXui * read_counts, const vector<ReadPathProbabilities> & cluster_probs, const vector<uint32_t> & path_ids);
        void addNoiseAndNormalizeProbabilityMatrix(Eigen::ColMatrixXd * read_path_probs, const Eigen::ColVectorXd & noise_probs);

        void readCollapseProbabilityMatrix(Eigen::ColMatrixXd * read_path_probs, Eigen::RowVectorXui * read_counts);
        void pathCollapseProbabilityMatrix(Eigen::ColMatrixXd * read_path_probs);

        void calculatePathGroupPosteriors(PathClusterEstimates * path_cluster_estimates, const Eigen::ColMatrixXd & read_path_probs, const Eigen::ColVectorXd & noise_probs, const Eigen::RowVectorXui & read_counts, const vector<uint32_t> & path_counts, const uint32_t group_size);
        void estimatePathGroupPosteriorsGibbs(PathClusterEstimates * path_cluster_estimates, const Eigen::ColMatrixXd & read_path_probs, const Eigen::ColVectorXd & noise_probs, const Eigen::RowVectorXui & read_counts, const vector<uint32_t> & path_counts, const uint32_t group_size, mt19937 * mt_rng);

    private:

        void rowSortProbabilityMatrix(Eigen::ColMatrixXd * read_path_probs, Eigen::RowVectorXui * read_counts);
        void colSortProbabilityMatrix(Eigen::ColMatrixXd * read_path_probs);

        vector<double> calcPathFrequences(const vector<uint32_t> & path_counts);
};

namespace std {

    template<> 
    struct hash<vector<uint32_t> >
    {
        size_t operator()(vector<uint32_t> const & vec) const
        {
            size_t seed = 0;

            for (auto & val: vec) {

                spp::hash_combine(seed, val);
            }

            return seed;
        }
    };
}


#endif
