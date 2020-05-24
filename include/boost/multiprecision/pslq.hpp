/*
 * Copyright Nick Thompson 2020
 * Use, modification and distribution are subject to the
 * Boost Software License, Version 1.0. (See accompanying file
 * LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 */

// Mathematica has an implementation of PSLQ which has the following interface:
// FindIntegerNullVector[{E, Pi}, 100000]
// FindIntegerNullVector::norel: There is no integer null vector for {E,\[Pi]} with norm less than or equal to 100000.
// Or:
// FindIntegerNullVector[{E, \[Pi]}]
// FindIntegerNullVector::rnfu: FindIntegerNullVector has not found an integer null vector for {E,\[Pi]}.
// I don't like this, because it should default to telling us the norm, as it's coproduced by the computation.

// Maple's Interface:
// with(IntegerRelations)
// v := [1.57079..., 1.4142135]
// u := PSLQ(v)
// u:= [-25474, 56589]
// Maple's interface is in fact worse, because it gives the wrong answer, instead of recognizing the precision provided.

// David Bailey's interface in tpslqm2.f90 in https://www.davidhbailey.com/dhbsoftware/  in  mpfun-fort-v19.tar.gz
// subroutine pslqm2(idb, n nwds, rb, eps, x, iq, r)
// idb is debug level
// n is the length of input vector and output relation r.
// nwds if the full precision level in words.
// rb is the log10 os max size Euclidean norm of relation
// eps tolerance for full precision relation detection.
// x input vector
// iq output flag: 0 (unsuccessful), 1 successful.
// r output integer relation vector, if successful.

#ifndef BOOST_MULTIPRECISION_PSLQ_HPP
#define BOOST_MULTIPRECISION_PSLQ_HPP
#include <iostream>
#include <stdexcept>
#include <vector>
#include <sstream>
#include <chrono>
#include <map>
#include <cmath>
#include <optional>
#include <boost/math/constants/constants.hpp>
#include <boost/math/special_functions/lambert_w.hpp>
#include <boost/math/tools/polynomial.hpp>
#include <boost/core/demangle.hpp>
#if defined __has_include
#  if __has_include (<Eigen/Dense>)
#    include <Eigen/Dense>
#  else
   #error This file has a dependency on Eigen; you must have Eigen (http://eigen.tuxfamily.org/index.php?title=Main_Page) in your include paths.
#  endif
#endif

#ifdef __APPLE__
#include <sys/ioctl.h> //ioctl() and TIOCGWINSZ
#include <unistd.h> // for STDOUT_FILENO
#endif

