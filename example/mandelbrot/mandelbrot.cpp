///////////////////////////////////////////////////////////////////////////////
//      Copyright Christopher Kormanyos 2015 - 2017, 2020.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
//

// This example uses Boost.Multiprecision to implement
// a high-precision Mandelbrot iteration and visualization.
// Graphic file creation uses Boost.Gil (old) to wrap JPEG.
// Color-strething in combination with the histogram method
// is used for creating vivid images. The default color
// scheme uses stretched, amplified and modulated black
// and white coloring.

// A well-known example of fractal is the Mandelbrot set,
// which is based upon the function z_{n+1} = z_{n}^2 + c.
// A common way of coloring Mandelbrot images is by taking
// the number of iterations required to reach non-bounded
// divergence from c and then assigning that value a color.
// This is called the escape time algorithm.

// The Mandelbrot set consists of those points c in the
// complex plane for which the iteration
//   z_{n+1} = z_{n}^2 + c with z_{0} = 0
// stays bounded.
// Interesting points could be points for which we have an orbit.
// An orbit of length n is a sequence of z_{n} with
//   z_{1} = c, z_{2}, ..., z{n},
// such that z_{n} = z_{1} and z_{n} != z_{k} with (1 < k < n).
// In order to find these, numerical methods are needed.
// The equation z_{n} = z_{1} can only be solved in closed form
// by hand for small n. A point c of order n will also show up
// as a point of order n_{m}, for some m > 1. Mark these points
// in your set.

// Any point that is inside the Mandelbrot set and close to the
// boundary between the set and its complement as well as any point
// outside the Mandelbrot set that is close to this boundary is an
// interesting point. The closer you are to the boundary, the more
// you need to zoom in to see the interesting parts. In particular,
// all points on the x-axis between -2 and 1/4 are in the Mandelbrot set.
// Especially close to x = -2 (from the right), the point (x, 0)
// is arbitrarily close to the boundary. So try the point (eps - 2, 0)
// for a small (eps > 0). Some Mandelbrot softwares use a strategy that
// zooms in, continually trying to find a point close to the boundary
// while zooming, and uses that as the zoom point.

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <ctime>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

#include <boost/gil/extension/io/jpeg/old.hpp>
#include <boost/gil/image.hpp>
#include <boost/gil/typedefs.hpp>
#include <boost/lexical_cast.hpp>

#include <boost/multiprecision/cpp_dec_float.hpp>

namespace boost { namespace multiprecision { namespace mandelbrot {

namespace detail {

namespace my_concurrency {
template<typename index_type,
          typename callable_function_type>
void parallel_for(index_type             start,
                  index_type             end,
                  callable_function_type parallel_function)
{
  // Estimate the number of threads available.
  static const unsigned int number_of_threads_hint =
    std::thread::hardware_concurrency();

  static const unsigned int number_of_threads_total =
    ((number_of_threads_hint == 0U) ? 4U : number_of_threads_hint);

  // Use only 3/4 of the available cores.
  static const unsigned int number_of_threads = number_of_threads_total - (number_of_threads_total / 4U);

  // Set the size of a slice for the range functions.
  index_type n = index_type(end - start) + index_type(1);

  index_type slice =
    static_cast<index_type>(std::round(n / static_cast<float>(number_of_threads)));

  slice = (std::max)(slice, index_type(1));

  // Inner loop.
  auto launch_range =
    [&parallel_function](index_type index_lo, index_type index_hi)
    {
      for(index_type i = index_lo; i < index_hi; ++i)
      {
        parallel_function(i);
      }
    };

  // Create the thread pool and launch the jobs.
  std::vector<std::thread> pool;

  pool.reserve(number_of_threads);

  index_type i1 = start;
  index_type i2 = (std::min)(index_type(start + slice), end);

  for(index_type i = 0U; ((index_type(i + index_type(1U)) < number_of_threads) && (i1 < end)); ++i)
  {
    pool.emplace_back(launch_range, i1, i2);

    i1 = i2;

    i2 = (std::min)(index_type(i2 + slice), end);
  }

  if(i1 < end)
  {
    pool.emplace_back(launch_range, i1, end);
  }

  // Wait for the jobs to finish.
  for(std::thread& thread_in_pool : pool)
  {
    if(thread_in_pool.joinable())
    {
      thread_in_pool.join();
    }
  }
}
} // namespace my_concurrency

class color_functions_base
{
public:
  virtual ~color_functions_base() = default;

