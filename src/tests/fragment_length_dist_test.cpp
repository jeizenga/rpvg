
#include "catch.hpp"

#include "../fragment_length_dist.hpp"
#include "../utils.hpp"


TEST_CASE("FragmentLengthDist is valid normal distribution") {
    
	FragmentLengthDist fragment_length_dist(10, 2);

    REQUIRE(fragment_length_dist.isValid());	
    REQUIRE(fragment_length_dist.maxLength() == 20);

    REQUIRE(Utils::doubleCompare(fragment_length_dist.logProb(9), -1.737085713764618));
    REQUIRE(Utils::doubleCompare(fragment_length_dist.logProb(15), -4.737085713764618));
    REQUIRE(Utils::doubleCompare(fragment_length_dist.logProb(9), fragment_length_dist.logProb(11)));
    REQUIRE(Utils::doubleCompare(fragment_length_dist.logProb(10000), -12475014.11208571307361));

}

TEST_CASE("FragmentLengthDist is valid skew normal distribution") {
    
    SECTION("Passes checks for internal consistency") {
        for (int mu = 0; mu <= 3; ++mu) {
            for (int sigma = 1; sigma <= 3; ++sigma) {
                for (int alpha = -3; alpha <= -3; ++alpha) {
                    FragmentLengthDist distr(mu, sigma, alpha);
                    for (int x = 0; x <= 3; ++x) {
                        if (alpha == 0) {
                            FragmentLengthDist norm(mu, sigma);
                            REQUIRE(Utils::doubleCompare(distr.logProb(x), norm.logProb(x)));
                        }
                        {
                            FragmentLengthDist other(x, sigma, -alpha);
                            REQUIRE(Utils::doubleCompare(distr.logProb(x), other.logProb(mu)));
                        }
                        {
                            // reflect the test point around mu and reverse the skew
                            int reflected = 2 * mu - x;
                            if (reflected >= 0) {
                                FragmentLengthDist other(mu, sigma, -alpha);
                                REQUIRE(Utils::doubleCompare(distr.logProb(x), other.logProb(reflected)));
                            }
                        }
                    }
                }
            }
        }
    }
    
    SECTION("Skew normal CDF produces correct results") {
        
        // random values computed using scipy.stats.skewnorm.cdf
        vector<tuple<double, double, double, double, double>> tests {
            {-1.377795671730496, -5.735988598231357, 6.587971754854138, 0.6242981711089186, 0.6067478509468889},
            {8.453381421131361, -6.737025018438891, 1.4128537861467216, -4.714759782925793, 0.9999999999999998},
            {-4.284513142216991, 0.8224611090246263, 4.868647362416242, 5.3775135785150034, 1.175664910419217e-10},
            {2.16127255846893, 3.2203575525564876, 7.543478189625029, -5.754430631695544, 0.8723557627210784},
            {-5.053036192675702, 5.874484839242527, 3.087679949836093, 4.419799676586766, 3.913067350708402e-60},
            {-3.631256826447924, 5.747026610035844, 9.44738425166303, 8.44831439784933, 1.654214272212821e-19},
            {1.8905313320417108, -1.927426833597579, 5.426906826220844, -1.322299136270484, 0.9666362215848003},
            {3.958586584693551, 1.453790486627451, 2.099665856851638, 2.513179002770114, 0.7671637348910078},
            {-7.97460050170157, -6.41790664548763, 2.8240014248393672, -0.040594102097193385, 0.30182880496547393},
            {9.540970160583104, -0.5606080847259811, 3.173017874188342, 4.057194595502764, 0.9985453757376268}
        };
        
        for (auto test : tests) {
            double x, m, s, a, r;
            tie(x, m, s, a, r) = test;
            REQUIRE(abs(Utils::skew_normal_cdf(x, m, s, a) - r) < 1e-6);
        }
    }
    
    SECTION("Truncated skew normal expected value produces correct results") {
        
        // random values computed using a Python implementation that checks out against truncated samples
        // from scipy.stats.skewnorm.rvs
        vector<tuple<double, double, double, double, double, double>> tests {
            {8.787459714627083, 6.868385554340576, -6.94929706130925, -0.37768056364280866, 31.723919183225018, 4.906268559966877},
            {-8.113569780964331, 5.839950201823468, 9.760396217870774, -7.89737335945399, 4.022749040637173, -3.6139469797079213},
            {7.141245637134961, 4.559985715921152, -4.823210556537941, 7.1865576184156446, 13.477227885455914, 7.758960573009493},
            {-7.6539988922504065, 1.7245352631865019, -8.919526791950119, -13.427923801308836, -13.056269996875283, -13.220675399104401},
            {-6.503575582874122, 2.4834075337660866, 8.665672873756801, -3.2347381900362233, 2.9721020130760643, -2.077522240946859},
            {-8.028800089506266, 3.7263335366543138, 3.576676117054836, -11.709946267256408, -5.8524562530204705, -7.215706684550359},
            {5.761132893708735, 9.955360196746412, -7.666141221239958, 3.9372946379953646, 39.03638724596176, 5.264304349707415},
            {-1.7809221540831732, 4.009154518247604, -7.6115564591837215, -10.289600103156221, -8.956624105150768, -9.551633998098026},
            {4.314469611446182, 3.4274638460977336, 2.2210239246182173, -9.192544467474805, 16.537245753118285, 6.804259667846736},
            {-9.841876537956933, 5.863996182747239, -7.284944646833256, -22.473871311547153, 6.850952329036673, -14.152091163539863},
        };
        
        for (auto test : tests) {
            double m, s, a, c, d, r;
            tie(m, s, a, c, d, r) = test;
            REQUIRE(abs(Utils::truncated_skew_normal_expected_value(m, s, a, c, d) - r) < 1e-6);
        }
    }
    
    SECTION("Skew normal fitting algorithm finds MLE") {
        
        // lengths generated from scipy.stats.skewnorm.rvs(a = 10, loc = 50, scale = 10)
        vector<uint32_t> length_counts{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 4, 22, 53, 144, 256, 394, 522, 647, 700, 744, 693, 673, 667, 573, 502, 454, 417, 380, 330, 299, 274, 225, 185, 181, 153, 115, 78, 77, 54, 43, 33, 27, 22, 17, 9, 7, 7, 4, 1, 6, 1, 2, 0, 1, 1, 1};
        
        // fit a skew normal
        FragmentLengthDist dist(length_counts, true);
        
        // correct MLE values determined by my Python script, which appears to check out against
        // simulated data
        REQUIRE(abs(dist.loc() - 50.996133408667475) < 1e-3);
        REQUIRE(abs(dist.scale() - 10.035973814767827) < 1e-3);
        REQUIRE(abs(dist.shape() - 4.7885824148015015) < 1e-3);
    }
}

