#define DEBUG_STREAM_ENABLED 0

#include "SMTSubsumption.hpp"
#include "SubstitutionTheory.hpp"
#include "SMTSubsumption/minisat/Solver.h"
#include "Indexing/LiteralMiniIndex.hpp"
#include "Lib/STL.hpp"
#include "Kernel/Matcher.hpp"
#include "Kernel/MLMatcher.hpp"
#include "Kernel/ColorHelper.hpp"

#include <chrono>
#include <iostream>
#include <iomanip>

using namespace Indexing;
using namespace Kernel;
using namespace SMTSubsumption;

#include "SMTSubsumption/cdebug.hpp"



template <typename Duration>
vstring fmt_microsecs(Duration d) {
  std::uint64_t microsecs = std::chrono::duration_cast<std::chrono::microseconds>(d).count();
  vstringstream s;
  s << std::setw(10) << microsecs << " [µs]";
  return s.str();
}

template <typename Duration>
vstring fmt_nanosecs(Duration d) {
  std::uint64_t nanosecs = std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
  vstringstream s;
  s << std::setw(10) << nanosecs << " [ns]";
  return s.str();
}


// namespace {
//   typedef DHMap<unsigned,TermList,IdentityHash> BindingMap;
//   struct MapBinder
//   {
//     bool bind(unsigned var, TermList term)
//     {
//       TermList* aux;
//       return _map.getValuePtr(var,aux,term) || *aux==term;
//     }
//     void specVar(unsigned var, TermList term)
//     { ASSERTION_VIOLATION; }
//     void reset() { _map.reset(); }
//   private:
//     BindingMap _map;
//   };
// }


/// Possible match alternative for a certain literal of the side premise.
struct Alt
{
  Literal* lit;  // the FOL literal
  unsigned j;    // index of lit in the main_premise
  Minisat::Var b;  // the b_{ij} representing this choice in the SAT solver
  bool reversed;
};













/****************************************************************************
 * Original subsumption implementation (from ForwardSubsumptionAndResolution)
 ****************************************************************************/

namespace OriginalSubsumption {


struct ClauseMatches {
  CLASS_NAME(OriginalSubsumption::ClauseMatches);
  USE_ALLOCATOR(ClauseMatches);

private:
  //private and undefined operator= and copy constructor to avoid implicitly generated ones
  ClauseMatches(const ClauseMatches&);
  ClauseMatches& operator=(const ClauseMatches&);
public:
  ClauseMatches(Clause* cl) : _cl(cl), _zeroCnt(cl->length())
  {
    unsigned clen=_cl->length();
    _matches=static_cast<LiteralList**>(ALLOC_KNOWN(clen*sizeof(void*), "Inferences::ClauseMatches"));
    for(unsigned i=0;i<clen;i++) {
      _matches[i]=0;
    }
  }
  ~ClauseMatches()
  {
    unsigned clen=_cl->length();
    for(unsigned i=0;i<clen;i++) {
      LiteralList::destroy(_matches[i]);
    }
    DEALLOC_KNOWN(_matches, clen*sizeof(void*), "Inferences::ClauseMatches");
  }

  void addMatch(Literal* baseLit, Literal* instLit)
  {
    addMatch(_cl->getLiteralPosition(baseLit), instLit);
  }
  void addMatch(unsigned bpos, Literal* instLit)
  {
    if(!_matches[bpos]) {
      _zeroCnt--;
    }
    LiteralList::push(instLit,_matches[bpos]);
  }
  void fillInMatches(LiteralMiniIndex* miniIndex)
  {
    unsigned blen=_cl->length();

    for(unsigned bi=0;bi<blen;bi++) {
      LiteralMiniIndex::InstanceIterator instIt(*miniIndex, (*_cl)[bi], false);
      while(instIt.hasNext()) {
	Literal* matched=instIt.next();
	addMatch(bi, matched);
      }
    }
  }
  bool anyNonMatched() { return _zeroCnt; }

  Clause* _cl;
  unsigned _zeroCnt;
  LiteralList** _matches;

