#ifndef SWL_CPP_LIBRARY_VARIANT_HPP
#define SWL_CPP_LIBRARY_VARIANT_HPP

#include <exception>
#include <type_traits>
#include <utility>
#include <new>
#include <limits>

#ifdef SWL_CPP_VARIANT_USE_STD_HASH

	// yeah, that's not great, but <functional> is an enormous header, so until modules are here...
	#ifdef __GLIBCXX__
		#include <bits/functional_hash.h>
	#elif defined(_LIBCPP_VERSION)
		// include nothing if libc++
	#else 
		#include <functional>
	#endif

#endif

namespace swl {

template <class T>
struct in_place_type_t{};

template <std::size_t Index>
struct in_place_index_t{};

template <std::size_t Index>
inline static constexpr in_place_index_t<Index> in_place_index;

template <class T>
inline static constexpr in_place_type_t<T> in_place_type;

struct bad_variant_access : std::exception {
	bad_variant_access(const char* msg) noexcept : message(msg) {}
	const char* what() const noexcept override { return message; }
	const char* message;
};

namespace vimpl {
	#include "variant_detail.hpp"
	#include "variant_visit.hpp"
	struct variant_detector_t{};
}

template <class T>
inline constexpr bool is_variant = std::is_base_of_v<vimpl::variant_detector_t, std::decay_t<T>>;

template <class... Ts>
class variant : private vimpl::variant_detector_t {
	
	static_assert( not (std::is_array_v<Ts> || ...), "A variant cannot contain a raw array type, consider using std::array instead." );
	static_assert( sizeof...(Ts) > 0, "A variant cannot be empty.");
	static_assert( not (std::is_reference_v<Ts> || ...), "A variant cannot contain references, consider using reference wrappers instead." );
	static_assert( not (std::is_void_v<Ts> || ...), "A variant cannot contains void." );
	
	static constexpr bool is_trivial 			= (std::is_trivial_v<Ts> && ...);
	static constexpr bool has_move_ctor			= (std::is_move_constructible_v<Ts> && ...);
	static constexpr bool trivial_move_ctor		= is_trivial || has_move_ctor && (std::is_trivially_move_constructible_v<Ts> && ...);
	static constexpr bool has_copy_ctor			= (std::is_copy_constructible_v<Ts> && ...);
	static constexpr bool trivial_copy_ctor 	= is_trivial || has_copy_ctor && (std::is_trivially_copy_constructible_v<Ts> && ...);
	static constexpr bool has_copy_assign 		= (std::is_copy_constructible_v<Ts> && ...);
	static constexpr bool trivial_copy_assign 	= is_trivial || has_copy_assign && (std::is_trivially_copy_assignable_v<Ts> && ...);
	static constexpr bool has_move_assign		= (std::is_move_assignable_v<Ts> && ...);
	static constexpr bool trivial_move_assign   = is_trivial || has_move_assign && (std::is_trivially_move_assignable_v<Ts> && ...);
	static constexpr bool trivial_dtor 			= (std::is_trivially_destructible_v<Ts> && ...);
	
	template <bool PassIndex = false>
	using make_dispatcher_t = vimpl::make_dispatcher<std::make_index_sequence<sizeof...(Ts)>, PassIndex>;
	
	public : 
	
	template <std::size_t Idx>
	using alternative = vimpl::type_pack_element<Idx, Ts...>;
	
	static constexpr bool can_be_valueless = is_trivial;
	
	static constexpr unsigned size = sizeof...(Ts);
	
	using index_type = vimpl::smallest_suitable_integer_type<sizeof...(Ts) + can_be_valueless, unsigned char, unsigned short, unsigned>;
	
	static constexpr index_type npos = -1;
	
	// ============================================= constructors (20.7.3.2)
	
	// default constructor
	constexpr variant() 
		noexcept (std::is_nothrow_default_constructible_v<alternative<0>>)
		requires std::is_default_constructible_v<alternative<0>> 
	: storage{ in_place_index<0> }, current{0}
	{}
	