TEST_CASE("Fragment length distribution parameters can be parsed from vg::Alignment") {
    
    FragmentLengthDist fragment_length_dist;

    SECTION("Missing fragment length distribution is not parsed") {

	    const string alignment_str = R"(
	    	{
	    		"sequence":"ACGT"
	    	}
	    )";

	    vg::Alignment alignment;
	    Utils::json2pb(alignment, alignment_str);

	    REQUIRE(!fragment_length_dist.parseAlignment(alignment));	
	}

    SECTION("Empty fragment length distribution is not parsed") {

	    const string alignment_str = R"(
	    	{
	    		"fragment_length_distribution":"0:0:0:0:1"
	    	}
	    )";

	    vg::Alignment alignment;
	    Utils::json2pb(alignment, alignment_str);

	    REQUIRE(!fragment_length_dist.parseAlignment(alignment));	
	}

    SECTION("Fragment length distribution parameters are parsed") {

	    const string alignment_str = R"(
	    	{
	    		"fragment_length_distribution":"100:10:2:0:1"
	    	}
	    )";

	    vg::Alignment alignment;
	    Utils::json2pb(alignment, alignment_str);

	    REQUIRE(fragment_length_dist.parseAlignment(alignment));
	    REQUIRE(Utils::doubleCompare(fragment_length_dist.loc(), 10));
	    REQUIRE(Utils::doubleCompare(fragment_length_dist.scale(), 2));
        REQUIRE(Utils::doubleCompare(fragment_length_dist.shape(), 0.0));
	}
}

TEST_CASE("Fragment length distribution parameters can be parsed from vg::MultipathAlignment") {
    
    FragmentLengthDist fragment_length_dist;

    SECTION("Missing fragment length distribution is not parsed") {

	   	const string alignment_str = R"(
	   		{
	   			"sequence":"ACGT"
	   		}
	    )";

	    vg::MultipathAlignment alignment;
	    Utils::json2pb(alignment, alignment_str);

	    REQUIRE(!fragment_length_dist.parseMultipathAlignment(alignment));	
	}

    SECTION("Fragment length distribution parameters are parsed") {

	    const string alignment_str = R"(
	    	{
	    		"annotation": {"fragment_length_distribution":"-I 10 -D 2"}
	    	}
	    )";

	    vg::MultipathAlignment alignment;
	    Utils::json2pb(alignment, alignment_str);

	    REQUIRE(fragment_length_dist.parseMultipathAlignment(alignment));
	    REQUIRE(Utils::doubleCompare(fragment_length_dist.loc(), 10));
	    REQUIRE(Utils::doubleCompare(fragment_length_dist.scale(), 2));
        REQUIRE(Utils::doubleCompare(fragment_length_dist.shape(), 0.0));
	}
}