namespace boost::multiprecision {

// For debugging and unit testing:
template<typename Real>
auto small_pslq_dictionary() {
    using std::sqrt;
    using namespace boost::math::constants;
    std::map<Real, std::string> m;
    m.emplace(pi<Real>(), "π");
    m.emplace(e<Real>(), "e");
    m.emplace(root_two<Real>(), "√2");
    m.emplace(ln_two<Real>(), "ln(2)");
    return m;
}

template<typename Real>
auto standard_pslq_dictionary() {
    using std::sqrt;
    using std::log;
    using std::exp;
    using std::pow;
    using std::cbrt;
    using namespace boost::math::constants;
    std::map<Real, std::string> m;
    Real euler_ = euler<Real>();
    m.emplace(euler_, "γ");
    m.emplace(euler_*euler_, "γ²");
    m.emplace(euler_*euler_*euler_, "γ³");
    m.emplace(1/euler_, "1/γ");
    m.emplace(1/(euler_*euler_), "1/γ²");
    m.emplace(1/(euler_*euler_*euler_), "1/γ³");
    m.emplace(-log(euler_), "-ln(γ)");
    m.emplace(exp(euler_), "exp(γ)");
    Real zeta_three_ = zeta_three<Real>();
    m.emplace(sqrt(zeta_three_), "√ζ(3)");
    m.emplace(zeta_three_, "ζ(3)");
    m.emplace(1/zeta_three_, "1/ζ(3)");
    m.emplace(1/(zeta_three_*zeta_three_), "1/ζ(3)²");
    m.emplace(1/(zeta_three_*zeta_three_*zeta_three_), "1/ζ(3)³");
    m.emplace(log(zeta_three_), "ln(ζ(3))");
    m.emplace(exp(zeta_three_), "exp(ζ(3))");
    m.emplace(zeta_three_*zeta_three_, "ζ(3)²");
    m.emplace(zeta_three_*zeta_three_*zeta_three_, "ζ(3)³");
    m.emplace(pow(zeta_three_, 4), "ζ(3)⁴");
    
    auto pi_ = pi<Real>();
    m.emplace(pi_, "π");
    m.emplace(1/pi_, "1/π");
    m.emplace(1/(pi_*pi_), "1/π²");
    m.emplace(sqrt(pi_), "√π");
    m.emplace(cbrt(pi_), "∛π");
    m.emplace(log(pi_), "ln(π)");
    m.emplace(pi_*pi_, "π²");
    m.emplace(pi_*pi_*pi_, "π³");

    Real e_ = e<Real>();
    m.emplace(e_, "e");
    m.emplace(sqrt(e_), "√e");
    m.emplace(root_two<Real>(), "√2");
    m.emplace(root_three<Real>(), "√3");
    m.emplace(sqrt(static_cast<Real>(5)), "√5");
    m.emplace(sqrt(static_cast<Real>(7)), "√7");
    m.emplace(sqrt(static_cast<Real>(11)), "√11");

    // φ is linearly dependent on √5; its logarithm is not.
    m.emplace(log(phi<Real>()), "ln(φ)");
    m.emplace(exp(phi<Real>()), "exp(φ)");
    Real catalan_ = catalan<Real>();
    m.emplace(catalan_, "G");
    m.emplace(catalan_*catalan_, "G²");
    m.emplace(1/catalan_, "1/G");
    m.emplace(-log(catalan_), "-ln(G)");
    m.emplace(exp(catalan_), "exp(G)");
    m.emplace(sqrt(catalan_), "√G");

    Real glaisher_ = glaisher<Real>();
    m.emplace(glaisher_, "A");
    m.emplace(glaisher_*glaisher_, "A²");
    m.emplace(1/glaisher_, "1/A");
    m.emplace(log(glaisher_), "ln(A)");
    m.emplace(exp(glaisher_), "exp(A)");
    Real khinchin_ = khinchin<Real>();
    m.emplace(khinchin_, "K₀");
    m.emplace(log(khinchin_), "ln(K₀)");
    m.emplace(exp(khinchin_), "exp(K₀)");
    m.emplace(1/khinchin_, "1/K₀");
    m.emplace(khinchin_*khinchin_, "K₀²");
    // To recover multiplicative relations we need the logarithms of small primes.
    m.emplace(log(static_cast<Real>(2)), "ln(2)");
    m.emplace(-log(log(static_cast<Real>(2))), "-ln(ln(2))");
    m.emplace(log(static_cast<Real>(3)), "ln(3)");
    m.emplace(log(static_cast<Real>(5)), "ln(5)");
    m.emplace(log(static_cast<Real>(7)), "ln(7)");
    m.emplace(log(static_cast<Real>(11)), "ln(11)");
    m.emplace(log(static_cast<Real>(13)), "ln(13)");
    m.emplace(log(static_cast<Real>(17)), "ln(17)");
    m.emplace(log(static_cast<Real>(19)), "ln(19)");
    // Omega constant = Lambert-W function evaluated at 1:
    Real Omega_ = boost::math::lambert_w0(static_cast<Real>(1));
    m.emplace(Omega_, "Ω");
    m.emplace(Omega_*Omega_, "Ω²");
    m.emplace(1/Omega_, "1/Ω");
    return m;
}

// anonymous namespace:
namespace {

class progress {

public:

progress()
{
    start_ = std::chrono::steady_clock::now();
}

void display_progress(double progress)
{
    struct winsize size_;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &size_);
    int barWidth = size_.ws_col;

