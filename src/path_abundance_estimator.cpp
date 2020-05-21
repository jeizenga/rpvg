
#include <limits>

#include "path_abundance_estimator.hpp"

#include "path_likelihood_estimator.hpp"
#include "discrete_sampler.hpp"


const uint32_t max_em_min_read_count = 10;

PathAbundanceEstimator::PathAbundanceEstimator(const uint32_t max_em_its_in, const double min_read_count_in, const double prob_precision) : max_em_its(max_em_its_in), min_read_count(min_read_count_in), PathEstimator(prob_precision) {}

void PathAbundanceEstimator::estimate(PathClusterEstimates * path_cluster_estimates, const vector<ReadPathProbabilities> & cluster_probs) {

    if (!cluster_probs.empty()) {

        Eigen::ColMatrixXd read_path_probs;
        Eigen::ColVectorXd noise_probs;
        Eigen::RowVectorXui read_counts;

        constructProbabilityMatrix(&read_path_probs, &noise_probs, &read_counts, cluster_probs, true);

        sortProbabilityMatrix(&read_path_probs, &read_counts);
        collapseProbabilityMatrix(&read_path_probs, &read_counts);

        path_cluster_estimates->abundances = Abundances(path_cluster_estimates->paths.size() + 1, false);

        expectationMaximizationEstimator(&(path_cluster_estimates->abundances), read_path_probs, read_counts);
        removeNoiseAndRenormalizeAbundances(&(path_cluster_estimates->abundances));

    } else {

        path_cluster_estimates->abundances = Abundances(path_cluster_estimates->paths.size(), true);
    }
}

void PathAbundanceEstimator::expectationMaximizationEstimator(Abundances * abundances, const Eigen::ColMatrixXd & read_path_probs, const Eigen::RowVectorXui & read_counts) const {

    abundances->read_count = read_counts.sum();
    assert(abundances->read_count > 0);

    Eigen::RowVectorXd prev_read_counts = abundances->expression * abundances->read_count;
    uint32_t em_min_read_count = 0;

    for (size_t i = 0; i < max_em_its; ++i) {

        Eigen::ColMatrixXd posteriors = read_path_probs.array().rowwise() * abundances->expression.array();
        posteriors = posteriors.array().colwise() / posteriors.rowwise().sum().array();

        abundances->expression = read_counts.cast<double>() * posteriors;

        if ((abundances->expression.array() - prev_read_counts.array()).abs().maxCoeff() < min_read_count) {

            em_min_read_count++;

            if (em_min_read_count == max_em_min_read_count) {

                break;
            }
        
        } else {

            em_min_read_count = 1;
        } 

        prev_read_counts = abundances->expression;
        abundances->expression /= abundances->read_count;   
    }

    abundances->expression = abundances->expression / abundances->expression.sum();

    for (size_t i = 0; i < abundances->expression.cols(); ++i) {

        if (abundances->expression(i) * abundances->read_count < min_read_count) {

            abundances->confidence(i) = 0;
            abundances->expression(i) = 0;            
        }
    }

    if (abundances->expression.sum() > 0) {

        abundances->expression = abundances->expression / abundances->expression.sum();
    } 
}

void PathAbundanceEstimator::removeNoiseAndRenormalizeAbundances(Abundances * abundances) const {

    const double noise_read_count = abundances->expression(0, abundances->expression.cols() - 1) * abundances->read_count;
    assert(abundances->read_count >= noise_read_count);

    abundances->confidence.conservativeResize(1, abundances->confidence.cols() - 1);
    abundances->expression.conservativeResize(1, abundances->expression.cols() - 1);

    if (abundances->expression.sum() > 0) {

        abundances->expression = abundances->expression / abundances->expression.sum();
    } 

    abundances->read_count -= noise_read_count;
}

MinimumPathAbundanceEstimator::MinimumPathAbundanceEstimator(const uint32_t max_em_its, const double min_read_count, const double prob_precision) : PathAbundanceEstimator(max_em_its, min_read_count, prob_precision) {}

