#ifndef INDEXED_PRIORITY_QUEUE_HPP_
#define INDEXED_PRIORITY_QUEUE_HPP_

#include <algorithm>
#include <functional>
#include <vector>
#include <cassert>
#include <type_traits>
#include <map>
 
/* This template class is softly modeled after the C++98 priority_queue
 * (with some of its C++11-added goodies), but it also diverts
 * significantly.
 *
 * This priority queue is mutable and indexable.
 *
 * In its default flavour, this type assumes that the keys to be
 * prioritized live in a contiguous index space, so that we can use a
 * vector to remember stuff about them. The first description below
 * is based on this default flavour.
 *
 * An indexed priority queue retains the capability to access elements by
 * the index from which they were added in the first place, and to modify
 * the associated value if desired.
 *
 * An element in the queue Q is a pair <j,v>. v is the data which is used
 * to order the queue. j is the original insertion index of v (however v
 * might have been modified since insertion).
 *
 * Also, given an original insertion index j, Q.update(j, v) modifies the
 * value with index j, and reorders Q accordingly.
 *
 * A typical operation may be (assuming the comparison takes the max,
 * which is the default behaviour):
 *
 *   indexed_priority_queue<int, int> q;
 *   q.push(17);        // at index 0
 *   q.push(42);        // at index 1
 *   q.push(11);        // at index 2
 *   q.push(59);        // at index 3
 *   q.update(2, 20);   // changes value 11 to 20
 *   q.top();           // return pair(3, 59)
 *   q.pop();           // chops off the top
 *
 * When push() operations are used after previous pop() operations, the
 * insertion index which is taken into account for the inserted value is
 * the same as if no pop() had taken place.
 *
 *
 * Alternatively, we also define a sparse_indexed_priority_queue, where
 * the keys to be prioritized are not supposed to be contiguous. The
 * requirement on the index arrays is more pressing. Some prototypes are
 * changed. See tests/utils/test_indexed_priority_queue.cpp
 */
template<
    typename KeyType,
    typename PriorityType,
    class Compare = std::less<PriorityType>,
    typename SizeType = size_t,
    class IndexContainerType = std::vector<SizeType>
    >
struct indexed_priority_queue {
    typedef KeyType key_type;
    typedef PriorityType priority_type;
    typedef std::pair<key_type, priority_type> value_type;
    typedef value_type & reference;
    typedef value_type const & const_reference;
    typedef std::vector<std::pair<KeyType, PriorityType>> value_container_type;
    typedef IndexContainerType index_container_type;
    typedef SizeType size_type;
    typedef Compare value_compare;
protected:
    struct inner_comp {
        value_compare comp;
        inner_comp(value_compare const& comp) : comp(comp) {}
        bool operator()(const_reference a, const_reference b) const {
            return comp(a.second, b.second);
        }
    };

    inner_comp comp;
    value_container_type values;
    index_container_type indices;

public:
    /* default copy, swap, and assignment operators are fine */
    indexed_priority_queue(const value_compare& compare = value_compare()) : comp(compare) {}

protected:
    inline bool _check(size_type j) const { return indices.at(values[j].first) == j; }
    inline void _fixup(size_type j) { indices[values[j].first] = j; }
    inline void _swap(size_type i, size_type j) {
        assert(_check(i));
        assert(_check(j));
        swap(values[i], values[j]);
        _fixup(i);
        _fixup(j);
    }
    inline void _swap_hole(size_type i, size_type j) {
        /* i must point to a hole ! */
        assert(_check(j));
        values[i] = values[j];
        _fixup(i);
    }

    /* T[hole] is irrelevant, but the subtree below it (within T[0..n[)
     * is a heap. We move the irrelevant cell to a leaf of the (sub-)
     * tree.
     * 
     * Note that n might be less than size()
     */
    size_type _prepare_insert_down(size_type hole, size_type n, const_reference v)
    {
        /* main case as long as there is a right child */
        for(; hole < (n-1) / 2 ; ) {
            /* note that hole < (n-1) / 2 implies hole <= (n-1) / 2 - 1
             * and hence 2*hole+2 <= n-1 */
            size_type left = 2 * hole + 1;
            size_type right = 2 * hole + 2;
            /* which one has the largest value ? */
            size_type next = (comp(values[left], values[right])) ? right : left;
            /* maybe v can be the hole */
            if (comp(values[next], v))
                return hole;
            _swap_hole(hole, next);
            hole = next;
        }
        /* maybe we breaked with a left child ? */
        if (2 * hole + 1 == n - 1) {
            size_type next = 2 * hole + 1;
            if (comp(values[next], v))
                return hole;
            _swap_hole(hole, next);
            hole = next;
        }
        return hole;
    }