    std::cout << "\033[0;32m[";
    int pos = barWidth * progress;
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] "
              << int(progress * 100.0)
              << "%\r";
    std::cout.flush();
}

private:

    std::chrono::steady_clock::time_point start_;
};

}

// The PSLQ algorithm; partial sum of squares, lower trapezoidal decomposition.
// See: https://www.davidhbailey.com/dhbpapers/cpslq.pdf, section 3.
template<typename Real>
std::vector<std::pair<int64_t, Real>> pslq(std::vector<Real> & x, Real max_acceptable_norm_bound, Real gamma) {
    std::vector<std::pair<int64_t, Real>> relation;
    if (!std::is_sorted(x.begin(), x.end())) {
        std::cerr << "Elements must be sorted in increasing order.\n";
        return relation;
    }
    using std::sqrt;
    if (gamma <= 2/sqrt(3)) {
        std::cerr << "γ > 2/√3 is required\n";
        return relation;
    }
    Real tmp = 1/Real(4) + 1/(gamma*gamma);
    Real tau = 1/sqrt(tmp);
    if (tau <= 1 || tau >= 2) {
        std::cerr << "τ ∈ (1, 2) is required.\n";
        return relation;
    }

    if (x.size() < 2) {
        std::cerr << "At least two values are required to find an integer relation.\n";
        return relation;
    }

    for (auto & t : x) {
        if (t == 0) {
            std::cerr << "Zero in the dictionary gives trivial relations.\n";
            return relation;
        }
        if (t < 0) {
            std::cerr << "The algorithm is reflection invariant, so negative values in the dictionary should be removed.\n";
            return relation;
        }
    }

    // Are we computing too many square roots??? Should we use s instead?
    std::vector<Real> s_sq(x.size());
    s_sq.back() = x.back()*x.back();
    int64_t n = x.size();
    for (int64_t i = n - 2; i >= 0; --i) {
        s_sq[i] = s_sq[i+1] + x[i]*x[i];
    }
    
    using std::pow;
    if (max_acceptable_norm_bound*max_acceptable_norm_bound*s_sq[0] > 1/std::numeric_limits<Real>::epsilon()) {
        std::cerr << "The maximum acceptable norm bound " << max_acceptable_norm_bound << " is too large; spurious relations will be recovered.\n";
        std::cerr << "Either reduce the norm bound, or increase the precision of the variables.\n";
        std::cerr << "At this precision, your norm bound cannot exceed " << 1/sqrt(s_sq[0]*std::numeric_limits<Real>::epsilon()) << "\n";
        return relation;
    }
    Eigen::Matrix<Real, Eigen::Dynamic, Eigen::Dynamic> H(n, n-1);
    for (int64_t i = 0; i < n - 1; ++i) {
        for (int64_t j = 0; j < n - 1; ++j) {
            if (i < j) {
                H(i,j) = 0;
            }
            else if (i == j) {
                H(i,i) = sqrt(s_sq[i+1]/s_sq[i]);
            }
            else {
                // i > j:
                H(i,j) = -x[i]*x[j]/sqrt(s_sq[j]*s_sq[j+1]);
            }
        }
    }
    for (int64_t j = 0; j < n - 1; ++j) {
        H(n-1, j) = -x[n-1]*x[j]/sqrt(s_sq[j]*s_sq[j+1]);
    }

    using std::ceil;
    int64_t expected_iterations = ceil(boost::math::binomial_coefficient<Real>(n, 2)*log(pow(gamma, n-1)*max_acceptable_norm_bound)/log(tau));
    std::cout << "Expected number of iterations = " << expected_iterations << "\n";
    // This validates that H is indeed lower trapezoidal,
    // but that's trival and verbose:
    //std::cout << "H = \n";
    //std::cout << H << "\n";

    // Validate the conditions of Lemma 1 in the referenced paper:
    // These tests should eventually be removed once we're confident that the code is correct.
    auto Hnorm_sq = H.squaredNorm();
    if (abs(Hnorm_sq/(n-1) - 1) > sqrt(std::numeric_limits<Real>::epsilon())) {
        std::cerr << "‖Hₓ‖² ≠ n - 1. Hence Lemma 1.ii of the reference has numerically failed; this is a bug.\n";
        return relation;
    }

    // Notations now follows https://www.davidhbailey.com/dhbpapers/pslq-cse.pdf
    Eigen::Matrix<Real, Eigen::Dynamic, 1> y(x.size());
    for (int64_t i = 0; i < n; ++i) {
        y[i] = x[i]/sqrt(s_sq[0]);
    }

    // Now check y:
    for (int64_t i = 0; i < n; ++i){
        if (abs(y[i]) < std::numeric_limits<Real>::epsilon()) {
            std::cerr << "Element y[" << i << "] = " << y[i] << " is too small; more precision is required.\n";
            return relation;
        }
    }
    for (int64_t i = 1; i < n; ++i){
        // using the sorted assumption here:
        if (abs(boost::math::float_distance(y[i], y[i-1])) <= 2) {
            std::cerr << "Elements y[" << i << "] = " << y[i] << " and " << y << "[" << i + 1 << "] = " << y[i+1] << " are too close together.\n";
            return relation;
        }
    }

    auto v = y.transpose()*H;
    for (int64_t i = 0; i < n - 1; ++i) {
        if (abs(v[i])/(n-1) > sqrt(std::numeric_limits<Real>::epsilon())) {
            std::cerr << "xᵀHₓ ≠ 0; Lemma 1.iii of the reference cpslq has numerically failed; this is a bug.\n";
            return relation;
        }
    }
    using std::round;

    // Initialize:
    // "1. Set the nxn matrices A and B to the identity."
    Eigen::Matrix<int64_t, Eigen::Dynamic, Eigen::Dynamic> A = Eigen::Matrix<int64_t, Eigen::Dynamic, Eigen::Dynamic>::Identity(n, n);
    Eigen::Matrix<int64_t, Eigen::Dynamic, Eigen::Dynamic> B = Eigen::Matrix<int64_t, Eigen::Dynamic, Eigen::Dynamic>::Identity(n, n);
    for (int64_t i = 1; i < n; ++i) {
        for (int64_t j = i - 1; j >= 0; --j) {
            Real t = round(H(i,j)/H(j,j));
            int64_t tint = static_cast<int64_t>(t);
            // This happens a lot because x_0 < x_1 < ...!
            // Sort them in decreasing order and it almost never happens.
            if (tint == 0)
            {
                continue;
            }
            for (int64_t k = 0; k <= j; ++k)
            {
                H(i,k) = H(i,k) - t*H(j,k);
            }
            for (int64_t k = 0; k < n; ++k) {
                A(i,k) = A(i,k) - tint*A(j,k);
                B(k,j) = B(k,j) + tint*B(k,i);
            }
            y[j] += t*y[i];
        }
    }
    /*std::cout << "A, post-reduction = \n" << A << "\n";
    std::cout << "B, post-reduction = \n" << B << "\n";
    std::cout << "A*B, post-reduction = \n" << A*B << "\n";
    std::cout << "H, post-reduction:\n" << H << "\n";

    std::cout << "Hit enter.\n";
    std::cin.get();*/
    Real max_coeff = 0;
    for (int64_t i = 0; i < n - 1; ++i) {
        if (abs(H(i,i)) > max_coeff) {
            max_coeff = abs(H(i,i));
        }
    }
    Real norm_bound = 1/max_coeff;
   // Real max_acceptable_norm_bound = 1e10;
    //int64_t iteration = 0;
    Real last_norm_bound = norm_bound;
    int64_t iteration = 0;
    while (norm_bound < max_acceptable_norm_bound)
    {
        // "1. Select m such that γ^{i+1}|H_ii| is maximal when i = m":
        // (note my C indexing translated from DHB's Fortran indexing)
        Real gammai = gamma;
        Real max_term = 0;
        int64_t m = -1;
        for (int i = 0; i < n - 1; ++i) {
            Real term = gammai*abs(H(i,i));
            if (term > max_term) {
                max_term = term;
                m = i;
            }
            gammai *= gamma;
        }
        // "2. Exchange the entries of y indexed m and m + 1"
        if (m == n - 1) {
            std::cerr << "OMG: m = n- 1, swap gonna segfault.\n";
            return relation;
        }
        if (m < 0) {
            std::cerr << "OMG: m = - 1, swap gonna segfault.\n";
            return relation;
        }
        std::swap(y[m], y[m+1]);
        // Swap the corresponding rows of A and H:
        A.row(m).swap(A.row(m+1));
        H.row(m).swap(H.row(m+1));
        // Swap the corresponding columns of B:
        B.col(m).swap(B.col(m+1));

        //std::cout << "H, post-swap = \n" << H << "\n";
        // "3. Remove the corner on H diagonal:"
        //std::cout << "Removing corner:\n";
        if (m < n - 2) {
            Real t0 = H(m,m)*H(m,m) + H(m, m+1)*H(m, m+1);
            t0 = sqrt(t0);
            Real t1 = H(m,m)/t0;
            Real t2 = H(m,m+1)/t0;
            for (int64_t i = m; i < n; ++i) {
                Real t3 = H(i,m);
                Real t4 = H(i, m+1);
                H(i,m) = t1*t3 + t2*t4;
                H(i,m+1) = -t2*t3 + t1*t4;
            }
            //std::cout << "H, post corner reduce = \n" << H << "\n";
        }
        else {
            //std::cout << "m = " << m << ", so no corner reduction.\n";
        }

        //std::cin.get();
        // "4. Reduce H:"
        //std::cout << "Reducing H:\n";
        for (int64_t i = m+1; i < n; ++i) {
            for (int64_t j = std::min(i-1, m+1); j >= 0; --j) {
                Real t = round(H(i,j)/H(j,j));
                int64_t tint = static_cast<int64_t>(t);
                if (tint == 0)
                {
                    continue;
                }
                //std::cout << "Update y\n";
                y[j] += t*y[i];
                //std::cout << "Updating H\n";
                for (int64_t k = 0; k <= j; ++k) {
                    H(i,k) = H(i,k) - t*H(j,k);
                }
                //std::cout << "Updating A,B:\n";
                for (int64_t k = 0; k < n; ++k) {
                    A(i,k) = A(i,k) - tint*A(j,k);
                    B(k,j) = B(k,j) + tint*B(k,i);
                }
            }
        }

        //std::cout << "Looking for a solution\n";
        // Look for a solution:
        for (int64_t i = 0; i < n; ++i) {
            if (abs(y[i]) < pow(std::numeric_limits<Real>::epsilon(), Real(15)/Real(16))) {
                //std::cout << "We've found a solution!\n";
                auto bcol = B.col(i);
                //std::cout << "Column of B = \n" << bcol << "\n";
                Real residual = 0;
                Real absum = 0;
                for (int64_t j = 0; j < n; ++j) {
                    residual += bcol[j]*x[j];
                    absum += abs(bcol[j]*x[j]);
                }
                Real tolerable_residual = 16*std::numeric_limits<Real>::epsilon()*absum;
                if (abs(residual) > tolerable_residual)
                {
                    std::cerr << __FILE__ << ":" << __LINE__ << "\n\tBug in PSLQ: Found a relation with a large residual.\n";
                    std::cerr << "\tThe residual " << abs(residual) << " is larger than the tolerable residual " << tolerable_residual << "\n";
                    std::cerr << "\tThis is either a bug, or your constants are not specified to the full accuracy of the floating point type " << boost::core::demangle(typeid(Real).name()) << ".\n";
                }

                for (int64_t j = 0; j < n; ++j)
                {
                    if (bcol[j] != 0)
                    {
                        relation.push_back({bcol[j], x[j]});
                    }
                }
                std::cout << std::endl;
                return relation;
            }
        }

       // std::cout << "Computing the norm bound:\n";
        max_coeff = 0;
        for (int64_t i = 0; i < n - 1; ++i) {
            if (abs(H(i,i)) > max_coeff) {
                max_coeff = abs(H(i,i));
            }
        }
        norm_bound = 1/max_coeff;
        //std::cout << "H = \n" << H << "\n";
        //std::cout << "A = \n" << A << "\n";
        //std::cout << "B = \n" << B << "\n";

        //std::cout << "A*B = \n" << A*B << "\n";
        //std::cout << "y = \n" << y << "\n";
        std::cout << "Norm bound = " << norm_bound  << "/" << max_acceptable_norm_bound << ", iteration " << iteration << "/" << expected_iterations << "\r";
        if (norm_bound < last_norm_bound) {
            std::cerr << "Norm bound has decreased!\n";
        }
        last_norm_bound = norm_bound;
        ++iteration;

    }
    std::cout << std::endl;
    return relation;
}