  virtual std::uint_fast32_t color_function_r(const std::uint_fast32_t&) const = 0;
  virtual std::uint_fast32_t color_function_g(const std::uint_fast32_t&) const = 0;
  virtual std::uint_fast32_t color_function_b(const std::uint_fast32_t&) const = 0;

protected:
  color_functions_base() = default;

  static std::uint_fast32_t color_phaser_01(const std::uint_fast32_t& c)
  {
    const float color_phase = (float(c) / 255.0F) * (3.1415926535897932385F * 8.0F);

    const float my_color = (std::sin(color_phase) / 2.0F) + 0.5F;

    return static_cast<std::uint_fast32_t>(my_color * 255.0F);
  }
};

class color_functions_bw final : public color_functions_base
{
public:
  color_functions_bw() = default;

  virtual ~color_functions_bw() = default;

private:
  virtual std::uint_fast32_t color_function_r(const std::uint_fast32_t& c) const { return color_phaser_01(c); }
  virtual std::uint_fast32_t color_function_g(const std::uint_fast32_t& c) const { return color_phaser_01(c); }
  virtual std::uint_fast32_t color_function_b(const std::uint_fast32_t& c) const { return color_phaser_01(c); }
};

class color_functions_pretty final : public color_functions_base
{
public:
  color_functions_pretty() = default;

  virtual ~color_functions_pretty() = default;

private:
  virtual std::uint_fast32_t color_function_r(const std::uint_fast32_t& c) const
  {
    return color_phaser_01(c);
  }

  virtual std::uint_fast32_t color_function_g(const std::uint_fast32_t& c) const
  {
    return c;
  }

  virtual std::uint_fast32_t color_function_b(const std::uint_fast32_t& c) const
  {
    return static_cast<std::uint_fast32_t>((float(c) * float(c)) / 255.0F);
  }
};

class color_stretches_base
{
public:
  virtual ~color_stretches_base() = default;

  void init(const std::uint_fast32_t total_pixels)
  {
    my_total_pixels = total_pixels;
    my_sum          = 0U;
  }

  virtual void color_stretch(std::uint_fast32_t&) = 0;

protected:
  std::uint_fast32_t my_total_pixels;
  std::uint_fast32_t my_sum;

  color_stretches_base() : my_total_pixels(0U),
                           my_sum         (0U) { }
};

class color_stretches_default final : public color_stretches_base
{
public:
  color_stretches_default() = default;

  virtual ~color_stretches_default() = default;

  virtual void color_stretch(std::uint_fast32_t& histogram_entry)
  {
    // Perform color stretching using the histogram approach.
    // Convert the histogram entries such that a given entry contains
    // the sum of its own entries plus all previous entries. This provides
    // a set of scale factors for the color. The histogram approach
    // automatically scales to the distribution of pixels in the image.

    my_sum += histogram_entry;

    const float sum_div_total_pixels =
      static_cast<float>(my_sum) / static_cast<float>(my_total_pixels);

    const float histogram_scale = std::pow(sum_div_total_pixels, 1.2F);

    const std::uint_fast32_t scaled_histogram_value =
      static_cast<std::uint_fast32_t>(histogram_scale * static_cast<float>(0xFFU));

    histogram_entry = UINT32_C(0xFF) - scaled_histogram_value;
  }
};

} // namespace detail

// Declare a base class for the Mandelbrot configuration.
template<typename NumericType,
         const std::uint_fast32_t MaxIterations>
class mandelbrot_config_base
{
public:
  static const std::uint_fast32_t max_iterations = MaxIterations;

  using mandelbrot_config_numeric_type = NumericType;

  virtual ~mandelbrot_config_base() = default;

  const mandelbrot_config_numeric_type& x_lo() const { return my_x_lo; }
  const mandelbrot_config_numeric_type& x_hi() const { return my_x_hi; }
  const mandelbrot_config_numeric_type& y_lo() const { return my_y_lo; }
  const mandelbrot_config_numeric_type& y_hi() const { return my_y_hi; }

  virtual int mandelbrot_fractional_resolution() const = 0;