  class ZeroMatchLiteralIterator
  {
  public:
    ZeroMatchLiteralIterator(ClauseMatches* cm)
    : _lits(cm->_cl->literals()), _mlists(cm->_matches), _remaining(cm->_cl->length())
    {
      if(!cm->_zeroCnt) {
	_remaining=0;
      }
    }
    bool hasNext()
    {
      while(_remaining>0 && *_mlists) {
	_lits++; _mlists++; _remaining--;
      }
      return _remaining;
    }
    Literal* next()
    {
      _remaining--;
      _mlists++;
      return *(_lits++);
    }
  private:
    Literal** _lits;
    LiteralList** _mlists;
    unsigned _remaining;
  };
};


class OriginalSubsumptionImpl
{
  private:
    Kernel::MLMatcher matcher;
  public:
    bool checkSubsumption(Clause* side_premise, Clause* main_premise)
    {
      Clause* mcl = side_premise;
      Clause* cl = main_premise;
      LiteralMiniIndex miniIndex(cl);  // TODO: to benchmark forward subsumption, we might want to move this to the benchmark setup instead? as the work may be shared between differed side premises.

      unsigned mlen=mcl->length();
      ASS_G(mlen,1);   // (not really necessary for the benchmarks)

      ClauseMatches* cms = new ClauseMatches(mcl);
      cms->fillInMatches(&miniIndex);

      if(cms->anyNonMatched()) {
	return false;
      }

      matcher.init(mcl, cl, cms->_matches, true);

      bool isSubsumed =
        matcher.nextMatch()
        && ColorHelper::compatible(cl->color(), mcl->color());

      return isSubsumed;
    }
};
using Impl = OriginalSubsumptionImpl;  // shorthand if we use qualified namespace



}  // namespace OriginalSubsumption

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/















/****************************************************************************
 * SMT-Subsumption for benchmark
 ****************************************************************************/