template<typename Real>
std::string pslq(std::map<Real, std::string> const & dictionary, Real max_acceptable_norm_bound, Real gamma) {
    std::vector<Real> values(dictionary.size());
    size_t i = 0;
    for (auto it = dictionary.begin(); it != dictionary.end(); ++it) {
        values[i++] = it->first;
    }

    auto m = pslq(values, max_acceptable_norm_bound, gamma);
    if (m.size() > 0) {
        std::ostringstream oss;
        auto const & symbol = dictionary.find(m[0].second)->second;
        oss << "As\n\t";
        Real sum = m[0].first*m[0].second;
        oss << m[0].first << "⋅" << m[0].second;
        for (size_t i = 1; i < m.size(); ++i)
        {
            if (m[i].first < 0) {
                oss << " - ";
            } else {
                oss << " + ";
            }
            oss << abs(m[i].first) << "⋅" << m[i].second;
            sum += m[i].first*m[i].second;
        }
        oss << " = " << sum << ",\nit is likely that\n\t";

        oss << m[0].first << "⋅" << symbol;
        for (size_t i = 1; i < m.size(); ++i)
        {
            if (m[i].first < 0) {
                oss << " - ";
            } else {
                oss << " + ";
            }
            auto const & symbol = dictionary.find(m[i].second)->second;
            oss << abs(m[i].first) << "⋅" << symbol;
        }
        oss << " = 0.";

        return oss.str();
    }
    return "";
}