    /* we wish to insert v into the tree, which is a heap, the hole
     * pointing to a leaf. Return a hole where the value should be
     * emplaced so that the heap property is maintained.
     */
    size_type _prepare_insert_up(size_type hole, const_reference v)
    {
        size_type next = (hole - 1) / 2;
        for(; hole && comp(values[next], v) ; ) {
            _swap_hole(hole, next);
            hole = next;
            next = (hole - 1) / 2;
        }
        return hole;
    }

    void _make_heap() {
        size_type n = size();
        for(size_type i = 1 ; i < n ; i++) {
            value_type saved = values[i];
            size_type hole = _prepare_insert_up(i, saved);
            values[hole] = saved;
            _fixup(hole);
        }
    }

#ifdef DEBUG_INDEXED_PRIORITY_QUEUE
    std::ostream& _print(std::ostream& o, std::string const & prefix, size_type i, size_type n) const {
        o << prefix << "[" << i << "] [col " << values[i].first << ", weight " << values[i].second << "]\n";
        assert(_check(i));
        std::string npref = prefix + " ";
        if (2 * i + 1 < n)
        _print(o, npref, 2*i+1, n);
        if (2 * i + 2 < n)
        _print(o, npref, 2*i+2, n);
        return o;
    }
#endif

public:
    inline const_reference top() const { return values.front(); }
    inline bool empty() const { return values.empty(); }
    inline size_type size() const { return values.size(); }

    void pop() {
        /* we swap elements [0] and [n-1], and then make
         * values[0..n-1[ into a heap. */
        assert(size());
        size_type n = size() - 1;
        assert(_check(n));
        value_type saved = values[n];
        if (!n) {
            values.pop_back();
            return;
        }
        _swap_hole(n, 0);
        values.pop_back();

        /* first step: T[0] is irrelevant, but apart from that cell,
         * T[0..n[ is a well-behaved heap. We move the irrelevant cell to
         * a leaf of the heap.
         */
        size_type hole = _prepare_insert_down(0, n, saved);

        /* last step: finally do the insertion */
        values[hole] = saved;
        _fixup(hole);
    }
    inline void update(size_type j, const priority_type & v) { update(std::make_pair(j,v)); }
    void update(const value_type& a) {
        size_type hole = indices.at(a.first);
        if (!hole || !comp(values[(hole-1)/2], a)) {
            hole = _prepare_insert_down(hole, size(), a);
        } else {
            hole = _prepare_insert_up(hole, a);
        }
        values[hole] = a;
        _fixup(hole);
    }

    bool is_heap(size_type base = 0) {
        size_type left = 2 * base + 1;
        size_type right = 2 * base + 2;
        if (left >= size()) return true;
        if (!comp(values[left], values[base])) return false;
        if (!is_heap(left)) return false;
        if (right >= size()) return true;
        if (!comp(values[right], values[base])) return false;
        return is_heap(right);
    }

private:
    /* We finish with a few functions which are special-cased depending
     * on whether we have a vector or a map for the table of indices.
     *
     * Because that's the way enable_if works, we have to make all of
     * these functions (albeit artificially) dependent on a template
     * parameter so that we can leverage the trick of template argument
     * deduction.
     */
    template<class T> struct Void { typedef void type; };
    template<class T, class U = void> struct has_mapped_type { enum { value = 0 }; };
    template<class T> struct has_mapped_type<T, typename Void<typename T::mapped_type>::type > { enum { value = 1 }; };


    template<bool, typename Then, typename Else> struct checkif { };
    template<typename Then, typename Else> struct checkif<true, Then, Else> : public Then {};
    template<typename Then, typename Else> struct checkif<false, Then, Else> : public Else {};

    struct requirements_for_sequence_index_container {
        static_assert(std::is_same<typename index_container_type::value_type, SizeType>::value,
            "when index container is a sequence container,"
            " we must have value_type=SizeType");
        typedef void type;
    };