	// copy constructor (trivial)
	constexpr variant(const variant&)
		requires trivial_copy_ctor
	= default;
	
	// note : both the copy and move constructor cannot be meaningfully constexpr before std::construct_at (C++20)
	// copy constructor
	constexpr variant(const variant& o)
		requires (has_copy_ctor and not trivial_copy_ctor)
	: storage{ vimpl::valueless_construct_t{} } {
		o.visit_with_index( vimpl::emplace_into<variant&>{*this} );
	}
	
	// move constructor (trivial)
	constexpr variant(variant&&)
		requires trivial_move_ctor
	= default;
	
	// move constructor
	constexpr variant(variant&& o)
		noexcept ((std::is_nothrow_move_constructible_v<Ts> && ...))
		requires (has_move_ctor and not trivial_move_ctor)
	: storage{ vimpl::valueless_construct_t{} } {
		static_cast<variant&&>(o).visit_with_index( vimpl::emplace_into<variant&>{*this} );
	}
	
	// generic constructor
	template <class T, class M = vimpl::best_overload_match<T&&, Ts...>>
		requires ( !std::is_same_v<std::decay_t<T>, variant> && vimpl::has_non_ambiguous_match<T, Ts...> )
	constexpr variant(T&& t) 
		noexcept ( std::is_nothrow_constructible_v<M, T&&> )
	: variant{ in_place_index< vimpl::find_type<M, Ts... >() >, static_cast<T&&>(t) }
	{}
	
	// construct at index
	template <std::size_t Index, class... Args>
	explicit constexpr variant(in_place_index_t<Index> tag, Args&&... args)
	: storage{tag, static_cast<Args&&>(args)...}, current(Index) 
	{}
	
	// construct a given type
	template <class T, class... Args>
	explicit constexpr variant(in_place_type_t<T> tag, Args&&... args)
	: variant{ in_place_index<vimpl::find_type<T, Ts...>()>, static_cast<Args&&>(args)... }
	{}
	
	// ================================ destructors (20.7.3.3)
	
	constexpr ~variant() requires trivial_dtor = default;
	
	constexpr ~variant() requires (not trivial_dtor) {
		visit( [] (auto& elem) {
			using type = std::decay_t<decltype(elem)>;
			if constexpr ( not std::is_trivially_destructible_v<type> )
				elem.~type();
		});
	}
	
	// ================================ assignment (20.7.3.4)
	
	// copy assignment (trivial)
	constexpr variant& operator=(const variant& o)
		requires trivial_copy_assign && trivial_copy_ctor 
	= default;
	
	// copy assignment 
	constexpr variant& operator=(const variant& o)
		requires (has_copy_assign and not(trivial_copy_assign && trivial_copy_ctor))
	{	
		o.visit_with_index( [this] (const auto& elem, auto index_cst) {
			if (index() == index_cst)
				this->get<index_cst>() = elem;
			else
				this->emplace<index_cst>(elem);
		});
		return *this;
	}
	
	// move assignment (trivial)
	constexpr variant& operator=(variant&& o)
		requires (trivial_move_assign and trivial_move_ctor and trivial_dtor)
	= default;
	
	// move assignment
	constexpr variant& operator=(variant&& o)
		noexcept ((std::is_nothrow_move_constructible_v<Ts> && ...) && (std::is_nothrow_move_assignable_v<Ts> && ...))
		requires (has_move_assign && has_move_ctor and not(trivial_move_assign and trivial_move_ctor and trivial_dtor))
	{
		o.visit_with_index( [this] (auto&& elem, auto index_cst) {
			if (index() == index_cst)
				this->get<index_cst>() = std::move(elem);
			else 
				this->emplace<index_cst>(std::move(elem));
		});
		return *this;
	}
	