class SMTSubsumptionImpl
{
  private:
    Minisat::Solver solver;
  public:
    // TODO
    bool checkSubsumption(Kernel::Clause* side_premise, Kernel::Clause* main_premise)
    {
      // solver.reset();  // TODO

      // Pre-matching
      // Determine which literals of the side_premise can be matched to which
      // literals of the main_premise when considered on their own.
      // Along with this, we create variables b_ij and the mapping for substitution
      // constraints.
      vvector<vvector<Alt>> alts;
      alts.reserve(side_premise->length());

      // for each instance literal (of main_premise),
      // the possible variables indicating a match with the instance literal
      vvector<vvector<Minisat::Var>> possible_base_vars;
      // start with empty vector for each instance literal
      possible_base_vars.resize(main_premise->length());

      SubstitutionTheoryConfiguration stc;

      for (unsigned i = 0; i < side_premise->length(); ++i) {
        Literal* base_lit = side_premise->literals()[i];

        vvector<Alt> base_lit_alts;

        // TODO: use LiteralMiniIndex here (need to extend InstanceIterator to a version that returns the binder)
        // LiteralMiniIndex::InstanceIterator inst_it(main_premise_mini_index, base_lit, false);
        for (unsigned j = 0; j < main_premise->length(); ++j) {
          Literal* inst_lit = main_premise->literals()[j];

          if (!Literal::headersMatch(base_lit, inst_lit, false)) {
            continue;
          }

          MapBinder binder;

          binder.reset();
          if (base_lit->arity() == 0 || MatchingUtils::matchArgs(base_lit, inst_lit, binder)) {
            Minisat::Var b = solver.newVar();

            if (binder.bindings().size() > 0) {
              ASS(!base_lit->ground());
              auto atom = SubstitutionAtom::from_binder(binder);
              stc.register_atom(b, std::move(atom));
            } else {
              ASS(base_lit->ground());
              ASS_EQ(base_lit, inst_lit);
              // TODO: in this case, at least for subsumption, we should skip this base_lit and this inst_list.
              // probably best to have a separate loop first that deals with ground literals? since those are only pointer equality checks.
              //
              // For now, just register an empty substitution atom.
              auto atom = SubstitutionAtom::from_binder(binder);
              stc.register_atom(b, std::move(atom));
            }

            base_lit_alts.push_back({
              .lit = inst_lit,
                .j = j,
                .b = b,
                .reversed = false,
            });
            possible_base_vars[j].push_back(b);
          }

          if (base_lit->commutative()) {
            ASS_EQ(base_lit->arity(), 2);
            ASS_EQ(inst_lit->arity(), 2);
            binder.reset();
            if (MatchingUtils::matchReversedArgs(base_lit, inst_lit, binder)) {
              auto atom = SubstitutionAtom::from_binder(binder);

              Minisat::Var b = solver.newVar();
              stc.register_atom(b, std::move(atom));

              // Minisat::Var b = solver.newVar();
              base_lit_alts.push_back({
                .lit = inst_lit,
                  .j = j,
                  .b = b,
                  .reversed = true,
              });
              possible_base_vars[j].push_back(b);
            }
          }
        }
        alts.push_back(std::move(base_lit_alts));
      }

      solver.setSubstitutionTheory(std::move(stc));

      // Pre-matching done
      for (auto const& v : alts) {
        if (v.empty()) {
          // There is a base literal without any possible matches => abort
          return false;
        }
      }

#define USE_ATMOSTONE_CONSTRAINTS 1

      // TODO: for these we can add special constraints to MiniSAT! see paper for idea
      // Add constraints:
      // \Land_i ExactlyOneOf(b_{i1}, ..., b_{ij})
      using Minisat::Lit;
      Minisat::vec<Lit> ls;
      for (auto const& v : alts) {
        ls.clear();
        // At least one must be true
        for (auto const& alt : v) {
          ls.push(Lit(alt.b));
        }
        solver.addClause(ls);
        // At most one must be true
#if USE_ATMOSTONE_CONSTRAINTS
        if (ls.size() >= 2) {
          solver.addConstraint_AtMostOne(ls);
        }
#else
        for (size_t j1 = 0; j1 < v.size(); ++j1) {
          for (size_t j2 = j1 + 1; j2 < v.size(); ++j2) {
            auto b1 = v[j1].b;
            auto b2 = v[j2].b;
            ASS_NEQ(b1, b2);
            solver.addBinary(~Lit(b1), ~Lit(b2));
          }
        }
#endif
      }

      // Add constraints:
      // \Land_j AtMostOneOf(b_{1j}, ..., b_{ij})
      for (auto const& w : possible_base_vars) {
#if USE_ATMOSTONE_CONSTRAINTS
        if (w.size() >= 2) {
          ls.clear();
          for (auto const& b : w) {
            ls.push(Lit(b));
          }
          solver.addConstraint_AtMostOne(ls);
        }
#else
        for (size_t i1 = 0; i1 < w.size(); ++i1) {
          for (size_t i2 = i1 + 1; i2 < w.size(); ++i2) {
            auto b1 = w[i1];
            auto b2 = w[i2];
            ASS_NEQ(b1, b2);
            solver.addBinary(~Lit(b1), ~Lit(b2));
          }
        }
#endif
      }

      bool res = solver.solve({});
      return res;
    }
};  // class SMTSubsumptionImpl


/****************************************************************************/
/****************************************************************************/
/****************************************************************************/












void ProofOfConcept::test(Clause* side_premise, Clause* main_premise)
{
  CALL("ProofOfConcept::test");
  std::cerr << "\% SMTSubsumption::test" << std::endl;
  std::cerr << "\% side_premise: " << side_premise->toString() << std::endl;
  std::cerr << "\% main_premise: " << main_premise->toString() << std::endl;

  bool subsumed = checkSubsumption(side_premise, main_premise, true);
  cdebug << "subsumed: " << subsumed;
}

static void escape(void* p) {
  asm volatile("" : : "g"(p) : "memory");
}

static void clobber() {
  asm volatile("" : : : "memory");
}

#include <atomic>
#include <x86intrin.h>

uint64_t rdtscp()
{
  std::atomic_signal_fence(std::memory_order_acq_rel);
  uint32_t aux;
  uint64_t result = __rdtscp(&aux);
  std::atomic_signal_fence(std::memory_order_acq_rel);
  return result;
}


// Google benchmark library
// https://github.com/google/benchmark
#include <benchmark/benchmark.h>


void bench_smt_alloc(benchmark::State& state, SubsumptionInstance instance)
{
  for (auto _ : state) {
    SMTSubsumptionImpl smt_impl;
    bool smt_result = smt_impl.checkSubsumption(instance.side_premise, instance.main_premise);
    benchmark::DoNotOptimize(smt_result);
    if (smt_result != instance.subsumed) {
      state.SkipWithError("Wrong result!");
      return;
    }
  }
}

