#ifndef TDICT_HPP_
#define TDICT_HPP_

#include <ostream>
#include <map>
#include <string>
#include <sstream>
#include <pthread.h>
#include "params.h"
#include "timing.h"

/* Uncomment this flag if you believe that the fine-grain -T timings
 * negatively impact the performance */
#define xxxDISABLE_TIMINGS

#ifndef DISABLE_TIMINGS

/* This header file defines objects for a "timing dictionary".
 *
 *  - We define "timing slots", which are the main thrust of the idea.
 *  - Actual "timing dictionaries" are mainly std::map types using timing
 *    slots as keys.
 *  - We also define "timing dictionary sentries", which are convenience
 *    objects meant to record time spent within a given scope.
 *
 * Timing slots are print-capable objects, which can be manipulated like
 * plain integers, for speed. Each of these objects can be printed in a
 * per-object defined manner. The actual plain integer is proxied via the
 * tdict::key type, but that one really resolves to an integer.
 *
 * The translation from the timing slot object to an integer
 * (tdict::key) is done at compile time.
 *
 * The reverse translation from tdict::key to strings is done by
 * the standard output operators.
 *
 * Two types of tdict objects are defined by default.
 *
 * The class "tdict::basic" can be used for
 * slots with only one given meaning. These get declared as:
 *
 * tdict::basic ORANGES("number of oranges");
 * tdict::basic PEARS("number of pears");
 * map<tdict::key, int> tab;      // a "timing dictionary"
 * tab[ORANGES]++;
 * tab[PEARS]++;
 * tab[ORANGES]++;
 * tab[ORANGES]++;
 * for(auto const& a : tab) {
 *        cout << a.first << ": " << a.second << endl;
 * }
 *
 * The second class "tdict::slot_parametric" takes a paramter and one
 * (or optionally two) strings at construction time. The printed version
 * is [first string][parameter][second string].
 *
 * tdict::slot_parametric GRAPE("grape with ", " seeds");
 *
 * To actually convert that into a key, you need to provide the
 * slot_parametric object with one argument, as in:
 *
 * tab[GRAPE(2)]++
 *
 * Behind the scenes, tdict::key actually encodes both the object
 * unique identifier as well as a parameter in the integer key. As it is
 * currently written (all in the tdict::key type), 16 bits are for
 * the tdict object identifier, and 16 for the parameter.
 *
 * Lifetime of tdict objects is understood as being global.
 * However, scope-limited is also ok. Upon destruction, the slot is kept
 * in the global tdict registry (to guarantee global uniqueness),
 * so use with extreme care. Also, having it scope-limited is not thread-safe.
 */
namespace tdict {

    extern int global_enable;

    /* to print a key object (e.g. from gdb) use
     * tdict::slot_base::print(k)
     */
    class key {
        int magic;
        public:
        key(){}
        int dict_key() const { return magic >> 16; }
        int parameter() const { return magic & 65535; }
        key encode(int arg) const { key res; res.magic = magic + arg; return res;}
        key(int a) { magic = a << 16; }
        friend bool operator<(key const& o1, key const& o2);
    };
    inline bool operator<(key const& o1, key const& o2) { return o1.magic < o2.magic; }
    class slot_base {

        slot_base(slot_base const&) {} /* prevent copy */
        public:
        typedef std::map<key, const tdict::slot_base*> dict_t;
        protected:
        key k;
        private:
        static dict_t& get_dict(int x MAYBE_UNUSED = 0) {
            /* The code below leaks, I know. Unfortunately I can't stow
             * the static member initialization in an other compilation
             * unit, or SIOF will kill me. See also "Meyers Singleton".
             *
             * #else branch is an ad hoc hack which kinda works here.
             * We destroy the singleton on the last tdict::slot_base
             * destructor.  It's not ideal, since we really really must
             * make sure the tdict::key objects never escape the
             * scope of existence of the associated tdict::slot_base
             * object themselves -- which is not guaranteed by the
             * interface.
             */
#if 1
            static dict_t d;   /* trusty leaky */
            return d;
#else
            static dict_t * d;
            static size_t nkeys;
            if (x > 0) {
                if (nkeys++ == 0) {
                    d = new dict_t();
                }
            } else if (x < 0) {
                if (--nkeys == 0) {
                    delete d;
                    d = NULL;
                }
            }
            return d;
#endif
        };
        static pthread_mutex_t m;
        static void lock(){pthread_mutex_lock(&m);}
        static void unlock(){pthread_mutex_unlock(&m);}
        public:
        // helgrind complains, here. I think that helgrind is wrong.
        // key base_key() const { lock(); key ret = k; unlock(); return ret; }
        key const & base_key() const { return k; }
        slot_base() {
            lock();
            dict_t& dict(get_dict(1));
            k = key(dict.size());
            dict[k.dict_key()] = this;
            unlock();
        }
        ~slot_base() {
            lock();
            dict_t& dict(get_dict());
            dict[k] = NULL;
            get_dict(-1);
            unlock();
        }
        public:
        static std::string print(key x) {
            lock();
            dict_t& dict(get_dict());
            dict_t::const_iterator it = dict.find(x.dict_key());
            if (it == dict.end()) {
                unlock();
                throw "Bad magic";
            }
            const tdict::slot_base * b = it->second;
            if (b == NULL) {
                unlock();
                return "FIXME: deleted timer";
            }
            unlock();
            return b->_print(x.parameter());
        }
        virtual std::string _print(int) const = 0;
    };
    inline std::ostream& operator<<(std::ostream& o, key const& k) {
        return o << slot_base::print(k);
    }

