#ifdef SWL_CPP_LIBRARY_VARIANT_HPP

template <class T>
T&& declval();

template <class T>
struct array_wrapper { 
	T data;
};

// find the first position of the type
template <class T, class... Ts>
constexpr std::size_t find_type(){
	constexpr auto size = sizeof...(Ts);
	constexpr bool same[size] = {std::is_same_v<T, Ts>...};
	for (std::size_t k = 0; k < size; ++k)
		if (same[k]) return k;
	return size;
}

template <class T, class... Ts>
inline constexpr bool appears_exactly_once = (static_cast<unsigned short>(std::is_same_v<T, Ts>) + ...) == 1;

// ============= type pack element 

template <class... Ts>
static constexpr unsigned true_ = sizeof...(Ts) < (1000000000000000);

template <unsigned char = 1>
struct find_type_i;

template <>
struct find_type_i<1> {
	template <std::size_t Idx, class T, class... Ts>
	using f = typename find_type_i<(Idx != 1)>::template f<Idx - 1, Ts...>;
};

template <>
struct find_type_i<0> {
	template <std::size_t, class T, class... Ts>
	using f = T;
};

template <std::size_t K, class... Ts>
using type_pack_element = typename find_type_i<(K != 0 and true_<Ts...>)>::template f<K, Ts...>;

// ============= overload match detector. to be used for variant generic assignment

template <class Dest>
void check_no_narrowing(Dest(&&)[1]);

template <class T>
using arr1 = T[1];

template <std::size_t N, class A>
struct overload_frag {
	using type = A;
    template <class T>
        requires requires { arr1<A>{std::declval<T>()}; }
	auto operator()(A, T&&) -> overload_frag<N, A>;
}; 

template <class Seq, class... Args>
struct make_overload;

template <std::size_t... Idx, class... Args>
struct make_overload<std::integer_sequence<std::size_t, Idx...>, Args...>
	 : overload_frag<Idx, Args>... { 
	using overload_frag<Idx, Args>::operator()...;
};

#define find_best_overload(T, Pack) make_overload<std::make_index_sequence<sizeof...(Pack)>, Pack ...>{}( declval<T>(), declval<T>() ) 

/* 
template <class T, class... Ts>
using best_overload_match 
	= typename decltype( make_overload<std::make_index_sequence<sizeof...(Ts)>, Ts...>{}( declval<T>(), declval<T>() ) 
					   )::type; */ 
					   
template <class T, class... Ts>
using best_overload_match = typename decltype( find_best_overload(T, Ts) )::type;

/* 
template <class T, class... Ts>
inline constexpr bool has_non_ambiguous_match 
	= requires { make_overload<std::make_index_sequence<sizeof...(Ts)>, Ts...>{}( declval<T>(), declval<T>() ); };  */ 
	
template <class T, class... Ts>
concept has_non_ambiguous_match = 
	requires { typename best_overload_match<T, Ts...>; };

// ================================== rel ops

template <class From, class To>
concept convertible = std::is_convertible_v<From, To>;

template <class T>
concept has_eq_comp = requires (T a, T b) { 
	{ a == b } -> convertible<bool>; 
};

template <class T>
concept has_lesser_comp = requires (T a, T b) { 
	{ a < b } -> convertible<bool>; 
};

template <class T>
concept has_lesser_than_comp = requires (T a, T b) { 
	{ a <= b } -> convertible<bool>;
};

template <class A>
struct eq_comp {
	constexpr bool operator()(const auto& elem, auto index) const noexcept { 
		return (a.template get<index>() == elem); 
	}
	const A& a;
};

template <class A>
struct emplace_into {
	constexpr void operator()(auto&& elem, auto index) const {
		a.template emplace<index>(decltype(elem)(elem));
	}
	A& a;
};
	
// =============================== variant union types
	
// =================== base variant storage type 
// this type is used to build a N-ary tree of union. 

struct dummy_type{}; // used to fill the back of union nodes

using union_index_t = unsigned;
	
#define UNION_SFM_TRAITS(X)  X has_copy_ctor, X trivial_copy_ctor, X has_copy_assign, X trivial_copy_assign, \
							 X has_move_ctor, X trivial_move_ctor, X has_move_assign, X trivial_move_assign, \
							 X trivial_dtor 

template <UNION_SFM_TRAITS(bool)>
struct traits{};

#define INJECT_UNION_SFM_FRAG(X) \
	constexpr X (const X &) 			requires trivial_copy_ctor = default; \
	constexpr X (const X &) 			requires (has_copy_ctor and not trivial_copy_ctor) {} \
	constexpr X (X &&) 					requires trivial_move_ctor = default; \
	constexpr X (X &&) 					requires (has_move_ctor and not trivial_move_ctor) {} \
	constexpr X & operator=(const X &) 	requires trivial_copy_assign = default; \
	constexpr X & operator=(const X &) 	requires (has_copy_assign and not trivial_copy_assign) {} \
	constexpr X & operator=(X &&) 		requires trivial_move_assign = default; \
	constexpr X & operator=(X &&) 		requires (has_move_assign and not trivial_move_assign) {} \
	constexpr ~ X () 					requires (not trivial_dtor) {} \
	constexpr ~ X () 					requires trivial_dtor = default; 

template <class Traits, bool IsTerminal, class... Ts>
union variant_union;

template <UNION_SFM_TRAITS(bool), class A, class B>
union variant_union< traits<UNION_SFM_TRAITS()>, false, A, B> {
	
	static constexpr auto elem_size = A::elem_size + B::elem_size;
	
	constexpr variant_union() = default;
	
	template <std::size_t Index, class... Args>
		requires (Index < A::elem_size)
	constexpr variant_union(in_place_index_t<Index>, Args&&... args)
	: a{ in_place_index<Index>, static_cast<Args&&>(args)... } {}
	