void bench_orig_alloc(benchmark::State& state, SubsumptionInstance instance)
{
  for (auto _ : state) {
    OriginalSubsumption::Impl orig_impl;
    bool orig_result = orig_impl.checkSubsumption(instance.side_premise, instance.main_premise);
    benchmark::DoNotOptimize(orig_result);
    if (orig_result != instance.subsumed) {
      state.SkipWithError("Wrong result!");
      return;
    }
  }
}

void bench_orig_reuse(benchmark::State& state, SubsumptionInstance instance)
{
  OriginalSubsumption::Impl orig_impl;
  benchmark::ClobberMemory();
  for (auto _ : state) {
    bool orig_result = orig_impl.checkSubsumption(instance.side_premise, instance.main_premise);
    benchmark::DoNotOptimize(orig_result);
    if (orig_result != instance.subsumed) {
      state.SkipWithError("Wrong result!");
      return;
    }
  }
}


void ProofOfConcept::benchmark_micro(vvector<SubsumptionInstance> instances)
{
  CALL("ProofOfConcept::benchmark_micro");
  std::cerr << "\% SMTSubsumption: micro-benchmarking " << instances.size() << " instances" << std::endl;
#if VDEBUG
  std::cerr << "\n\n\nWARNING: compiled in debug mode!\n\n\n" << std::endl;
#endif

  // while (true) {
  //   uint64_t t1 = rdtscp();
  //   uint64_t t2 = rdtscp();
  //   std::cerr << t2 - t1 << std::endl;
  // }

  vvector<char*> args = {
    "vampire-sbench-micro",
    // "--benchmark_repetitions=10",  // Enable this to get mean/median/stddev
    // "--benchmark_report_aggregates_only=true",
    // "--benchmark_display_aggregates_only=true",
    // "--help",
  };
  std::cerr << "sizeof args = " << args.size() << std::endl;
  int argc = args.size();

  BYPASSING_ALLOCATOR;  // google-benchmark needs its own memory

  // for (auto instance : instances) {
  for (int i = 0; i < 5; ++i) {
    auto instance = instances[i];
    std::string name;

    name = "smt_alloc_" + std::to_string(instance.number);
    benchmark::RegisterBenchmark(name.c_str(), bench_smt_alloc, instance);
    // name = "smt_reuse_" + std::to_string(instance.number);
    // benchmark::RegisterBenchmark(name.c_str(), bench_smt_reuse, instance);

    name = "orig_alloc_" + std::to_string(instance.number);
    benchmark::RegisterBenchmark(name.c_str(), bench_orig_alloc, instance);
    name = "orig_reuse_" + std::to_string(instance.number);
    benchmark::RegisterBenchmark(name.c_str(), bench_orig_reuse, instance);
  }

  benchmark::Initialize(&argc, args.data());
  benchmark::RunSpecifiedBenchmarks();

  return;
}

void ProofOfConcept::benchmark_micro1(SubsumptionInstance instance)
{
  CALL("ProofOfConcept::benchmark_micro1");
  // TODO return results

  clobber();

  // TODO: this includes all allocation overhead, which is not what we want
  using namespace std::chrono;
  steady_clock::time_point smt_ts_begin = steady_clock::now();
  clobber();
  // uint64_t c1 = rdtscp();
  SMTSubsumptionImpl smt_impl;
  bool smt_result = smt_impl.checkSubsumption(instance.side_premise, instance.main_premise);
  // uint64_t c2 = rdtscp();
  clobber();
  steady_clock::time_point smt_ts_end = steady_clock::now();
  steady_clock::duration smt_duration = smt_ts_end - smt_ts_begin;

  clobber();

  steady_clock::time_point orig_ts_begin = steady_clock::now();
  clobber();
  OriginalSubsumption::Impl orig_impl;
  // uint64_t c1 = rdtscp();
  bool orig_result = orig_impl.checkSubsumption(instance.side_premise, instance.main_premise);
  // uint64_t c2 = rdtscp();
  clobber();
  steady_clock::time_point orig_ts_end = steady_clock::now();
  steady_clock::duration orig_duration = orig_ts_end - orig_ts_begin;

  clobber();


  std::cout << "Instance #" << instance.number << ": ";
  std::cout << "SMTS: " << fmt_nanosecs(smt_duration) << " / ";
  std::cout << "Orig: " << fmt_nanosecs(orig_duration);
  if (smt_duration < orig_duration) {
    std::cout << "  !!!!!!";
  }
  std::cout << std::endl;
  // std::cout << "SMT Subsumption: rdtscp: " << c2 - c1 << std::endl;

  if (smt_result != instance.subsumed) {
    std::cout << "ERROR: wrong result!" << std::endl;
  }
}