void MinimumPathAbundanceEstimator::estimate(PathClusterEstimates * path_cluster_estimates, const vector<ReadPathProbabilities> & cluster_probs) {

    if (!cluster_probs.empty()) {

        Eigen::ColVectorXd noise_probs(cluster_probs.size());
        Eigen::RowVectorXui read_counts(cluster_probs.size());

        Eigen::ColMatrixXb read_path_cover(cluster_probs.size(), path_cluster_estimates->paths.size());
        Eigen::RowVectorXd path_weights = Eigen::RowVectorXd::Zero(path_cluster_estimates->paths.size());

        for (size_t i = 0; i < read_path_cover.rows(); ++i) {

            noise_probs(i) = cluster_probs.at(i).noiseProbability();

            if (doubleCompare(noise_probs(i), 1)) {

                read_counts(i) = 0;

            } else {
                
                read_counts(i) = cluster_probs.at(i).readCount();
            }

            assert(cluster_probs.at(i).probabilities().size() == read_path_cover.cols());

            for (size_t j = 0; j < path_cluster_estimates->paths.size(); ++j) {

                path_weights(j) += log(cluster_probs.at(i).probabilities().at(j) + noise_probs(i)) * read_counts(i);

                if (doubleCompare(cluster_probs.at(i).probabilities().at(j), 0)) {

                    read_path_cover(i, j) = false;

                } else {
                    
                    read_path_cover(i, j) = true;
                }
            }
        }

        path_weights *= -1;

        vector<uint32_t> min_path_cover = weightedMinimumPathCover(read_path_cover, read_counts, path_weights);

        if (min_path_cover.empty()) {

            path_cluster_estimates->abundances = Abundances(path_cluster_estimates->paths.size(), true);
            return;
        }

        Eigen::ColMatrixXd min_path_read_path_probs(cluster_probs.size(), min_path_cover.size());

        for (size_t i = 0; i < min_path_read_path_probs.rows(); ++i) {

            read_counts(i) = cluster_probs.at(i).readCount();

            for (size_t j = 0; j < min_path_cover.size(); ++j) {

                min_path_read_path_probs(i, j) = cluster_probs.at(i).probabilities().at(min_path_cover.at(j));
            }
        }
        
        addNoiseAndNormalizeProbabilityMatrix(&min_path_read_path_probs, noise_probs);

        sortProbabilityMatrix(&min_path_read_path_probs, &read_counts);
        collapseProbabilityMatrix(&min_path_read_path_probs, &read_counts);

        assert(min_path_read_path_probs.cols() > 1);
        Abundances min_path_cluster_estimates(min_path_read_path_probs.cols(), false);
        
        expectationMaximizationEstimator(&(min_path_cluster_estimates), min_path_read_path_probs, read_counts);

        path_cluster_estimates->abundances = Abundances(path_cluster_estimates->paths.size() + 1, true);
        path_cluster_estimates->abundances.read_count = read_counts.sum();

        for (size_t j = 0; j < min_path_cover.size(); j++) {

            path_cluster_estimates->abundances.confidence(min_path_cover.at(j)) = min_path_cluster_estimates.confidence(j);
            path_cluster_estimates->abundances.expression(min_path_cover.at(j)) = min_path_cluster_estimates.expression(j);
        }

        assert(min_path_cluster_estimates.confidence.cols() == min_path_cover.size() + 1);

        path_cluster_estimates->abundances.confidence(min_path_cover.size()) = min_path_cluster_estimates.confidence(min_path_cover.size());
        path_cluster_estimates->abundances.expression(min_path_cover.size()) = min_path_cluster_estimates.expression(min_path_cover.size());  
                  
        removeNoiseAndRenormalizeAbundances(&(path_cluster_estimates->abundances));

    } else {

        path_cluster_estimates->abundances = Abundances(path_cluster_estimates->paths.size(), true);
    }
}