	template <std::size_t Index, class... Args>
		requires (Index >= A::elem_size)
	constexpr variant_union(in_place_index_t<Index>, Args&&... args)
	: b{ in_place_index<Index - A::elem_size>, static_cast<Args&&>(args)... } {} 
	
	template <union_index_t Index>
	constexpr auto& get(){
		if constexpr 		( Index < A::elem_size )
			return a.template get<Index>();
		else 
			return b.template get<Index - A::elem_size>();
	}
	
	INJECT_UNION_SFM_FRAG(variant_union)
	
	A a;
	B b;
};

template <UNION_SFM_TRAITS(bool), class A, class B>
union variant_union<traits<UNION_SFM_TRAITS()>, true, A, B> {
	
	static constexpr union_index_t elem_size = not( std::is_same_v<B, dummy_type> ) ? 2 : 1;
	
	constexpr variant_union() = default;
	
	template <class... Args>
	constexpr variant_union(in_place_index_t<0>, Args&&... args)
	: a{static_cast<Args&&>(args)...} {}
	
	template <class... Args>
	constexpr variant_union(in_place_index_t<1>, Args&&... args)
	: b{static_cast<Args&&>(args)...} {}
	
	template <union_index_t Index>
	constexpr auto& get(){
		if constexpr 		( Index == 0 )
			return a;
		else return b;
	}
	
	INJECT_UNION_SFM_FRAG(variant_union)
	
	A a;
	B b;
};

struct valueless_construct_t{};

template <class Traits, class Impl>
union variant_top_union;

template <UNION_SFM_TRAITS(bool), class A>
union variant_top_union< traits<UNION_SFM_TRAITS()>, A> {

	constexpr variant_top_union() = default;
	constexpr variant_top_union(valueless_construct_t) : dummy{} {}
	
	template <class... Args>
	constexpr variant_top_union(Args&&... args) : impl{static_cast<Args&&>(args)...} {}
	
	INJECT_UNION_SFM_FRAG(variant_top_union)
	
	A impl;
	dummy_type dummy;
};

#undef INJECT_UNION_SFM_FRAG

// =================== algorithm to build the tree of unions 
// take a sequence of types and perform an order preserving fold until only one type is left
// the first parameter is the numbers of types remaining for the current pass

constexpr unsigned char pick_next(unsigned remaining){
	return remaining >= 2 ? 2 : remaining;
}

template <unsigned char Pick, unsigned char GoOn, bool FirstPass, class Traits>
struct make_tree;

template <bool IsFirstPass, class Traits>
struct make_tree<2, 1, IsFirstPass, Traits> {
	template <unsigned Remaining, class A, class B, class... Ts>
	using f = typename make_tree<pick_next(Remaining - 2), 
								 sizeof...(Ts) != 0,
								 IsFirstPass, 
								 Traits
								 >::template f< Remaining - 2, 
								 				Ts..., 
								 				variant_union<Traits, IsFirstPass, A, B>
								 			  >; 
};

// only one type left, stop
template <bool F, class Traits>
struct make_tree<0, 0, F, Traits> {
	template <unsigned, class A>
	using f = A;
};

// end of one pass, restart
template <bool IsFirstPass, class Traits>
struct make_tree<0, 1, IsFirstPass, Traits> {
	template <unsigned Remaining, class... Ts>
	using f = typename make_tree<pick_next(sizeof...(Ts)), 
								 (sizeof...(Ts) != 1), 
								 false,  // <- both first pass and tail call recurse into a tail call
								 Traits
								>::template f<sizeof...(Ts), Ts...>;
};

// one odd type left in the pass, put it at the back to preserve the order
template <class Traits>
struct make_tree<1, 1, false, Traits> {
	template <unsigned Remaining, class A, class... Ts>
	using f = typename make_tree<0, sizeof...(Ts) != 0, false, Traits>::template f<0, Ts..., A>;
};

// one odd type left in the first pass, wrap it in an union
template <class Traits>
struct make_tree<1, 1, true, Traits> {
	template <unsigned, class A, class... Ts>
	using f = typename make_tree<0, sizeof...(Ts) != 0, false, Traits>
		::template f<0, Ts..., 
					 variant_union<Traits, true, A, dummy_type>
					>;
};

template <class Traits, class... Ts>
using make_tree_union = typename 
	make_tree<pick_next(sizeof...(Ts)), 1, true, Traits>::template f<sizeof...(Ts), Ts...>;

// ============================================================

// Ts... must be sorted in ascending size 
template <std::size_t Num, class... Ts>
using smallest_suitable_integer_type = 
	type_pack_element<(static_cast<unsigned char>(Num > std::numeric_limits<Ts>::max()) + ...),
					  Ts...
					  >;

template <bool HasAdlSwap>
struct swap_trait;

template <>
struct swap_trait<true> {
	template <class T>
	static constexpr bool nothrow = noexcept( swap(declval<T&>(), declval<T&>()) );
};

template <>
struct swap_trait<false> {
	template <class T>
	static constexpr bool nothrow = false;
};

// gcc is_swappable and is_nothrow_swappable implementation is broken? 
template <class A>
concept swappable = requires (A a, A b) { swap(a, b); } 
			|| requires (A a, A b) { std::swap(a, b); };
			
template <class T>
inline constexpr bool nothrow_swappable = 
	(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>)
	|| swap_trait< requires (T a, T b) { swap(a, b); } >::template nothrow<T>;

#ifdef SWL_CPP_VARIANT_USE_STD_HASH
  
template <class T>
inline constexpr bool has_std_hash = requires (T t) { 
	std::size_t( ::std::hash<T>{}(t) ); 
};

#endif

#endif
					  