    struct requirements_for_associative_index_container {
        static_assert(std::is_same<typename index_container_type::key_type, KeyType>::value,
                "when index container is an associative container,"
                " we must have key_type=KeyType");
        static_assert(std::is_same<typename index_container_type::mapped_type, SizeType>::value,
                "when index container is an associative container,"
                " we must have mapped_type=SizeType");
        typedef void type;
    };

    typedef typename checkif<
        has_mapped_type<index_container_type>::value,
        requirements_for_associative_index_container,
        requirements_for_sequence_index_container>::type requirements_are_met;


#define ONLY_FOR_SEQUENCE_INDEX(T) \
    , typename std::enable_if<!(has_mapped_type<T>::value)>::type * = 0
#define ONLY_FOR_ASSOCIATIVE_INDEX(T) \
    , typename std::enable_if<has_mapped_type<T>::value>::type * = 0

public:
    template<class RandomAccessIterator, typename T = index_container_type>
    indexed_priority_queue(
            RandomAccessIterator first,
            RandomAccessIterator last,
            const Compare& compare = Compare()
            ONLY_FOR_SEQUENCE_INDEX(T)
            )
    : comp(compare)
    {
        indices.assign(last-first, 0);
        values.reserve(last-first);
        for(RandomAccessIterator i = first ; i != last ; ++i) {
            values.push_back(std::make_pair(i-first, *i));
        }
        _make_heap();
        for(auto const& x : values) {
            indices[x.first] = &x - &values.front();
        }
    }

    template<typename T = index_container_type>
    void push(const priority_type & x ONLY_FOR_SEQUENCE_INDEX(T)) {
        size_type n = indices.size();
        value_type v = make_pair(n, x);
        values.push_back(value_type());
        indices.push_back(size_type());
        size_type hole = _prepare_insert_up(n, v);
        values[hole] = v;
        _fixup(hole);
    }
#ifdef DEBUG_INDEXED_PRIORITY_QUEUE
    template<typename T = index_container_type>
    std::ostream& print(std::ostream& o ONLY_FOR_SEQUENCE_INDEX(T)) const {
        _print(o, std::string(), 0, size());
        o << "col positions\n";
        for(size_type i = 0 ; i < size() ; i++) {
            o << "col " << i << " at position " << indices[i] << "\n";
        }
        return o;
    }
#endif
public:
    template<class InputIterator, typename T = index_container_type>
    indexed_priority_queue(
            InputIterator first,
            InputIterator last,
            const Compare& compare = Compare()
            ONLY_FOR_ASSOCIATIVE_INDEX(T)
            )
    : comp(compare)
    {
        for(InputIterator i = first ; i != last ; ++i) {
            indices[i->first] = values.size();
            values.push_back(std::make_pair(i->first, i->second));
        }
        _make_heap();
    }

    template<typename T = index_container_type>
    void push(const value_type& x ONLY_FOR_ASSOCIATIVE_INDEX(T)) {
        assert(indices.find(x.first) == indices.end());
        values.push_back(value_type());
        // indices[x.first] will be set below
        size_type hole = _prepare_insert_up(values.size()-1, x);
        values[hole] = x;
        _fixup(hole);
    }
#ifdef DEBUG_INDEXED_PRIORITY_QUEUE
    template<typename T = index_container_type>
    std::ostream& print(std::ostream& o ONLY_FOR_ASSOCIATIVE_INDEX(T)) const {
        _print(o, std::string(), 0, size());
        o << "col positions\n";
        for(auto const& x : indices) {
            o << "col " << x.first << " at position " << x.second << "\n";
        }
        return o;
    }
#endif


#undef ONLY_FOR_SEQUENCE_INDEX
#undef ONLY_FOR_ASSOCIATIVE_INDEX
};

#ifdef DEBUG_INDEXED_PRIORITY_QUEUE
template<
    typename KeyType,
    typename PriorityType,
    class Compare,
    typename SizeType,
    class IndexContainerType>
std::ostream& operator<<(std::ostream& o, indexed_priority_queue<KeyType, PriorityType, Compare, SizeType, IndexContainerType> const & Q)
{
        return Q.print(o);
}
#endif

template<
    typename KeyType,
    typename PriorityType,
    class Compare = std::less<PriorityType>,
    typename SizeType = size_t,
    typename IndexContainerType = std::map<KeyType, SizeType>
    >
struct sparse_indexed_priority_queue : public indexed_priority_queue<KeyType, PriorityType, Compare, SizeType, IndexContainerType> {};


#endif	/* INDEXED_PRIORITY_QUEUE_HPP_ */