bool ProofOfConcept::checkSubsumption(Kernel::Clause* side_premise, Kernel::Clause* main_premise, bool debug_messages)
{
  CALL("ProofOfConcept::checkSubsumption");
  // debug_messages = true;

  if (debug_messages) {
    cdebug << "SMTSubsumption:";
    cdebug << "Side premise (base):     " << side_premise->toString();
    cdebug << "Main premise (instance): " << main_premise->toString();

    static_assert(alignof(Minisat::Solver) == 8, "");
    static_assert(alignof(Minisat::Solver*) == 8, "");
    static_assert(alignof(Minisat::Clause) == 4, "");
    static_assert(alignof(Minisat::Clause*) == 8, "");
    static_assert(sizeof(Minisat::Clause) == 8, "");
    static_assert(sizeof(Minisat::Clause*) == 8, "");
  }

  Minisat::Solver solver;
  if (debug_messages) {
    solver.verbosity = 2;
  }

  // Matching for subsumption checks whether
  //
  //      side_premise\theta \subseteq main_premise
  //
  // holds.

  LiteralMiniIndex const main_premise_mini_index(main_premise);

  // Pre-matching
  // Determine which literals of the side_premise can be matched to which
  // literals of the main_premise when considered on their own.
  // Along with this, we create variables b_ij and the mapping for substitution
  // constraints.
  vvector<vvector<Alt>> alts;
  alts.reserve(side_premise->length());

  // for each instance literal (of main_premise),
  // the possible variables indicating a match with the instance literal
  vvector<vvector<Minisat::Var>> possible_base_vars;
  // start with empty vector for each instance literal
  possible_base_vars.resize(main_premise->length());

  SubstitutionTheoryConfiguration stc;

  for (unsigned i = 0; i < side_premise->length(); ++i) {
    Literal* base_lit = side_premise->literals()[i];

    vvector<Alt> base_lit_alts;

    // if (debug_messages) {
    //   std::cerr
    //     << std::left << std::setw(20) << base_lit->toString()
    //     << " -> ";
    // }

    // TODO: use LiteralMiniIndex here (need to extend InstanceIterator to a version that returns the binder)
    // LiteralMiniIndex::InstanceIterator inst_it(main_premise_mini_index, base_lit, false);
    for (unsigned j = 0; j < main_premise->length(); ++j) {
      Literal* inst_lit = main_premise->literals()[j];

      if (!Literal::headersMatch(base_lit, inst_lit, false)) {
        continue;
      }

      MapBinder binder;

      binder.reset();
      if (base_lit->arity() == 0 || MatchingUtils::matchArgs(base_lit, inst_lit, binder)) {
        Minisat::Var b = solver.newVar();

        // if (debug_messages) {
        //   if (!base_lit_alts.empty()) {
        //     std::cerr << " | ";
        //   }
        //   std::cerr << std::right << std::setw(20) << inst_lit->toString() << " [b_" << b << "]";
        // }

        if (binder.bindings().size() > 0) {
          ASS(!base_lit->ground());
          auto atom = SubstitutionAtom::from_binder(binder);
          // std::cerr << "atom: " << atom << std::endl;
          stc.register_atom(b, std::move(atom));
        } else {
          ASS(base_lit->ground());
          ASS_EQ(base_lit, inst_lit);
          // TODO: in this case, at least for subsumption, we should skip this base_lit and this inst_list.
          // probably best to have a separate loop first that deals with ground literals? since those are only pointer equality checks.
          //
          // For now, just register an empty substitution atom.
          auto atom = SubstitutionAtom::from_binder(binder);
          stc.register_atom(b, std::move(atom));
        }

        base_lit_alts.push_back({
          .lit = inst_lit,
          .j = j,
          .b = b,
          .reversed = false,
        });
        possible_base_vars[j].push_back(b);
      }

      if (base_lit->commutative()) {
        ASS_EQ(base_lit->arity(), 2);
        ASS_EQ(inst_lit->arity(), 2);
        binder.reset();
        if (MatchingUtils::matchReversedArgs(base_lit, inst_lit, binder)) {
          // if (debug_messages) {
          //   if (!base_lit_alts.empty()) {
          //     std::cerr << " | ";
          //   }
          //   std::cerr << "REV: " << std::left << std::setw(20) << inst_lit->toString();
          // }

          auto atom = SubstitutionAtom::from_binder(binder);
          // std::cerr << "atom: " << atom << std::endl;

          Minisat::Var b = solver.newVar();
          stc.register_atom(b, std::move(atom));

          // Minisat::Var b = solver.newVar();
          base_lit_alts.push_back({
            .lit = inst_lit,
            .j = j,
            .b = b,
            .reversed = true,
          });
          possible_base_vars[j].push_back(b);
        }
      }
    }

    // if (debug_messages) {
    //   std::cerr << std::endl;
    // }

    alts.push_back(std::move(base_lit_alts));
  }

  solver.setSubstitutionTheory(std::move(stc));

  // Pre-matching done
  for (auto const& v : alts) {
    if (v.empty()) {
      if (debug_messages) {
        cdebug << "There is a base literal without any possible matches => abort";
      }
      return false;
    }
  }

#define USE_ATMOSTONE_CONSTRAINTS 1

  // TODO: for these we can add special constraints to MiniSAT! see paper for idea
  // Add constraints:
  // \Land_i ExactlyOneOf(b_{i1}, ..., b_{ij})
  using Minisat::Lit;
  for (auto const& v : alts) {
    // At least one must be true
    Minisat::vec<Lit> ls;
    for (auto const& alt : v) {
      ls.push(Lit(alt.b));
    }
    solver.addClause(ls);
    // At most one must be true
#if USE_ATMOSTONE_CONSTRAINTS
    if (ls.size() >= 2) {
      solver.addConstraint_AtMostOne(ls);
    }
#else
    for (size_t j1 = 0; j1 < v.size(); ++j1) {
      for (size_t j2 = j1 + 1; j2 < v.size(); ++j2) {
        auto b1 = v[j1].b;
        auto b2 = v[j2].b;
        ASS_NEQ(b1, b2);
        solver.addBinary(~Lit(b1), ~Lit(b2));
      }
    }
#endif
  }

  // Add constraints:
  // \Land_j AtMostOneOf(b_{1j}, ..., b_{ij})
  for (auto const& w : possible_base_vars) {
#if USE_ATMOSTONE_CONSTRAINTS
    if (w.size() >= 2) {
      Minisat::vec<Lit> ls;
      for (auto const& b : w) {
        ls.push(Lit(b));
      }
      solver.addConstraint_AtMostOne(ls);
    }
#else
    for (size_t i1 = 0; i1 < w.size(); ++i1) {
      for (size_t i2 = i1 + 1; i2 < w.size(); ++i2) {
        auto b1 = w[i1];
        auto b2 = w[i2];
        ASS_NEQ(b1, b2);
        solver.addBinary(~Lit(b1), ~Lit(b2));
      }
    }
#endif
  }

  if (debug_messages) {
    cdebug << "ok before solving? " << solver.okay();
    cdebug << "solving";
  }
  bool res = solver.solve({});
  if (debug_messages) {
    cdebug << "Result: " << res;
    cdebug << "ok: " << solver.okay();
  }
  return res;
}


// Example commutativity:
// side: f(x) = y
// main: f(d) = f(e)
// possible matchings:
// - x->d, y->f(e)
// - x->e, y->f(d)

// Example by Bernhard re. problematic subsumption demodulation:
// side: x1=x2 or x3=x4 or x5=x6 or x7=x8
// main: x9=x10 or x11=x12 or x13=14 or P(t)


// TODO: subsumption resolution
// maybe we can extend the subsumption instance easily?
// Add a flag (i.e., a boolean variable that's to be used as assumption)
//  to switch between subsumption and subsumption resolution.
// But other SR-clauses are only generated after checking S.