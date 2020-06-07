
#ifndef RPVG_SRC_PATHCLUSTERESTIMATES_HPP
#define RPVG_SRC_PATHCLUSTERESTIMATES_HPP

#include <vector>

#include <Eigen/Dense>

#include "utils.hpp"

using namespace std;


struct PathInfo {
        
    string name;
    string origin;
    uint32_t length;
    double effective_length;
    
    PathInfo() {

        name = "";
        origin = "";
        length = 0;
        effective_length = 0;
    }
};

struct PathClusterEstimates {

    vector<PathInfo> paths;

    Eigen::RowVectorXd posteriors;
    Eigen::RowVectorXd abundances;

    uint32_t read_count;

    vector<vector<uint32_t> > path_groups;

    void generateGroupsRecursive(const uint32_t num_components, const uint32_t group_size, vector<uint32_t> cur_group) {

        assert(cur_group.size() <= group_size);

        if (cur_group.size() < group_size) {

            for (uint32_t i = 0; i < num_components; ++i) {

                vector<uint32_t> new_group = cur_group;
                new_group.push_back(i);
                generateGroupsRecursive(num_components, group_size, new_group);
            }

        } else {

            path_groups.emplace_back(cur_group);
        }
    }

    void initEstimates(uint32_t num_components, const uint32_t group_size, const bool init_zero) {

        if (group_size > 0) {

            generateGroupsRecursive(num_components, group_size, vector<uint>());
            num_components = path_groups.size();
        }

        if (init_zero) {

            posteriors = Eigen::RowVectorXd::Zero(1, num_components);
            abundances = Eigen::RowVectorXd::Zero(1, num_components);

        } else {

            posteriors = Eigen::RowVectorXd::Constant(num_components, 1);
            abundances = Eigen::RowVectorXd::Constant(num_components, 1 / static_cast<float>(num_components));
        }

        read_count = 0;
    }
};


#endif
