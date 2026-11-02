#include <string>
#include <type_traits>
template <typename... Ts>
struct TypeList {};

using MyTypes = TypeList<int, double, std::string>;

template <typename List>
struct Front;

template <typename T, typename... Rest>
struct Front<TypeList<T, Rest...>> {
  using type = T;
};

// Template Partial Specialization:
// Front<TypeList<int, doule, std::string>> matches Front<TypeList<T, Rest...>>
using T = Front<MyTypes>::type;

static_assert(std::is_same_v<T, int>);