  virtual const mandelbrot_config_numeric_type& step() const = 0;

  std::uint_fast32_t integral_width() const
  {
    const std::uint_fast32_t non_justified_width =
      static_cast<std::uint_fast32_t>(my_width / this->step());

    return non_justified_width;
  }

  std::uint_fast32_t integral_height() const
  {
    const std::uint_fast32_t non_justified_height =
      static_cast<std::uint_fast32_t>(my_height / this->step());

    return non_justified_height;
  }

protected:
  const mandelbrot_config_numeric_type my_x_lo;
  const mandelbrot_config_numeric_type my_x_hi;
  const mandelbrot_config_numeric_type my_y_lo;
  const mandelbrot_config_numeric_type my_y_hi;
  const mandelbrot_config_numeric_type my_width;
  const mandelbrot_config_numeric_type my_height;

  mandelbrot_config_base(const mandelbrot_config_numeric_type& xl,
                         const mandelbrot_config_numeric_type& xh,
                         const mandelbrot_config_numeric_type& yl,
                         const mandelbrot_config_numeric_type& yh)
    : my_x_lo  (xl),
      my_x_hi  (xh),
      my_y_lo  (yl),
      my_y_hi  (yh),
      my_width (my_x_hi - my_x_lo),
      my_height(my_y_hi - my_y_lo) { }

private:
  mandelbrot_config_base() = default;
};

// Make a template class that represents the Mandelbrot configuration.
// This class automatically creates sensible parameters based on
// the resolution of the fixed-point type supplied in the template
// parameter. If a custom pixel count is required, the step()
// method can be modified accordingly.
template<typename NumericType,
         const std::uint_fast32_t MaxIterations,
         const int MandelbrotFractionalResolution>
class mandelbrot_config final : public mandelbrot_config_base<NumericType, MaxIterations>
{
private:
  using base_class_type = mandelbrot_config_base<NumericType, MaxIterations>;

public:
  static_assert(MandelbrotFractionalResolution < -1,
                "The Mandelbrot fractional resolution should be less than -1");

  mandelbrot_config(const typename base_class_type::mandelbrot_config_numeric_type& xl,
                    const typename base_class_type::mandelbrot_config_numeric_type& xh,
                    const typename base_class_type::mandelbrot_config_numeric_type& yl,
                    const typename base_class_type::mandelbrot_config_numeric_type& yh)
    : base_class_type(xl, xh, yl, yh),
      my_step()
  {
    using std::ldexp;

    my_step = ldexp(typename base_class_type::mandelbrot_config_numeric_type(1U), MandelbrotFractionalResolution);
  }

  mandelbrot_config(const std::string& str_xl,
                    const std::string& str_xh,
                    const std::string& str_yl,
                    const std::string& str_yh)
    : base_class_type(boost::lexical_cast<typename base_class_type::mandelbrot_config_numeric_type>(str_xl),
                      boost::lexical_cast<typename base_class_type::mandelbrot_config_numeric_type>(str_xh),
                      boost::lexical_cast<typename base_class_type::mandelbrot_config_numeric_type>(str_yl),
                      boost::lexical_cast<typename base_class_type::mandelbrot_config_numeric_type>(str_yh)),
      my_step()
  {
    using std::ldexp;

    my_step = ldexp(typename base_class_type::mandelbrot_config_numeric_type(1U), MandelbrotFractionalResolution);
  }

  mandelbrot_config(const char* pc_xl,
                    const char* pc_xh,
                    const char* pc_yl,
                    const char* pc_yh)
    : base_class_type(boost::lexical_cast<typename base_class_type::mandelbrot_config_numeric_type>(std::string(pc_xl)),
                      boost::lexical_cast<typename base_class_type::mandelbrot_config_numeric_type>(std::string(pc_xh)),
                      boost::lexical_cast<typename base_class_type::mandelbrot_config_numeric_type>(std::string(pc_yl)),
                      boost::lexical_cast<typename base_class_type::mandelbrot_config_numeric_type>(std::string(pc_yh))),
      my_step()
  {
    using std::ldexp;

    my_step = ldexp(typename base_class_type::mandelbrot_config_numeric_type(1U), MandelbrotFractionalResolution);
  }

  virtual ~mandelbrot_config() { }

private:
  typename base_class_type::mandelbrot_config_numeric_type my_step;