template<typename Real>
std::string pslq(std::map<Real, std::string> const & dictionary, Real max_acceptable_norm) {
    Real gamma = 2/sqrt(3) + 0.01;
    return pslq(dictionary, max_acceptable_norm, gamma);
}

template<typename Real>
std::string identify(std::pair<Real, std::string> value_symbol, Real max_acceptable_norm) {
    auto dictionary = standard_pslq_dictionary<Real>();
    Real gamma = 2/sqrt(3) + 0.01;

    using std::log;
    using std::exp;
    dictionary.emplace(value_symbol.first, value_symbol.second);
    /*Real log_ = log(value_symbol.first);
    if (log_ < 0) {
        dictionary.emplace(-log(value_symbol.first), "-ln(" + value_symbol.second + ")");
    } else {
        dictionary.emplace(log(value_symbol.first), "ln(" + value_symbol.second + ")");
    }*/
    dictionary.emplace(exp(value_symbol.first), "exp(" + value_symbol.second + ")");
    dictionary.emplace(1/value_symbol.first, "1/" + value_symbol.second);
    dictionary.emplace(value_symbol.first*value_symbol.first, value_symbol.second + "²");
    return pslq(dictionary, max_acceptable_norm, gamma);
}

template<typename Real>
std::string is_algebraic(std::pair<Real, std::string> x, Real max_acceptable_norm) {
    // TODO: Figure out this interface.
    return "";
}

}
#endif