vector<uint32_t> MinimumPathAbundanceEstimator::weightedMinimumPathCover(const Eigen::ColMatrixXb & read_path_cover, const Eigen::RowVectorXui & read_counts, const Eigen::RowVectorXd & path_weights) {

    assert(read_path_cover.rows() == read_counts.cols());
    assert(read_path_cover.cols() == path_weights.cols());

    if (read_path_cover.cols() == 1) {

        return vector<uint32_t>({0});
    }

    auto uncovered_read_counts = read_counts;

    vector<uint32_t> min_path_cover;
    min_path_cover.reserve(read_path_cover.cols());

    while (uncovered_read_counts.maxCoeff() > 0) {

        Eigen::RowVectorXd weighted_read_path_cover = (uncovered_read_counts.cast<double>() * read_path_cover.cast<double>()).array() / path_weights.array();
        assert(weighted_read_path_cover.size() == read_path_cover.cols());

        double max_weighted_read_path_cover = weighted_read_path_cover(0);
        uint32_t max_weighted_read_path_cover_idx = 0;

        for (size_t i = 1; i < weighted_read_path_cover.size(); ++i) {

            if (weighted_read_path_cover(i) > max_weighted_read_path_cover) {

                max_weighted_read_path_cover = weighted_read_path_cover(i);
                max_weighted_read_path_cover_idx = i;
            }
        }

        assert(max_weighted_read_path_cover > 0);
        min_path_cover.emplace_back(max_weighted_read_path_cover_idx);

        uncovered_read_counts = (uncovered_read_counts.array() * (!read_path_cover.col(max_weighted_read_path_cover_idx).transpose().array()).cast<uint32_t>()).matrix();
    }

    assert(min_path_cover.size() <= read_path_cover.cols());
    return min_path_cover;
}

NestedPathAbundanceEstimator::NestedPathAbundanceEstimator(const uint32_t num_nested_its_in, const uint32_t ploidy_in, const uint32_t rng_seed, const uint32_t max_em_its, const double min_read_count, const double prob_precision) : num_nested_its(num_nested_its_in), ploidy(ploidy_in), PathAbundanceEstimator(max_em_its, min_read_count, prob_precision) {

    assert(ploidy >= 1 && ploidy <= 2);
    mt_rng = mt19937(rng_seed);
}