    class slot : public slot_base {
        std::string text;
        public:
        slot(std::string const& s) : text(s) {}
        virtual std::string _print(int) const { return text; }
        operator key() const { return base_key(); }
    };

    class slot_parametric : public slot_base {
        std::string s,t;
        public:
        slot_parametric(std::string const& s) : s(s) { }
        slot_parametric(std::string const& s, std::string const& t) : s(s), t(t) { }
        key operator()(int p) const {
            return base_key().encode(p);
        }
        virtual std::string _print(int p) const {
            std::ostringstream ss;
            ss << s << p << t;
            return ss.str();
        }
    };

    struct timer_seconds_thread {
        typedef double type;
        type operator()() const {
            if (tdict::global_enable)
                return seconds_thread();
            else
                return 0;
        }
    };
    struct timer_seconds_thread_and_wct {
        struct type {
            double t = 0;
            double w = 0;
            type(type const&) = default;
            type(type&&) = default;
            type() = default;
            type(int) : t(0), w(0) {}
            type(double t, double w) : t(t), w(w) {}
            type& operator-=(type const & o) {
                t-=o.t;
                w-=o.w;
                return *this;
            }
            type& operator+=(type const & o) {
                t+=o.t;
                w+=o.w;
                return *this;
            }
            bool operator>(double const& c) const {
                return w > c;
            }
        };
        type operator()() const {
            if (tdict::global_enable)
                return type(seconds_thread(), wct_seconds());
            else
                return type();
        }
    };
    std::ostream& operator<<(std::ostream & o, timer_seconds_thread_and_wct::type const & a);


    /*
    template<typename T>
        class sentry {
            std::map<key, timer_data_type> & m;
            key k;
            public:
            sentry(std::map<key, timer_data_type> & m, key const& k) : k(k), m(m) {
                m[k] -= T()();
            }
            ~sentry() {
                m[k] += T()();
            }
        };
        */

    template<typename T>
        struct tree {
            typedef typename T::type timer_data_type;
            timer_data_type self;
            bool scoping;
            int category;
            typedef std::map<tdict::key, tree<T> > M_t;
            M_t M;
            tree<T> * current;   /* could be NULL */
            tree<T> * parent;   /* could be NULL */
            tree() : self(timer_data_type()), scoping(true), category(-1), current(NULL), parent(this) { }
            bool running() const { return current != NULL; }
            void stop() {
                if (!running()) return;
                timer_data_type v = T()();
                current->self += v;
                current = NULL;
                return;
            }
            void start() {
                if (running()) return;
                timer_data_type v = T()();
                self -= v;
                current = this;
            }
            void set_current_category(int c) {
                ASSERT_ALWAYS(running());
                /* It is not allowed to set a category for the root of
                 * the tree */
                ASSERT_ALWAYS(current != this);
                current->category = c;
            }