  virtual int mandelbrot_fractional_resolution() const { return MandelbrotFractionalResolution; }

  virtual const typename base_class_type::mandelbrot_config_numeric_type& step() const { return my_step; }
};

// This class generates the rows of the mandelbrot iteration.
// The coordinates are set up according to the Mandelbrot configuration.
template<typename NumericType,
         const std::uint_fast32_t MaxIterations>
class mandelbrot_generator final
{
public:
  static const std::uint_fast32_t max_iterations = MaxIterations;

  using mandelbrot_config_type = mandelbrot_config_base<NumericType, max_iterations>;

  mandelbrot_generator(const mandelbrot_config_type& config)
    : mandelbrot_config_object   (config),
      mandelbrot_image           (config.integral_width(), config.integral_height()),
      mandelbrot_view            (boost::gil::rgb8_view_t()),
      mandelbrot_iteration_matrix(config.integral_width(),
                                  std::vector<std::uint_fast32_t>(config.integral_height())),
      mandelbrot_color_histogram (max_iterations + 1U, UINT32_C(0))
  {
    mandelbrot_view = boost::gil::view(mandelbrot_image);
  }

  ~mandelbrot_generator() = default;

  void generate_mandelbrot_image(const std::string&                  str_filename,
                                 const detail::color_functions_base& color_functions = detail::color_functions_bw(),
                                       detail::color_stretches_base& color_stretches = detail::color_stretches_default(),
                                       std::ostream&                 output_stream   = std::cout)
  {
    // Setup the x-axis and y-axis coordinates.

    std::vector<NumericType> x_values(mandelbrot_config_object.integral_width());
    std::vector<NumericType> y_values(mandelbrot_config_object.integral_height());

    {
      const NumericType local_step(mandelbrot_config_object.step());

      NumericType x_coord(mandelbrot_config_object.x_lo());
      NumericType y_coord(mandelbrot_config_object.y_hi());

      for(auto& x : x_values) { x = x_coord; x_coord += local_step; }
      for(auto& y : y_values) { y = y_coord; y_coord -= local_step; }
    }

    static const NumericType four(4U);

    std::atomic_flag mandelbrot_iteration_lock = ATOMIC_FLAG_INIT;

    std::size_t unordered_parallel_row_count = 0U;

    detail::my_concurrency::parallel_for
    (
      std::size_t(0U),
      y_values.size(),
      [&mandelbrot_iteration_lock, &unordered_parallel_row_count, &output_stream, &x_values, &y_values, this](std::size_t j_row)
      {
        while(mandelbrot_iteration_lock.test_and_set()) { ; }
        ++unordered_parallel_row_count;
        output_stream << "Calculating Mandelbrot image at row "
                      << unordered_parallel_row_count
                      << " of "
                      << y_values.size()
                      << " total: "
                      << std::fixed
                      << std::setprecision(1)
                      << (100.0F * float(unordered_parallel_row_count)) / float(y_values.size())
                      << "%. Have patience."
                      << "\r";
        mandelbrot_iteration_lock.clear();

        for(std::size_t i_col = 0U; i_col < x_values.size(); ++i_col)
        {
          NumericType zr (0U);
          NumericType zi (0U);
          NumericType zr2(0U);
          NumericType zi2(0U);

          // Use an optimized complex-numbered multiplication scheme.
          // Thereby reduce the main work of the Mandelbrot iteration to
          // three real-valued multiplications and several real-valued
          // addition/subtraction operations.

          std::uint_fast32_t iteration_result = UINT32_C(0);

          // Perform the iteration sequence for generating the Mandelbrot set.
          // Here is the main work of the program.

          while((iteration_result < max_iterations) && ((zr2 + zi2) < four))
          {
            // Optimized complex multiply and add.
            zi *= zr;

            zi  = (zi  + zi)  + y_values[j_row];
            zr  = (zr2 - zi2) + x_values[i_col];

            zr2 = zr; zr2 *= zr;
            zi2 = zi; zi2 *= zi;

            ++iteration_result;
          }

          while(mandelbrot_iteration_lock.test_and_set()) { ; }
          mandelbrot_iteration_matrix[i_col][j_row] = iteration_result;
          ++mandelbrot_color_histogram[iteration_result];
          mandelbrot_iteration_lock.clear();
        }
      }
    );

    output_stream << std::endl;

    output_stream << "Perform color stretching." << std::endl;
    apply_color_stretches(x_values, y_values, color_stretches);

    output_stream << "Apply color functions." << std::endl;
    apply_color_functions(x_values, y_values, color_functions);

    output_stream << "Write JPEG file." << std::endl;
    boost::gil::jpeg_write_view(str_filename, mandelbrot_view);

    output_stream << std::endl
                  << std::string("The ouptput file " + str_filename + " has been written")
                  << std::endl;
  }

private:
  const mandelbrot_config_type&                mandelbrot_config_object;