void NestedPathAbundanceEstimator::estimate(PathClusterEstimates * path_cluster_estimates, const vector<ReadPathProbabilities> & cluster_probs) {

    if (!cluster_probs.empty()) {

        Eigen::ColMatrixXd read_path_probs;
        Eigen::ColVectorXd noise_probs;
        Eigen::RowVectorXui read_counts;

        constructProbabilityMatrix(&read_path_probs, &noise_probs, &read_counts, cluster_probs, false);

        unordered_map<string, vector<uint32_t> > path_groups;

        for (size_t i = 0; i < path_cluster_estimates->paths.size(); ++i) {

            assert(path_cluster_estimates->paths.at(i).origin != "");

            auto path_groups_it = path_groups.emplace(path_cluster_estimates->paths.at(i).origin, vector<uint32_t>());
            path_groups_it.first->second.emplace_back(i);
        }

        vector<vector<vector<uint32_t> > > group_ploidy_path_indices;
        group_ploidy_path_indices.reserve(path_groups.size());

        vector<LogDiscreteSampler> group_ploidy_log_samplers;
        group_ploidy_log_samplers.reserve(path_groups.size());

        for (auto & group: path_groups) {

            uint32_t ploidy_combinations = group.second.size();

            if (ploidy == 2) {

                ploidy_combinations = group.second.size() * (group.second.size() - 1) / 2 + group.second.size();
            }
             
            group_ploidy_path_indices.emplace_back(vector<vector<uint32_t> >());
            group_ploidy_path_indices.back().reserve(ploidy_combinations);

            group_ploidy_log_samplers.emplace_back(LogDiscreteSampler(ploidy_combinations));

            if (ploidy == 1) {

                for (size_t i = 0; i < group.second.size(); ++i) {

                    group_ploidy_path_indices.back().emplace_back(vector<uint32_t>({group.second.at(i)}));
                    group_ploidy_log_samplers.back().addOutcome(read_counts.cast<double>() * (read_path_probs.col(group.second.at(i)) + noise_probs).array().log().matrix());
                }

            } else {

                for (size_t i = 0; i < group.second.size(); ++i) {

                    group_ploidy_path_indices.back().emplace_back(vector<uint32_t>({group.second.at(i)}));
                    group_ploidy_log_samplers.back().addOutcome(read_counts.cast<double>() * (read_path_probs.col(group.second.at(i)) + read_path_probs.col(group.second.at(i)) + noise_probs).array().log().matrix());

                    for (size_t j = i + 1; j < group.second.size(); ++j) {

                        group_ploidy_path_indices.back().emplace_back(vector<uint32_t>({group.second.at(i), group.second.at(j)}));
                        group_ploidy_log_samplers.back().addOutcome(read_counts.cast<double>() * ((read_path_probs.col(group.second.at(i)) + read_path_probs.col(group.second.at(j)) + noise_probs).array()).log().matrix() + log(2));
                    }
                }
            }
        }

        unordered_map<vector<uint32_t>, uint32_t> ploidy_path_indices_samples;

        for (size_t i = 0; i < num_nested_its; ++i) {

            vector<uint32_t> ploidy_path_indices;
            ploidy_path_indices.reserve(path_groups.size() * ploidy);

            for (size_t j = 0; j < group_ploidy_path_indices.size(); ++j) {

                auto sampled_path_indices = group_ploidy_path_indices.at(j).at(group_ploidy_log_samplers.at(j).sample(&mt_rng));
                
                assert(!sampled_path_indices.empty());
                assert(sampled_path_indices.size() <= ploidy);

                ploidy_path_indices.insert(ploidy_path_indices.end(), sampled_path_indices.begin(), sampled_path_indices.end());
            }

            auto ploidy_path_indices_samples_it = ploidy_path_indices_samples.emplace(ploidy_path_indices, 0);
            ploidy_path_indices_samples_it.first->second++;
        }

        path_cluster_estimates->abundances = Abundances(path_cluster_estimates->paths.size() + 1, true);
        path_cluster_estimates->abundances.read_count = read_counts.sum();

        for (auto & path_indices_sample: ploidy_path_indices_samples) {

            assert(path_indices_sample.second > 0);
            Eigen::ColMatrixXd ploidy_read_path_probs(read_path_probs.rows(), path_indices_sample.first.size());

            for (size_t i = 0; i < path_indices_sample.first.size(); ++i) {

                ploidy_read_path_probs.col(i) = read_path_probs.col(path_indices_sample.first.at(i));
            }

            addNoiseAndNormalizeProbabilityMatrix(&ploidy_read_path_probs, noise_probs);

            auto ploidy_read_counts = read_counts;
            sortProbabilityMatrix(&ploidy_read_path_probs, &ploidy_read_counts);
            collapseProbabilityMatrix(&ploidy_read_path_probs, &ploidy_read_counts);
            assert(ploidy_read_counts.sum() == read_counts.sum());

            assert(ploidy_read_path_probs.cols() >= 2);
            Abundances ploidy_abundances(ploidy_read_path_probs.cols(), false);
            
            expectationMaximizationEstimator(&ploidy_abundances, ploidy_read_path_probs, ploidy_read_counts);

            for (size_t i = 0; i < path_indices_sample.first.size(); i += 2) {

                if (ploidy_abundances.confidence(i) > 0) {

                    assert(doubleCompare(ploidy_abundances.confidence(i), 1));

                    path_cluster_estimates->abundances.confidence(path_indices_sample.first.at(i)) += (ploidy_abundances.confidence(i) * path_indices_sample.second);
                    path_cluster_estimates->abundances.expression(path_indices_sample.first.at(i)) += (ploidy_abundances.expression(i) * path_indices_sample.second);
                }
            }

            for (size_t i = 1; i < path_indices_sample.first.size(); i += 2) {

                if (ploidy_abundances.confidence(i) > 0) {

                    assert(doubleCompare(ploidy_abundances.confidence(i), 1));

                    if (path_indices_sample.first.at(i - 1) != path_indices_sample.first.at(i)) {
                        
                        path_cluster_estimates->abundances.confidence(path_indices_sample.first.at(i)) += (ploidy_abundances.confidence(i) * path_indices_sample.second);
                    }
                    
                    path_cluster_estimates->abundances.expression(path_indices_sample.first.at(i)) += (ploidy_abundances.expression(i) * path_indices_sample.second);
                }
            }

            assert(ploidy_abundances.confidence.cols() == path_indices_sample.first.size() + 1);

            if (ploidy_abundances.confidence(path_indices_sample.first.size()) > 0) {

                path_cluster_estimates->abundances.confidence(path_cluster_estimates->paths.size()) += (ploidy_abundances.confidence(path_indices_sample.first.size()) * path_indices_sample.second);
                path_cluster_estimates->abundances.expression(path_cluster_estimates->paths.size()) += (ploidy_abundances.expression(path_indices_sample.first.size()) * path_indices_sample.second);  
            }
        }

        for (size_t i = 0; i < path_cluster_estimates->abundances.expression.cols(); ++i) {

            if (path_cluster_estimates->abundances.confidence(i) > 0) {

                path_cluster_estimates->abundances.expression(i) /= path_cluster_estimates->abundances.confidence(i);
            }

            path_cluster_estimates->abundances.confidence(i) /= num_nested_its;
        }

        removeNoiseAndRenormalizeAbundances(&(path_cluster_estimates->abundances));

    } else {

        path_cluster_estimates->abundances = Abundances(path_cluster_estimates->paths.size(), true);
    }
}

