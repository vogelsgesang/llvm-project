// RUN: %check_clang_tidy -std=c++23-or-late %s modernize-use-range-insert %t

// Some *very* simplified versions of `map` etc.
namespace std {

template <typename T>
struct vector {
  struct const_iterator {};
  struct iterator : public const_iterator {};

  template <class InputIterator>
    iterator insert(const_iterator position, InputIterator first, InputIterator last);
  template<typename R>
    constexpr iterator insert_range(const_iterator position, R&& rg);
  template<typename R>
    constexpr iterator prepend_range(const_iterator position, R&& rg);
  template<typename R>
    constexpr iterator append_range(const_iterator position, R&& rg);

  iterator begin();
  iterator end();
};

template <typename T>
T::iterator begin(T& c) { return c.begin(); }

template <typename T>
T::iterator end(T& c) { return c.end(); }

} // namespace std

void prepend_vector(std::vector<int>& v1, std::vector<int> v2) {
   v1.insert(v1.begin(), v2.begin(), v2.end());
}

/*
void append_vector(std::vector<int>& v1, std::vector<int> v2) {
   v1.insert(v1.begin(), v2.begin(), v2.end());
}

void insert_vector_middle(std::vector<int>& v1, std::vector<int> v2) {
   v1.insert(v1.begin() + 2, v2.begin(), v2.end());
}

void append_vector_free_funcs(std::vector<int>& v1, std::vector<int> v2) {
   v1.insert(begin(v1), begin(v2), end(v2));
}
*/

// TODO:
// * prepend_range, append_range, push_range, 
// * custom type
