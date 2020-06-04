
#include <limits>
#include <chrono>

#include "path_abundance_estimator.hpp"

#include "path_likelihood_estimator.hpp"
#include "discrete_sampler.hpp"


const uint32_t min_em_conv_its = 10;
const double min_expression = pow(10, -8);

PathAbundanceEstimator::PathAbundanceEstimator(const uint32_t max_em_its_in, const double min_em_conv, const double prob_precision) : max_em_its(max_em_its_in), em_conv_min_exp(min_em_conv), em_conv_max_rel_diff(min_em_conv), PathEstimator(prob_precision) {}

void PathAbundanceEstimator::estimate(PathClusterEstimates * path_cluster_estimates, const vector<ReadPathProbabilities> & cluster_probs) {

    if (!cluster_probs.empty()) {

        Eigen::ColMatrixXd read_path_probs;
        Eigen::ColVectorXd noise_probs;
        Eigen::RowVectorXui read_counts;

        constructProbabilityMatrix(&read_path_probs, &noise_probs, &read_counts, cluster_probs, true);
        rowCollapseProbabilityMatrix(&read_path_probs, &read_counts);

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

    Eigen::RowVectorXd prev_expression = abundances->expression;
    uint32_t em_conv_its = 0;

    for (size_t i = 0; i < max_em_its; ++i) {

        Eigen::ColMatrixXd posteriors = read_path_probs.array().rowwise() * abundances->expression.array();
        posteriors = posteriors.array().colwise() / posteriors.rowwise().sum().array();

        abundances->expression = read_counts.cast<double>() * posteriors;
        abundances->expression /= abundances->read_count;

        bool has_converged = true;

        for (size_t i = 0; i < abundances->expression.cols(); ++i) {

            if (abundances->expression(i) > em_conv_min_exp) {

                auto relative_expression_diff = fabs(abundances->expression(i) - prev_expression(i)) / abundances->expression(i);

                if (relative_expression_diff > em_conv_max_rel_diff) {

                    has_converged = false;
                    break;
                }
            }
        }

        if (has_converged) {

            em_conv_its++;

            if (em_conv_its == min_em_conv_its) {

                break;
            }
        
        } else {

            em_conv_its = 0;
        } 

        prev_expression = abundances->expression;
    }

    double expression_sum = 0;

    for (size_t i = 0; i < abundances->expression.cols(); ++i) {

        if (abundances->expression(i) < min_expression) {

            abundances->confidence(i) = 0;
            abundances->expression(i) = 0;            
        
        } else {

            expression_sum += abundances->expression(i);
        }
    }

    abundances->expression = abundances->expression / expression_sum;
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

MinimumPathAbundanceEstimator::MinimumPathAbundanceEstimator(const uint32_t max_em_its, const double min_em_conv, const double prob_precision) : PathAbundanceEstimator(max_em_its, min_em_conv, prob_precision) {}

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
        rowCollapseProbabilityMatrix(&min_path_read_path_probs, &read_counts);

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

NestedPathAbundanceEstimator::NestedPathAbundanceEstimator(const uint32_t num_nested_its_in, const uint32_t ploidy_in, const bool use_mh_gibbs_in, const uint32_t rng_seed, const uint32_t max_em_its, const double min_em_conv, const double prob_precision) : num_nested_its(num_nested_its_in), ploidy(ploidy_in), use_mh_gibbs(use_mh_gibbs_in), PathAbundanceEstimator(max_em_its, min_em_conv, prob_precision) {

    assert(ploidy >= 1 && ploidy <= 2);
    mt_rng = mt19937(rng_seed);
}

void NestedPathAbundanceEstimator::estimate(PathClusterEstimates * path_cluster_estimates, const vector<ReadPathProbabilities> & cluster_probs) {

    if (!cluster_probs.empty()) {

        Eigen::ColMatrixXd read_path_probs;
        Eigen::ColVectorXd noise_probs;
        Eigen::RowVectorXui read_counts;

        double time1 = gbwt::readTimer();

        cerr << "\n\n##" << endl;

        constructProbabilityMatrix(&read_path_probs, &noise_probs, &read_counts, cluster_probs, true);

        double time2 = gbwt::readTimer();
        cerr << "Construct: " << path_cluster_estimates->paths.front().origin << " " << time2 - time1 << " " << read_path_probs.cols() << " " << read_path_probs.rows() << endl;

        rowCollapseProbabilityMatrix(&read_path_probs, &read_counts);

        noise_probs = read_path_probs.col(read_path_probs.cols() - 1);
        read_path_probs.conservativeResize(read_path_probs.rows(), read_path_probs.cols() - 1);

        double time3 = gbwt::readTimer();
        cerr << "Row collapse: " << path_cluster_estimates->paths.front().origin << " " << time3 - time2 << " " << read_path_probs.cols() << " " << read_path_probs.rows() << endl;

        unordered_map<vector<uint32_t>, uint32_t> ploidy_path_indices_samples;

        if (use_mh_gibbs) {

            ploidy_path_indices_samples = samplePloidyPathIndicesMHGibbs(path_cluster_estimates->paths, read_path_probs, noise_probs, read_counts);

        } else {

            ploidy_path_indices_samples = samplePloidyPathIndicesExact(path_cluster_estimates->paths, read_path_probs, noise_probs, read_counts);
        }

        double time5 = gbwt::readTimer();
        cerr << "Sample: " << path_cluster_estimates->paths.front().origin << " " << time5 - time3 << " " << read_path_probs.cols() << " " << read_path_probs.rows() << " " << ploidy_path_indices_samples.begin()->first.size() << " " << ploidy_path_indices_samples.size()<< endl;

        path_cluster_estimates->abundances = Abundances(path_cluster_estimates->paths.size() + 1, true);
        path_cluster_estimates->abundances.read_count = read_counts.sum();

        for (auto & path_indices_sample: ploidy_path_indices_samples) {

            assert(path_indices_sample.second > 0);

            Eigen::ColMatrixXd ploidy_read_path_probs;

            constructPloidyProbabilityMatrix(&ploidy_read_path_probs, read_path_probs, path_indices_sample.first);
            addNoiseAndNormalizeProbabilityMatrix(&ploidy_read_path_probs, noise_probs);

            auto ploidy_read_counts = read_counts;
            rowCollapseProbabilityMatrix(&ploidy_read_path_probs, &ploidy_read_counts);
            assert(ploidy_read_counts.sum() == read_counts.sum());

            assert(ploidy_read_path_probs.cols() >= 2);
            Abundances ploidy_abundances(ploidy_read_path_probs.cols(), false);
            
            expectationMaximizationEstimator(&ploidy_abundances, ploidy_read_path_probs, ploidy_read_counts);
            updateAbundances(&(path_cluster_estimates->abundances), ploidy_abundances, path_indices_sample.first, path_indices_sample.second);
        }

        for (size_t i = 0; i < path_cluster_estimates->abundances.expression.cols(); ++i) {

            if (path_cluster_estimates->abundances.confidence(i) > 0) {

                path_cluster_estimates->abundances.expression(i) /= path_cluster_estimates->abundances.confidence(i);
            }

            path_cluster_estimates->abundances.confidence(i) /= num_nested_its;
        }

        removeNoiseAndRenormalizeAbundances(&(path_cluster_estimates->abundances));

        double time6 = gbwt::readTimer();
        cerr << "Infer: " << path_cluster_estimates->paths.front().origin << " " << time6 - time5 << " " << read_path_probs.cols() << " " << read_path_probs.rows() << " " << ploidy_path_indices_samples.begin()->first.size() << " " << ploidy_path_indices_samples.size()<< endl;

    } else {

        path_cluster_estimates->abundances = Abundances(path_cluster_estimates->paths.size(), true);
    }
}

vector<vector<uint32_t> > NestedPathAbundanceEstimator::findPathOriginGroups(const vector<PathInfo> & paths) const {

    vector<vector<uint32_t> > path_groups;
    unordered_map<string, uint32_t> path_group_indexes;

    for (size_t i = 0; i < paths.size(); ++i) {

        assert(paths.at(i).origin != "");

        auto path_group_indexes_it = path_group_indexes.emplace(paths.at(i).origin, path_group_indexes.size());

        if (path_group_indexes_it.second) {

            path_groups.emplace_back(vector<uint32_t>());
        }

        path_groups.at(path_group_indexes_it.first->second).emplace_back(i);
    }

    return path_groups;
}

unordered_map<vector<uint32_t>, uint32_t> NestedPathAbundanceEstimator::samplePloidyPathIndicesExact(const vector<PathInfo> & paths, const Eigen::ColMatrixXd & read_path_probs, const Eigen::ColVectorXd & noise_probs, const Eigen::RowVectorXui & read_counts) {

    auto path_groups = findPathOriginGroups(paths);

    vector<vector<vector<uint32_t> > > group_ploidy_path_indices;
    group_ploidy_path_indices.reserve(path_groups.size());

    vector<LogDiscreteSampler> group_ploidy_log_samplers;
    group_ploidy_log_samplers.reserve(path_groups.size());

    for (auto & group: path_groups) {

        Eigen::ColMatrixXd group_read_path_probs = Eigen::ColMatrixXd(read_path_probs.rows(), group.size());

        for (size_t i = 0; i < group.size(); ++i) {

            group_read_path_probs.col(i) = read_path_probs.col(group.at(i));
        }

        addNoiseAndNormalizeProbabilityMatrix(&group_read_path_probs, noise_probs);

        Eigen::RowVectorXui group_read_counts = read_counts;
        rowCollapseProbabilityMatrix(&group_read_path_probs, &group_read_counts);
        
        assert(group_read_path_probs.cols() == group.size() + 1);
        assert(group_read_counts.sum() == read_counts.sum());

        uint32_t ploidy_combinations = group.size();

        if (ploidy == 2) {

            ploidy_combinations = group.size() * (group.size() - 1) / 2 + group.size();
        }
         
        group_ploidy_path_indices.emplace_back(vector<vector<uint32_t> >());
        group_ploidy_path_indices.back().reserve(ploidy_combinations);

        group_ploidy_log_samplers.emplace_back(LogDiscreteSampler(ploidy_combinations));

        if (ploidy == 1) {

            for (size_t i = 0; i < group.size(); ++i) {

                group_ploidy_path_indices.back().emplace_back(vector<uint32_t>({group.at(i)}));
                group_ploidy_log_samplers.back().addOutcome(group_read_counts.cast<double>() * (group_read_path_probs.col(i) + group_read_path_probs.col(group.size())).array().log().matrix());
            }

        } else {

            for (size_t i = 0; i < group.size(); ++i) {

                for (size_t j = i; j < group.size(); ++j) {

                    group_ploidy_path_indices.back().emplace_back(vector<uint32_t>({group.at(i), group.at(j)}));
                    group_ploidy_log_samplers.back().addOutcome(group_read_counts.cast<double>() * ((group_read_path_probs.col(i) + group_read_path_probs.col(j) + group_read_path_probs.col(group.size())).array()).log().matrix() + log(2));
                }
            }
        }
    }

    assert(group_ploidy_path_indices.size() == group_ploidy_log_samplers.size());

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

        sort(ploidy_path_indices.begin(), ploidy_path_indices.end());

        auto ploidy_path_indices_samples_it = ploidy_path_indices_samples.emplace(ploidy_path_indices, 0);
        ploidy_path_indices_samples_it.first->second++;
    }

    return ploidy_path_indices_samples;
}

unordered_map<vector<uint32_t>, uint32_t> NestedPathAbundanceEstimator::samplePloidyPathIndicesMHGibbs(const vector<PathInfo> & paths, const Eigen::ColMatrixXd & read_path_probs, const Eigen::ColVectorXd & noise_probs, const Eigen::RowVectorXui & read_counts) {

    auto path_groups = findPathOriginGroups(paths);

    uniform_real_distribution<double> uniform_dist(0, 1);

    vector<vector<uint32_t> > ploidy_path_indices_samples(num_nested_its);

    vector<uint32_t> all_accept_count;

    for (auto & group: path_groups) {

        Eigen::ColMatrixXd group_read_path_probs = Eigen::ColMatrixXd(read_path_probs.rows(), group.size());

        for (size_t i = 0; i < group.size(); ++i) {

            group_read_path_probs.col(i) = read_path_probs.col(group.at(i));
        }

        addNoiseAndNormalizeProbabilityMatrix(&group_read_path_probs, noise_probs);

        Eigen::RowVectorXui group_read_counts = read_counts;
        rowCollapseProbabilityMatrix(&group_read_path_probs, &group_read_counts);
        
        assert(group_read_path_probs.cols() == group.size() + 1);
        assert(group_read_counts.sum() == read_counts.sum());

        vector<double> group_proposal_probs;
        group_proposal_probs.reserve(group.size());

        double group_proposal_probs_log_sum = numeric_limits<double>::lowest();

        for (size_t i = 0; i < group.size(); ++i) {

            group_proposal_probs.emplace_back(group_read_counts.cast<double>() * (group_read_path_probs.col(i) + group_read_path_probs.col(group.size())).array().log().matrix());

            group_proposal_probs_log_sum = add_log(group_proposal_probs_log_sum, group_proposal_probs.back());
        }

        for (auto & log_probs: group_proposal_probs) {

            log_probs = exp(log_probs - group_proposal_probs_log_sum);
        }

        discrete_distribution<uint32_t> group_proposal_dist(group_proposal_probs.begin(), group_proposal_probs.end());

        vector<uint32_t> cur_sampled_group_paths;
        cur_sampled_group_paths.reserve(ploidy);

        // cerr << "\n" << group_proposal_probs << endl;

        auto cur_sampled_group_path_read_probs = group_read_path_probs.col(group.size());

        for (uint32_t i = 0; i < ploidy; ++i) {

            cur_sampled_group_paths.emplace_back(group_proposal_dist(mt_rng));
            cur_sampled_group_path_read_probs += group_read_path_probs.col(cur_sampled_group_paths.back());
        }     

        double cur_sampled_group_paths_prob = group_read_counts.cast<double>() * cur_sampled_group_path_read_probs.array().log().matrix();

        uint32_t accept_count = 0;
        uint32_t burn_in = 10;

        for (uint32_t i = 0; i < burn_in + num_nested_its; ++i) {

            for (uint32_t j = 0; j < ploidy; ++j) {

                auto next_sampled_group_paths = cur_sampled_group_paths;
                next_sampled_group_paths.at(j) = group_proposal_dist(mt_rng);

                Eigen::ColVectorXd new_sampled_group_path_read_probs = group_read_path_probs.col(group.size());

                for (auto & path_idx: next_sampled_group_paths) {

                    new_sampled_group_path_read_probs += group_read_path_probs.col(path_idx);
                }     

                double new_sampled_group_paths_prob = group_read_counts.cast<double>() * new_sampled_group_path_read_probs.array().log().matrix();
                double uniform_sample = uniform_dist(mt_rng);
                
                // cerr << cur_sampled_group_paths << ", " << next_sampled_group_paths << ", " << exp(new_sampled_group_paths_prob - cur_sampled_group_paths_prob + log(group_proposal_probs.at(cur_sampled_group_paths.at(j))) - log(group_proposal_probs.at(next_sampled_group_paths.at(j)))) << ", " << exp(new_sampled_group_paths_prob- cur_sampled_group_paths_prob) << ", " << uniform_sample << endl;

                if (log(uniform_dist(mt_rng)) < new_sampled_group_paths_prob - cur_sampled_group_paths_prob + log(group_proposal_probs.at(cur_sampled_group_paths.at(j))) - log(group_proposal_probs.at(next_sampled_group_paths.at(j)))) {

                    if (cur_sampled_group_paths != next_sampled_group_paths) {

                        accept_count++;
                    }

                    cur_sampled_group_paths = next_sampled_group_paths;
                    cur_sampled_group_paths_prob = new_sampled_group_paths_prob;
                }
            }

            if (i >= burn_in) {

                ploidy_path_indices_samples.at(i - burn_in).insert(ploidy_path_indices_samples.at(i - burn_in).end(), cur_sampled_group_paths.begin(), cur_sampled_group_paths.end());
            }
        }

        // cerr << accept_count << endl;
        all_accept_count.emplace_back(accept_count);
    }

    cerr << all_accept_count << endl;

    unordered_map<vector<uint32_t>, uint32_t> collapsed_ploidy_path_indices_samples;

    for (auto & path_samples: ploidy_path_indices_samples) {

        sort(path_samples.begin(), path_samples.end());

        auto collapsed_ploidy_path_indices_samples_it = collapsed_ploidy_path_indices_samples.emplace(path_samples, 0);
        collapsed_ploidy_path_indices_samples_it.first->second++;
    }

    return collapsed_ploidy_path_indices_samples;
}

void NestedPathAbundanceEstimator::constructPloidyProbabilityMatrix(Eigen::ColMatrixXd * ploidy_read_path_probs, const Eigen::ColMatrixXd & read_path_probs, const vector<uint32_t> & path_indices) const {

    *ploidy_read_path_probs = Eigen::ColMatrixXd(read_path_probs.rows(), path_indices.size());

    for (size_t i = 0; i < path_indices.size(); ++i) {

        ploidy_read_path_probs->col(i) = read_path_probs.col(path_indices.at(i));
    }
}

void NestedPathAbundanceEstimator::updateAbundances(Abundances * abundances, const Abundances & ploidy_abundances, const vector<uint32_t> & path_indices, const uint32_t sample_count) const {

   for (size_t i = 0; i < path_indices.size(); i += 2) {

        if (ploidy_abundances.confidence(i) > 0) {

            assert(doubleCompare(ploidy_abundances.confidence(i), 1));

            abundances->confidence(path_indices.at(i)) += (ploidy_abundances.confidence(i) * sample_count);
            abundances->expression(path_indices.at(i)) += (ploidy_abundances.expression(i) * sample_count);
        }
    }

    for (size_t i = 1; i < path_indices.size(); i += 2) {

        if (ploidy_abundances.confidence(i) > 0) {

            assert(doubleCompare(ploidy_abundances.confidence(i), 1));

            if (path_indices.at(i - 1) != path_indices.at(i)) {
                
                abundances->confidence(path_indices.at(i)) += (ploidy_abundances.confidence(i) * sample_count);
            }
            
            abundances->expression(path_indices.at(i)) += (ploidy_abundances.expression(i) * sample_count);
        }
    }

    assert(ploidy_abundances.confidence.cols() == path_indices.size() + 1);

    if (ploidy_abundances.confidence(path_indices.size()) > 0) {

        abundances->confidence(abundances->confidence.cols() - 1) += (ploidy_abundances.confidence(path_indices.size()) * sample_count);
        abundances->expression(abundances->expression.cols() - 1) += (ploidy_abundances.expression(path_indices.size()) * sample_count);  
    }
}