            timer_data_type stop_and_start() {
                ASSERT_ALWAYS(running());
                timer_data_type v = T()();
                timer_data_type res = current->self + v;
                current->self = -v;
                return res;
            }
            struct accounting_base {
                tree& t;
                inline accounting_base(tree& t): t(t) {}
                inline ~accounting_base() {}
            };
            /* This one is useful so that ctor/dtor order works right.
             */
            struct accounting_activate : public accounting_base {
                inline accounting_activate(tree& t): accounting_base(t) { accounting_base::t.start(); }
                inline ~accounting_activate() { accounting_base::t.stop(); }
            };
            struct accounting_activate_recursive : public accounting_base {
                bool act = false;
                inline accounting_activate_recursive(tree& t): accounting_base(t), act(!t.running()) { if (act) accounting_base::t.start(); }
                inline ~accounting_activate_recursive() { if (act) accounting_base::t.stop(); }
            };
            template<typename BB>
            struct accounting_child_meta : public BB {
                accounting_child_meta(tree& t, tdict::key k): BB(t) {
                    ASSERT_ALWAYS(BB::t.running());
                    timer_data_type v = T()();
                    BB::t.current->self += v;
                    tree<T> * kid = &(BB::t.current->M[k]);  /* auto-vivifies */
                    kid->parent = BB::t.current;
                    kid->self -=  v;
                    kid->scoping = true;
                    BB::t.current = kid;
                }
                ~accounting_child_meta() {
                    timer_data_type v = T()();
                    BB::t.current->self += v;
                    /* It could be that we are one level below. */
                    for(;!BB::t.current->scoping;) {
                        BB::t.current = BB::t.current->parent;
                    }
                    BB::t.current = BB::t.current->parent;
                    BB::t.current->self -= v;
                }
            };
            typedef accounting_child_meta<accounting_base> accounting_child;
            typedef accounting_child_meta<accounting_activate> accounting_child_autoactivate;
            typedef accounting_child_meta<accounting_activate_recursive> accounting_child_autoactivate_recursive;

            struct accounting_debug : public accounting_base {
                std::ostream& o;
                inline accounting_debug(tree& t, std::ostream&o): accounting_base(t), o(o) {}
                inline ~accounting_debug() {
                    o << "# debug print\n";
                    o << accounting_base::t.display();
                    o << "# --\n";
                }
            };

            /* We make this an object for consistency with the child case, but
             * really we don't have to */
            struct accounting_sibling {
                accounting_sibling(tree& t, tdict::key k) {
                    ASSERT_ALWAYS(t.running());
                    timer_data_type v = T()();
                    tree<T> * kid;
                    if (t.current->scoping) {
                        kid = &(t.current->M[k]);  /* auto-vivifies */
                        kid->parent = t.current;
                    } else {
                        kid = &(t.current->parent->M[k]);  /* auto-vivifies */
                        kid->parent = t.current->parent;
                    }
                    t.current->self += v;
                    kid->scoping = false;
                    kid->self -=  v;
                    t.current = kid;
                }
            };
            /* mostly the same as the previous, except that we return to
             * the main "bookkeeping" timer attached to this level of the
             * tree.
             */
            struct accounting_bookkeeping {
                accounting_bookkeeping(tree& t) {
                    ASSERT_ALWAYS(t.running());
                    timer_data_type v = T()();
                    if (!t.current->scoping) {
                        t.current->self += v;
                        t.current = t.current->parent;
                        t.current->self -= v;
                    }
                }
            };

private:
            std::ostream& _display(std::ostream& o, std::string const& prefix) const {
                // o << prefix << self << " (self)\n";
                std::ostringstream ss;
                ss << prefix << " ";
                for(typename M_t::const_iterator a = M.begin() ; a != M.end() ; a++) {
                    o << prefix << a->second.self << " " << a->first;
#define DEBUG_CATEGORY
#ifdef DEBUG_CATEGORY
                    if (a->second.category >= 0)
                        o << " ; category " << a->second.category;
#endif
                    o << "\n";
                    a->second._display(o, ss.str());
                }
                return o;
            }

            void filter_by_category(std::map<int, timer_data_type> & D, int inherited) const {
                int flag = inherited;
                if (category >= 0)
                    flag = category;
                D[flag] += self;
                for(typename M_t::const_iterator a = M.begin() ; a != M.end() ; a++) {
                    a->second.filter_by_category(D, flag);

                }
            }

public:
            std::map<int, timer_data_type> filter_by_category() const {
                std::map<int, timer_data_type> res;
                filter_by_category(res, -1);
                return res;
            }

            std::string display(double bookkeeping_cutoff = 1e-5) const {
                ASSERT_ALWAYS(!running());
                std::ostringstream ss;
                if (self > bookkeeping_cutoff)
                    ss << "# " << self << " (bookkeeping)\n";
                _display(ss, "# ");
                return ss.str();
            }
            tree& operator+=(tree const& t) {
                self += t.self;
                ASSERT_ALWAYS(category < 0 || t.category < 0 || category == t.category);
                if (t.category >= 0)
                    category = t.category;
                for(typename M_t::const_iterator a = t.M.begin() ; a != t.M.end() ; a++) {
                    M[a->first] += a->second;
                }
                return *this;
            }
            tree& steal_children_timings(tree & t) {
                ASSERT_ALWAYS(t.running());
                ASSERT_ALWAYS(t.current = &t);
                ASSERT_ALWAYS(t.category < 0);
                for(typename M_t::iterator a = t.M.begin() ; a != t.M.end() ; a++) {
                    M[a->first] += a->second;
                }
                t.M.clear();
                return *this;
            }
        };