	// generic assignment 
	template <class T>
		requires vimpl::has_non_ambiguous_match<T, Ts...>
	constexpr variant& operator=(T&& t) {
		using namespace vimpl;
		using related_type = best_overload_match<T, Ts...>;
		constexpr auto new_index = find_type<related_type, Ts...>();
		if (current == new_index)
			get<new_index>() = static_cast<T&&>(t);
		else 
			emplace<new_index>(static_cast<T&&>(t));
		return *this;
	}
	
	// ================================== modifiers (20.7.3.5)
	
	template <class T, class... Args>
	T& emplace(Args&&... args){
		static_assert( (static_cast<unsigned short>(std::is_same_v<T, Ts>) + ...) == 1,
			"Variant emplace : the type to be emplaced must appear exactly once." ); 
		
		constexpr auto Index = vimpl::find_type<T, Ts...>();	
		emplace<Index>(static_cast<Args&&>(args)...);
		return get<Index>();
	}
	
	template <std::size_t Idx, class... Args>
	auto& emplace(Args&&... args){
		using T = std::remove_reference_t<decltype(get<Idx>())>;
		if constexpr (not std::is_nothrow_constructible_v<T, Args&&...>)
			current = npos;
		this->~variant();
		new(static_cast<void*>(&get<Idx>())) T (static_cast<Args&&>(args)...);
		current = static_cast<index_type>(Idx);
		return get<Idx>();
	}
	
	// ==================================== value status (20.7.3.6)
	
	constexpr bool valueless_by_exception() const noexcept {
		if constexpr ( can_be_valueless )
			return current == npos;
		else return false;
	}
	
	constexpr std::size_t index() const noexcept {
		return current;
	}
	
	// =================================== swap (20.7.3.7)
	
	void swap(variant& o) requires has_move_ctor {
		if constexpr (can_be_valueless){
			if (valueless_by_exception() && o.valueless_by_exception())
				return;
			}
		
		if (index() == o.index())
			o.visit_with_index( [this] (auto& elem, auto index_) {
				using std::swap;
				swap(get<index_>(), elem);
			});
		else {
			auto tmp = o;
			o = std::move(*this);
			(*this) = std::move(tmp);
		}
	}
	
	// +================================== methods for internal use
	// these methods performs no errors checking at all
	
	template <vimpl::union_index_t Idx>
	constexpr auto& get() & noexcept	{ 
		return storage.impl.template get<Idx>(); 
	}
	
	template <vimpl::union_index_t Idx>
	constexpr auto&& get() && noexcept { 
		return std::move(storage.impl.template get<Idx>()); 
	}
	
	template <vimpl::union_index_t Idx>
	constexpr const auto& get() const noexcept { 
		return const_cast<variant&>(*this).get<Idx>();
	}
	
	template <class VisitorType>
	constexpr decltype(auto) visit(VisitorType&& fn){
		return make_dispatcher_t<false>::template dispatcher<VisitorType&&, variant&>[current](decltype(fn)(fn), *this);
	}
	
	template <class VisitorType>
	constexpr decltype(auto) visit_with_index(VisitorType&& fn){ 
		return make_dispatcher_t<true>::template dispatcher<VisitorType&&, variant&>[current](decltype(fn)(fn), *this);
	}
	
	template <class VisitorType>
	constexpr decltype(auto) visit_with_index(VisitorType&& fn) const { 
		return make_dispatcher_t<true>::template dispatcher<VisitorType&&, const variant&>[current](decltype(fn)(fn), *this);
	}
	
	private :
	
	using storage_t = vimpl::variant_top_union<vimpl::make_tree_union<Ts...>>;
	