  boost::gil::rgb8_image_t                     mandelbrot_image;
  boost::gil::rgb8_view_t                      mandelbrot_view;

  std::vector<std::vector<std::uint_fast32_t>> mandelbrot_iteration_matrix;
  std::vector<std::uint_fast32_t>              mandelbrot_color_histogram;

  void apply_color_stretches(const std::vector<NumericType>& x_values,
                             const std::vector<NumericType>& y_values,
                             detail::color_stretches_base& color_stretches)
  {
    color_stretches.init(static_cast<std::uint_fast32_t>(x_values.size() * y_values.size()));

    for(auto& histogram_entry : mandelbrot_color_histogram)
    {
      color_stretches.color_stretch(histogram_entry);
    }
  }

  void apply_color_functions(const std::vector<NumericType>& x_values,
                             const std::vector<NumericType>& y_values,
                             const detail::color_functions_base& color_functions)
  {
    for(std::uint_fast32_t j_row = UINT32_C(0); j_row < y_values.size(); ++j_row)
    {
      for(std::uint_fast32_t i_col = UINT32_C(0); i_col < x_values.size(); ++i_col)
      {
        const std::uint_fast32_t color = mandelbrot_color_histogram[mandelbrot_iteration_matrix[i_col][j_row]];

        // Get the three hue values.
        const std::uint_fast32_t color_r = ((color <= 4U) ? color : color_functions.color_function_r(color));
        const std::uint_fast32_t color_g = ((color <= 4U) ? color : color_functions.color_function_g(color));
        const std::uint_fast32_t color_b = ((color <= 4U) ? color : color_functions.color_function_b(color));

        // Mix the color from the hue values.
        const std::uint8_t rh = static_cast<std::uint8_t>((255U * color_r) / UINT32_C(255));
        const std::uint8_t gh = static_cast<std::uint8_t>((255U * color_g) / UINT32_C(255));
        const std::uint8_t bh = static_cast<std::uint8_t>((255U * color_b) / UINT32_C(255));

        const boost::gil::rgb8_pixel_t the_color  = boost::gil::rgb8_pixel_t(rh, gh, bh);

        mandelbrot_view(i_col, j_row) = boost::gil::rgb8_pixel_t(the_color);
      }
    }
  }
};

} } } // namespace boost::multiprecision::mandelbrot

#define BOOST_MANDELBROT_IMAGE_INDEX_01_FULL                  1
#define BOOST_MANDELBROT_IMAGE_INDEX_03_TOP                   3
#define BOOST_MANDELBROT_IMAGE_INDEX_04_SWIRL                 4
#define BOOST_MANDELBROT_IMAGE_INDEX_05_SEAHORSES             5
#define BOOST_MANDELBROT_IMAGE_INDEX_06_BRANCHES              6
#define BOOST_MANDELBROT_IMAGE_INDEX_07_SEAHORSE_VALLEY       7
#define BOOST_MANDELBROT_IMAGE_INDEX_08_DEEP_DIVE_01          8
#define BOOST_MANDELBROT_IMAGE_INDEX_09_DEEP_DIVE_02          9
#define BOOST_MANDELBROT_IMAGE_INDEX_10_ZOOM_WIKI_01         10
#define BOOST_MANDELBROT_IMAGE_INDEX_11_ZOOM_VERY_DEEP       11

#if !defined(BOOST_MANDELBROT_IMAGE_INDEX)
#define BOOST_MANDELBROT_IMAGE_INDEX BOOST_MANDELBROT_IMAGE_INDEX_05_SEAHORSES
#endif