    void declare_usage(cxx_param_list & pl);
    void configure_switches(cxx_param_list & pl);
    void configure_aliases(cxx_param_list & pl);
};

// timer_seconds_thread_and_wct is not satisfactory.
// typedef tdict::tree<tdict::timer_seconds_thread_and_wct> timetree_t;
typedef tdict::tree<tdict::timer_seconds_thread> timetree_t;

#if 0

// an example. In fact this one is already covered by tdict::parametric

/* This is an anonymous class, intentionally. We have no use for the
 * class name. The object is the whole story. The object registers with
 * the global tdict layer, and gets a unique key. Eventually, how
 * the object reacts to operator() to encode its arguments in its 16-bit
 * value space is really what we're interested in.
 */

class : public tdict::slot_base {
    public:
        tdict::key operator()(int p) const { return k.encode(p); }
        virtual std::string _print(int p) const {
            std::ostringstream ss;
            ss << "inner loop with " << p << " legs";
            return ss.str();
        }
} TT_INNER_LOOP;
#endif

#define UNIQUE_ID(t) CADO_CONCATENATE3(uid_,t,__LINE__)

/* Note that in most cases we *can't* play do-while(0) here, because that
 * would scope the timer object, which is precisely what we want to
 * avoid. In cases where the dtor is trivial, we can, since it makes no
 * difference.
 */
#define CHILD_TIMER(T, name)                                            \
        static tdict::slot UNIQUE_ID(slot)(name);		       	\
        timetree_t::accounting_child UNIQUE_ID(sentry)(T,UNIQUE_ID(slot))
#define CHILD_TIMER_PARAMETRIC(T, name, arg, suffix)                    \
        static tdict::slot_parametric UNIQUE_ID(slot)(name, suffix);\
        timetree_t::accounting_child UNIQUE_ID(sentry)(T,UNIQUE_ID(slot)(arg))
#define SIBLING_TIMER(T, name) do {				        \
        static tdict::slot x(name);			        	\
        timetree_t::accounting_sibling UNIQUE_ID(sentry) (T,x);         \
    } while (0)
#define SIBLING_TIMER_PARAMETRIC(T, name, arg, suffix) do {	        \
        static tdict::slot_parametric x(name, suffix);                  \
        timetree_t::accounting_sibling UNIQUE_ID(sentry) (T,x(arg));    \
    } while (0)
#define BOOKKEEPING_TIMER(T)						\
    timetree_t::accounting_bookkeeping UNIQUE_ID(sentry) (T);
#define ACTIVATE_TIMER(T)						\
    timetree_t::accounting_activate UNIQUE_ID(sentry) (T);
#define ACTIVATE_TIMER_IF_NOT_RUNNING(T)				\
    timetree_t::accounting_activate_recursive UNIQUE_ID(sentry) (T);
#define DEBUG_DISPLAY_TIMER_AT_DTOR(T,o)				\
    timetree_t::accounting_debug UNIQUE_ID(sentry) (T, o);


#else /* DISABLE_TIMINGS */

struct timetree_t {
    typedef double timer_data_type;
    std::map<int, double> filter_by_category() const {
        /* always an empty map */
        return std::map<int, double>();
    }
    void filter_by_category(std::map<int, double> &, int) const { }
    timetree_t& operator+=(timetree_t const&) { return *this; }
    std::string display() const { return std::string(); }
    /* what should we do */
    bool running() const { return false; }
    inline void nop() const {}
    inline void start() const {}
    inline void stop() const {}
};

namespace tdict {
    extern int global_enable;
    void declare_usage(cxx_param_list & pl);
    void configure_switches(cxx_param_list & pl);
    void configure_aliases(cxx_param_list & pl);
};

#define CHILD_TIMER(T, name) T.nop()
#define CHILD_TIMER_PARAMETRIC(T, name, arg, suffix) T.nop()
#define SIBLING_TIMER(T, name) T.nop()
#define SIBLING_TIMER_PARAMETRIC(T, name, arg, suffix) T.nop()
#define BOOKKEEPING_TIMER(T) T.nop()
#define ACTIVATE_TIMER(T) T.nop()
#define DEBUG_DISPLAY_TIMER_AT_DTOR(T,o) T.nop()

#endif

#endif	/* TDICT_HPP_ */