	storage_t storage;
	index_type current;
};

// ================================= value access (20.7.5)

template <class T, class... Ts>
constexpr bool holds_alternative(const variant<Ts...>& v) noexcept {
	static_assert( (std::is_same_v<T, Ts> || ...), "Requested type is not contained in the variant" );
	constexpr auto Index = vimpl::find_type<T, Ts...>();
	return v.index() == Index;
}

// ========= get by index

template <std::size_t Idx, class... Ts>
constexpr auto& get (variant<Ts...>& v){
	static_assert( Idx < sizeof...(Ts), "Index exceeds the variant size. ");
	if (v.index() != Idx) throw bad_variant_access{"swl::variant : Bad variant access."};
	return (v.template get<Idx>());
}

template <std::size_t Idx, class... Ts>
constexpr const auto& get (const variant<Ts...>& v){
	return swl::get<Idx>(const_cast<variant<Ts...>&>(v));
}

template <std::size_t Idx, class... Ts>
constexpr auto&& get (variant<Ts...>&& v){
	return std::move( swl::get<Idx>(v) );
}

template <std::size_t Idx, class... Ts>
constexpr const auto&& get (const variant<Ts...>&& v){
	return std::move( swl::get<Idx>(v) );
}

// ========= get by type

template <class T, class... Ts>
constexpr T& get (variant<Ts...>& v){
	return swl::get<vimpl::find_type<T, Ts...>()>(v);
}

template <class T, class... Ts>
constexpr const T& get (const variant<Ts...>& v){
	return swl::get<vimpl::find_type<T, Ts...>()>(v);
}

template <class T, class... Ts>
constexpr T&& get (variant<Ts...>&& v){
	return swl::get<vimpl::find_type<T, Ts...>()>( static_cast<variant<Ts...>&&>(v) );
}

template <class T, class... Ts>
constexpr const T&& get (const variant<Ts...>&& v){
	return swl::get<vimpl::find_type<T, Ts...>()>(decltype(v)(v));
}

// ===== get_if by index

template <std::size_t Idx, class... Ts>
constexpr auto* get_if(variant<Ts...>* v) noexcept {
	if (v == nullptr || v->index() != Idx) return decltype(&v->template get<Idx>()){nullptr};
	else return &v->template get<Idx>();
}

template <std::size_t Idx, class... Ts>
constexpr const auto* get_if(const variant<Ts...>* v) noexcept {
	return swl::get_if<Idx>(const_cast<variant<Ts...>*>(v));
}

// ====== get_if by type 

template <class T, class... Ts>
constexpr T* get_if(variant<Ts...>* v) noexcept {
	static_assert( (std::is_same_v<T, Ts> || ...), "Requested type is not contained in the variant" );
	return swl::get_if<vimpl::find_type<T, Ts...>()>(v);
}

template <class T, class... Ts>
constexpr const T* get_if(const variant<Ts...>* v) noexcept {
	return swl::get_if<T>( const_cast<variant<Ts...>*>(v) );
}

// =============================== visitation (20.7.7)

template <class Fn, class... Vs>
	requires (is_variant<Vs> && ...)
constexpr decltype(auto) visit(Fn&& fn, Vs&&... vars){
	if constexpr ( (std::decay_t<Vs>::can_be_valueless || ...) )
			if ( (vars.valueless_by_exception() || ...) ) 
				throw bad_variant_access{"swl::variant : Bad variant access in visit."};
				
	if constexpr (sizeof...(Vs) == 1){
		return [] (auto&& fn, auto&& head) { 
			return decltype(head)(head).visit(static_cast<Fn&&>(fn));
		} (static_cast<Fn&&>(fn), static_cast<Vs&&>(vars)...);
	}
	else {
		using namespace vimpl;
		constexpr unsigned max_size = (std::decay_t<Vs>::size * ...);
		using dispatcher_t = typename multi_dispatcher<sizeof...(Vs)>::template with_table_size<max_size>;
		const auto table_indice = flatten_indices<std::decay_t<Vs>::size...>(vars.index()...);
		return dispatcher_t::template impl<Fn&&, Vs&&...>[ table_indice ]( static_cast<Fn&&>(fn), static_cast<Vs&&>(vars)... );
	}
}

template <class R, class Fn, class... Vs>
	requires (is_variant<Vs> && ...)
constexpr R visit(Fn&& fn, Vs&&... vars){
	return visit(static_cast<Fn&&>(fn), static_cast<Vs&&>(vars)...);
}

// ============================== relational operators (20.7.6)

template <class... Ts>
	requires ( vimpl::has_eq_comp<Ts> && ... )
constexpr bool operator==(const variant<Ts...>& v1, const variant<Ts...>& v2){
	if (v1.index() != v2.index()) 
		return false;
	if constexpr (variant<Ts...>::can_be_valueless)
		if (v1.valueless_by_exception()) return true;
	return v2.visit_with_index( vimpl::eq_comp<const variant<Ts...>&>{v1} );
}

template <class... Ts>
constexpr bool operator!=(const variant<Ts...>& v1, const variant<Ts...>& v2)
	requires requires { v1 == v2; }
{
	return not(v1 == v2);
}

template <class... Ts>
	requires ( vimpl::has_lesser_comp<const Ts&> && ... )
constexpr bool operator<(const variant<Ts...>& v1, const variant<Ts...>& v2){
	if ( v1.index() == v2.index() ){
		if constexpr (variant<Ts...>::can_be_valueless){
			if (v2.valueless_by_exception()) return false;
			if (v1.valueless_by_exception()) return true;
		}
		return v1.visit_with_index( [&v2] (auto& elem, auto index) -> bool 
		{
			return (elem < v2.template get<index>());
		});
	}
	else
		return (v1.index() < v2.index());
}

template <class... Ts>
constexpr bool operator>(const variant<Ts...>& v1, const variant<Ts...>& v2){
	return v2 < v1;
}

template <class... Ts>
	requires ( vimpl::has_lesser_than_comp<const Ts&> && ... )
constexpr bool operator<=(const variant<Ts...>& v1, const variant<Ts...>& v2){
	if ( v1.index() == v2.index() ){
		if constexpr (variant<Ts...>::can_be_valueless){
			if (v2.valueless_by_exception()) return false;
			if (v1.valueless_by_exception()) return true;
		}
		return v1.visit_with_index( [&v2] (auto& elem, auto index) -> bool {
			return (elem <= v2.template get<index>());
		});
	}
	else
		return (v1.index() < v2.index());
}

template <class... Ts>
constexpr bool operator>=(const variant<Ts...>& v1, const variant<Ts...>& v2){
	return v2 <= v1;
}

// ===================================== monostate (20.7.8, 20.7.9)

struct monostate{};
constexpr bool operator==(monostate, monostate) noexcept { return true; }
constexpr bool operator> (monostate, monostate) noexcept { return false; }
constexpr bool operator< (monostate, monostate) noexcept { return false; }
constexpr bool operator<=(monostate, monostate) noexcept { return true; }
constexpr bool operator>=(monostate, monostate) noexcept { return true; }

// ===================================== specialized algorithms (20.7.10)

template <class... Ts>
void swap(variant<Ts...>& a, variant<Ts...>& b)
	noexcept(noexcept(a.swap(b)))
	requires requires { a.swap(b); }
{
	a.swap(b);
}

} // SWL

#ifdef SWL_CPP_VARIANT_USE_STD_HASH

// ====================================== hash support (20.7.12)

namespace std {
	template <class... Ts>
		requires (::swl::vimpl::has_std_hash<Ts> && ...)
	struct hash<::swl::variant<Ts...>> {
		std::size_t operator()(const ::swl::variant<Ts...>& v) const {
			return ::swl::visit(v, [] (auto& elem) {
				return std::hash<std::decay_t<decltype(elem)>>{}(elem);
			});
		}
	};

	template <>
	struct hash<::swl::monostate> {
		constexpr std::size_t operator()(::swl::monostate) const noexcept { return 0; }
	};
}

#endif // std-hash

#endif