AdaptiveNestedPathAbundanceEstimator::AdaptiveNestedPathAbundanceEstimator(const uint32_t num_nested_its_in, const uint32_t ploidy_in, const uint32_t rng_seed, const uint32_t max_em_its, const double min_read_count, const double prob_precision) : num_burn_in_its(10), min_abundance(pow(10, -8)), num_nested_its(num_nested_its_in), ploidy(ploidy_in), PathAbundanceEstimator(max_em_its, min_read_count, prob_precision) {

    assert(ploidy >= 1 && ploidy <= 2);
    mt_rng = mt19937(rng_seed);
}

void AdaptiveNestedPathAbundanceEstimator::estimate(PathClusterEstimates * path_cluster_estimates, const vector<ReadPathProbabilities> & cluster_probs) {

    if (!cluster_probs.empty()) {

        Eigen::ColMatrixXd read_path_probs;
        Eigen::ColVectorXd noise_probs;
        Eigen::RowVectorXui read_counts;

        constructProbabilityMatrix(&read_path_probs, &noise_probs, &read_counts, cluster_probs, false);

        unordered_map<string, vector<uint32_t> > path_groups;

        for (size_t i = 0; i < path_cluster_estimates->paths.size(); ++i) {

            assert(path_cluster_estimates->paths.at(i).origin != "");

            auto path_groups_it = path_groups.emplace(path_cluster_estimates->paths.at(i).origin, vector<uint32_t>());
            path_groups_it.first->second.emplace_back(i);
        }

        vector<vector<vector<uint32_t> > > group_ploidy_path_indices;
        group_ploidy_path_indices.reserve(path_groups.size());

        vector<LogDiscreteSampler> group_ploidy_log_samplers;
        group_ploidy_log_samplers.reserve(path_groups.size());

        for (auto & group: path_groups) {

            uint32_t ploidy_combinations = group.second.size();

            if (ploidy == 2) {

                ploidy_combinations = group.second.size() * (group.second.size() - 1) / 2 + group.second.size();
            }
             
            group_ploidy_path_indices.emplace_back(vector<vector<uint32_t> >());
            group_ploidy_path_indices.back().reserve(ploidy_combinations);

            group_ploidy_log_samplers.emplace_back(LogDiscreteSampler(ploidy_combinations));

            if (ploidy == 1) {

                for (size_t i = 0; i < group.second.size(); ++i) {

                    group_ploidy_path_indices.back().emplace_back(vector<uint32_t>({group.second.at(i)}));
                    group_ploidy_log_samplers.back().addOutcome(read_counts.cast<double>() * (read_path_probs.col(group.second.at(i)) + noise_probs).array().log().matrix());
                }

            } else {

                for (size_t i = 0; i < group.second.size(); ++i) {

                    group_ploidy_path_indices.back().emplace_back(vector<uint32_t>({group.second.at(i), group.second.at(i)}));
                    group_ploidy_log_samplers.back().addOutcome(read_counts.cast<double>() * (read_path_probs.col(group.second.at(i)) + read_path_probs.col(group.second.at(i)) + noise_probs).array().log().matrix());

                    for (size_t j = i + 1; j < group.second.size(); ++j) {

                        group_ploidy_path_indices.back().emplace_back(vector<uint32_t>({group.second.at(i), group.second.at(j)}));
                        group_ploidy_log_samplers.back().addOutcome(read_counts.cast<double>() * ((read_path_probs.col(group.second.at(i)) + read_path_probs.col(group.second.at(j)) + noise_probs).array()).log().matrix() + log(2));
                    }
                }
            }
        }

        path_cluster_estimates->abundances = Abundances(path_cluster_estimates->paths.size() + 1, true);
        path_cluster_estimates->abundances.read_count = read_counts.sum();

        Abundances prev_ploidy_abundances;

        for (size_t s = 1; s <= num_nested_its + num_burn_in_its; ++s) {

            vector<uint32_t> ploidy_path_indices;
            ploidy_path_indices.reserve(group_ploidy_path_indices.size() * ploidy);

            Eigen::ColMatrixXd ploidy_read_path_probs(read_path_probs.rows(), group_ploidy_path_indices.size() * ploidy);

            for (size_t i = 0; i < group_ploidy_path_indices.size(); ++i) {

                auto sampled_path_indices = group_ploidy_path_indices.at(i).at(group_ploidy_log_samplers.at(i).sample(&mt_rng));
                assert(sampled_path_indices.size() == ploidy);

                for (size_t j = 0; j < sampled_path_indices.size(); ++j) {

                    ploidy_read_path_probs.col(i * ploidy + j) = read_path_probs.col(sampled_path_indices.at(j));
                }

                ploidy_path_indices.insert(ploidy_path_indices.end(), sampled_path_indices.begin(), sampled_path_indices.end());
            }

            assert(ploidy_path_indices.size() == ploidy_read_path_probs.cols());

            addNoiseAndNormalizeProbabilityMatrix(&ploidy_read_path_probs, noise_probs);

            auto ploidy_read_counts = read_counts;
            sortProbabilityMatrix(&ploidy_read_path_probs, &ploidy_read_counts);
            collapseProbabilityMatrix(&ploidy_read_path_probs, &ploidy_read_counts);
            assert(ploidy_read_counts.sum() == read_counts.sum());

            assert(ploidy_read_path_probs.cols() >= 2);
            Abundances ploidy_abundances(ploidy_read_path_probs.cols(), false);

            if (s > 1) {

                assert(prev_ploidy_abundances.expression.cols() == ploidy_abundances.expression.cols());

                for (size_t i = 0; i < prev_ploidy_abundances.expression.cols(); ++i) {

                    ploidy_abundances.expression(i) = max(prev_ploidy_abundances.expression(i), min_abundance);
                }

                ploidy_abundances.expression = ploidy_abundances.expression / ploidy_abundances.expression.sum();
            }
            
            expectationMaximizationEstimator(&ploidy_abundances, ploidy_read_path_probs, ploidy_read_counts);
            prev_ploidy_abundances = ploidy_abundances;

            if (s > num_burn_in_its) {

                for (size_t i = 0; i < ploidy_path_indices.size(); i += 2) {

                    if (ploidy_abundances.confidence(i) > 0) {

                        assert(doubleCompare(ploidy_abundances.confidence(i), 1));

                        path_cluster_estimates->abundances.confidence(ploidy_path_indices.at(i)) += ploidy_abundances.confidence(i);
                        path_cluster_estimates->abundances.expression(ploidy_path_indices.at(i)) += ploidy_abundances.expression(i);
                    }
                }

                for (size_t i = 1; i < ploidy_path_indices.size(); i += 2) {

                    if (ploidy_abundances.confidence(i) > 0) {

                        assert(doubleCompare(ploidy_abundances.confidence(i), 1));

                        if (ploidy_path_indices.at(i - 1) != ploidy_path_indices.at(i)) {
                            
                            path_cluster_estimates->abundances.confidence(ploidy_path_indices.at(i)) += ploidy_abundances.confidence(i);
                        }
                        
                        path_cluster_estimates->abundances.expression(ploidy_path_indices.at(i)) += ploidy_abundances.expression(i);
                    }
                }

                assert(ploidy_abundances.confidence.cols() == ploidy_path_indices.size() + 1);

                if (ploidy_abundances.confidence(ploidy_path_indices.size()) > 0) {

                    path_cluster_estimates->abundances.confidence(path_cluster_estimates->paths.size()) += ploidy_abundances.confidence(ploidy_path_indices.size());
                    path_cluster_estimates->abundances.expression(path_cluster_estimates->paths.size()) += ploidy_abundances.expression(ploidy_path_indices.size());  
                }
            }
        }

        for (size_t i = 0; i < path_cluster_estimates->abundances.expression.cols(); ++i) {

            if (path_cluster_estimates->abundances.confidence(i) > 0) {

                path_cluster_estimates->abundances.expression(i) /= path_cluster_estimates->abundances.confidence(i);
            }

            path_cluster_estimates->abundances.confidence(i) /= num_nested_its;
        }

        removeNoiseAndRenormalizeAbundances(&(path_cluster_estimates->abundances));

    } else {

        path_cluster_estimates->abundances = Abundances(path_cluster_estimates->paths.size(), true);
    }
}