int main()
{
  #if (BOOST_MANDELBROT_IMAGE_INDEX == BOOST_MANDELBROT_IMAGE_INDEX_01_FULL)

    using numeric_type = boost::multiprecision::number<boost::multiprecision::cpp_dec_float<31>>;

    const std::string str_filename = "images/mandelbrot_" + std::string("BOOST_MANDELBROT_01_FULL") + ".jpg";

    // This is the classic full immage.
    using mandelbrot_config_type = boost::multiprecision::mandelbrot::mandelbrot_config<numeric_type, UINT32_C(2000), -10>;

    const mandelbrot_config_type mandelbrot_config_object(-2.000L, +0.500L,
                                                          -1.000L, +1.000L);

  #elif (BOOST_MANDELBROT_IMAGE_INDEX == BOOST_MANDELBROT_IMAGE_INDEX_03_TOP)

    using numeric_type = boost::multiprecision::number<boost::multiprecision::cpp_dec_float<31>>;

    const std::string str_filename = "images/mandelbrot_" + std::string("BOOST_MANDELBROT_03_TOP") + ".jpg";

    // This is a view of an upper part of the image (near the top of the classic full view).
    using mandelbrot_config_type = boost::multiprecision::mandelbrot::mandelbrot_config<numeric_type, UINT32_C(1000), -12>;

    const mandelbrot_config_type mandelbrot_config_object(-0.130L - 0.282L, -0.130L + 0.282L,
                                                          +0.856L - 0.282L, +0.856L + 0.282L);

  #elif (BOOST_MANDELBROT_IMAGE_INDEX == BOOST_MANDELBROT_IMAGE_INDEX_04_SWIRL)

    using numeric_type = boost::multiprecision::number<boost::multiprecision::cpp_dec_float<31>>;

    const std::string str_filename = "images/mandelbrot_" + std::string("BOOST_MANDELBROT_04_SWIRL") + ".jpg";

    // This is a fanning swirl image.
    using mandelbrot_config_type = boost::multiprecision::mandelbrot::mandelbrot_config<numeric_type, UINT32_C(2000), -22>;

    const mandelbrot_config_type mandelbrot_config_object(-0.749730L - 0.0002315L, -0.749730L + 0.0002315L,
                                                          -0.046608L - 0.0002315L, -0.046608L + 0.0002315L);

  #elif (BOOST_MANDELBROT_IMAGE_INDEX == BOOST_MANDELBROT_IMAGE_INDEX_05_SEAHORSES)

    using numeric_type = boost::multiprecision::number<boost::multiprecision::cpp_dec_float<31>>;

    const std::string str_filename = "images/mandelbrot_" + std::string("BOOST_MANDELBROT_05_SEAHORSES") + ".jpg";

    // This is a swirly seahorse image.
    using mandelbrot_config_type = boost::multiprecision::mandelbrot::mandelbrot_config<numeric_type, UINT32_C(2000), -48>;

    const mandelbrot_config_type
      mandelbrot_config_object(-0.7453983606667815L - 1.76E-12L, -0.7453983606667815L + 1.76E-12L,
                               +0.1125046349959942L - 1.76E-12L, +0.1125046349959942L + 1.76E-12L);

  #elif (BOOST_MANDELBROT_IMAGE_INDEX == BOOST_MANDELBROT_IMAGE_INDEX_06_BRANCHES)

    using numeric_type = boost::multiprecision::number<boost::multiprecision::cpp_dec_float<31>>;

    const std::string str_filename = "images/mandelbrot_" + std::string("BOOST_MANDELBROT_06_BRANCHES") + ".jpg";

    // This is a spiral image of branches.
    using mandelbrot_config_type = boost::multiprecision::mandelbrot::mandelbrot_config<numeric_type, UINT32_C(2000), -47>;

    const mandelbrot_config_type mandelbrot_config_object(+0.3369844464873L - 4.2E-12L, +0.3369844464873L + 4.2E-12L,
                                                          +0.0487782196791L - 4.2E-12L, +0.0487782196791L + 4.2E-12L);

  #elif (BOOST_MANDELBROT_IMAGE_INDEX == BOOST_MANDELBROT_IMAGE_INDEX_07_SEAHORSE_VALLEY)

    using numeric_type = boost::multiprecision::number<boost::multiprecision::cpp_dec_float<31>>;

    const std::string str_filename = "images/mandelbrot_" + std::string("BOOST_MANDELBROT_07_SEAHORSE_VALLEY") + ".jpg";

    // This is an image from the seahorse valley.
    using mandelbrot_config_type = boost::multiprecision::mandelbrot::mandelbrot_config<numeric_type, UINT32_C(1000), -15>;

    const mandelbrot_config_type
      mandelbrot_config_object("-0.748", "-0.700",
                               "+0.222", "+0.270");

  #elif (BOOST_MANDELBROT_IMAGE_INDEX == BOOST_MANDELBROT_IMAGE_INDEX_08_DEEP_DIVE_01)

    using numeric_type = boost::multiprecision::number<boost::multiprecision::cpp_dec_float<127>>;

    const std::string str_filename = "images/mandelbrot_" + std::string("BOOST_MANDELBROT_08_DEEP_DIVE_01") + ".jpg";

    // This is a deep zoom image.
    // Note: Use 128 or more decimal digits for this iteration.

    static_assert(std::numeric_limits<numeric_type>::digits10 >= 127,
                  "Error: Please use 127 or more decimal digits for BOOST_MANDELBROT_08_DEEP_DIVE_01.");

    using mandelbrot_config_type = boost::multiprecision::mandelbrot::mandelbrot_config<numeric_type, UINT32_C(2000), -365>;

    const numeric_type delta("+1.25E-107");
    const numeric_type cx   ("-1.99999999913827011875827476290869498831680913663682095950680227271547027727918984035447670553861909622481524124");
    const numeric_type cy   ("+0.00000000000001314895443507637575136247566806505002151700520912095709529449343530548994027524594471095886432006");

    const mandelbrot_config_type
      mandelbrot_config_object(cx - delta, cx + delta,
                               cy - delta, cy + delta);

  #elif (BOOST_MANDELBROT_IMAGE_INDEX == BOOST_MANDELBROT_IMAGE_INDEX_09_DEEP_DIVE_02)

    using numeric_type = boost::multiprecision::number<boost::multiprecision::cpp_dec_float<79>>;

    const std::string str_filename = "images/mandelbrot_" + std::string("BOOST_MANDELBROT_09_DEEP_DIVE_02") + ".jpg";

    // This is a deep zoom image.
    // Note: Use 79 or more decimal digits for this iteration.

    static_assert(std::numeric_limits<numeric_type>::digits10 >= 79,
                  "Error: Please use 79 or more decimal digits for BOOST_MANDELBROT_09_DEEP_DIVE_02.");

    using mandelbrot_config_type = boost::multiprecision::mandelbrot::mandelbrot_config<numeric_type, UINT32_C(10000), -191>;

    const numeric_type delta("+2.15E-55");
    const numeric_type cx   (numeric_type("-1.295189082147777457017064177185681926706566460884888469217456"));
    const numeric_type cy   (numeric_type("+0.440936982678320138880903678356262612113214627431396203682661"));

    const mandelbrot_config_type
      mandelbrot_config_object(cx - delta, cx + delta,
                               cy - delta, cy + delta);

  #elif (BOOST_MANDELBROT_IMAGE_INDEX ==  BOOST_MANDELBROT_IMAGE_INDEX_10_ZOOM_WIKI_01)

    using numeric_type = boost::multiprecision::number<boost::multiprecision::cpp_dec_float<47>>;

    const std::string str_filename = "images/mandelbrot_" + std::string("BOOST_MANDELBROT_10_ZOOM_WIKI_01") + ".jpg";

    // This is a medium zoom image from the zoom coordinates of:
    // http://en.wikipedia.org/wiki/File:Mandelbrot_sequence_new.gif
    // Note: Use 39 or more decimal digits for this iteration.

    static_assert(std::numeric_limits<numeric_type>::digits10 >= 47,
                  "Error: Please use 47 or more decimal digits for BOOST_MANDELBROT_10_ZOOM_WIKI_01.");

    using mandelbrot_config_type = boost::multiprecision::mandelbrot::mandelbrot_config<numeric_type, UINT32_C(20000), -91>;

    const numeric_type delta("+3.0E-25");
    const numeric_type cx   (numeric_type("-0.743643887037158704752191506114774"));
    const numeric_type cy   (numeric_type("+0.131825904205311970493132056385139"));

    const mandelbrot_config_type
      mandelbrot_config_object(cx - delta, cx + delta,
                               cy - delta, cy + delta);

  #elif (BOOST_MANDELBROT_IMAGE_INDEX ==  BOOST_MANDELBROT_IMAGE_INDEX_11_ZOOM_VERY_DEEP)

    using numeric_type = boost::multiprecision::number<boost::multiprecision::cpp_dec_float<147>>;

    const std::string str_filename = "images/mandelbrot_" + std::string("BOOST_MANDELBROT_IMAGE_INDEX_11_ZOOM_VERY_DEEP") + ".jpg";

    using mandelbrot_config_type = boost::multiprecision::mandelbrot::mandelbrot_config<numeric_type, UINT32_C(50000), -424>;

    // TBD: Try to zoom in deeper.
    // The author here:
    // https://www.youtube.com/watch?v=pCpLWbHVNhk
    // Reports Zoom: 3.4e1091
    const numeric_type delta("+1.0E-125");
    const numeric_type cx   (numeric_type("+0.360240443437614363236125244449545308482607807958585750488375814740195346059218100311752936722773426396233731729724987737320035372683285317664532401218521579554288661726564324134702299962817029213329980895208036363104546639698106204384566555001322985619004717862781192694046362748742863016467354574422779443226982622356594130430232458472420816652623492974891730419252651127672782407292315574480207005828774566475024380960675386215814315654794021855269375824443853463117354448779647099224311848192893972572398662626725254769950976527431277402440752868498588785436705371093442460696090720654908973712759963732914849861213100695402602927267843779747314419332179148608587129105289166676461292845685734536033692577618496925170576714796693411776794742904333484665301628662532967079174729170714156810530598764525260869731233845987202037712637770582084286587072766838497865108477149114659838883818795374195150936369987302574377608649625020864292915913378927790344097552591919409137354459097560040374880346637533711271919419723135538377394364882968994646845930838049998854075817859391340445151448381853615103761584177161812057928"));
    const numeric_type cy   (numeric_type("-0.6413130610648031748603750151793020665794949522823052595561775430644485741727536902556370230689681162370740565537072149790106973211105273740851993394803287437606238596262287731075999483940467161288840614581091294325709988992269165007394305732683208318834672366947550710920088501655704252385244481168836426277052232593412981472237968353661477793530336607247738951625817755401065045362273039788332245567345061665756708689359294516668271440525273653083717877701237756144214394870245598590883973716531691124286669552803640414068523325276808909040317617092683826521501539932397262012011082098721944643118695001226048977430038509470101715555439047884752058334804891389685530946112621573416582482926221804767466258346014417934356149837352092608891639072745930639364693513216719114523328990690069588676087923656657656023794484324797546024248328156586471662631008741349069961493817600100133439721557969263221185095951241491408756751582471307537382827924073746760884081704887902040036056611401378785952452105099242499241003208013460878442953408648178692353788153787229940221611731034405203519945313911627314900851851072122990492499999999999999999991"));

    const mandelbrot_config_type
      mandelbrot_config_object(cx - delta, cx + delta,
                               cy - delta, cy + delta);

  #else

    #error: Mandelbrot image type is not defined!

  #endif

  using mandelbrot_numeric_type = mandelbrot_config_type::mandelbrot_config_numeric_type;

  using mandelbrot_generator_type =
    boost::multiprecision::mandelbrot::mandelbrot_generator<mandelbrot_numeric_type,
                                                            mandelbrot_config_type::max_iterations>;

  const std::clock_t start = std::clock();

        boost::multiprecision::mandelbrot::detail::color_stretches_default local_color_stretches;
  const boost::multiprecision::mandelbrot::detail::color_functions_bw      local_color_functions;

  mandelbrot_generator_type mandelbrot_generator(mandelbrot_config_object);

  mandelbrot_generator.generate_mandelbrot_image(str_filename,
                                                 local_color_functions,
                                                 local_color_stretches);

  const float elapsed = (float(std::clock()) - float(start)) / float(CLOCKS_PER_SEC);

  std::cout << "Time for calculation: "
            << elapsed
            << "s"
            << std::endl;
}
