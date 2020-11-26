/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */
#include "GaussianVariableElimination.hpp"
#include "Kernel/Rebalancing.hpp"
#include "Kernel/Rebalancing/Inverters.hpp"
#include "Kernel/Clause.hpp"
#include "Kernel/EqHelper.hpp"
#include "Kernel/InterpretedLiteralEvaluator.hpp"
#include "Inferences/InterpretedEvaluation.hpp"

#define DEBUG(...)  //DBG(__VA_ARGS__)

namespace Inferences {
  using Balancer = Kernel::Rebalancing::Balancer<Kernel::Rebalancing::Inverters::NumberTheoryInverter>;

Clause* GaussianVariableElimination::simplify(Clause* in) 
{
  CALL("GaussianVariableElimination::simplify")
  ASS(in)
  
  auto performStep = [&](Clause& cl) -> Clause& {

    for(unsigned i = 0; i < cl.size(); i++) {
      auto& lit = *cl[i];
      if (lit.isEquality() && lit.isNegative()) { 
        for (auto b : Balancer(lit)) {

          /* found a rebalancing: lhs = rhs[lhs, ...] */
          auto lhs = b.lhs();
          auto rhs = b.buildRhs();
          //TODO simplify here
          ASS_REP(lhs.isVar(), lhs);

          if (!rhs.containsSubterm(lhs)) {
            /* lhs = rhs[...] */
            DEBUG(lhs, " -> ", rhs);

            return *rewrite(cl, lhs, rhs, i);
          }
        }
      }
    }
    return cl;

  };

  return &performStep(*in);
  // Clause* out = in;
  // while(true) {
  //   Clause* step = &performStep(*out);
  //   if (step == out) 
  //     break;
  //   else 
  //     out = step;
  // }

  // return out;
}

Clause* GaussianVariableElimination::rewrite(Clause& cl, TermList find, TermList replace, unsigned skipLiteral) const 
{
  CALL("GaussianVariableElimination::rewrite");
  // Inference& inf = *new Inference1(Kernel::InferenceRule::GAUSSIAN_VARIABLE_ELIMINIATION, &cl);
  Inference inf(SimplifyingInference1(Kernel::InferenceRule::GAUSSIAN_VARIABLE_ELIMINIATION, &cl));

  auto sz = cl.size() - 1;
  Clause& out = *new(sz) Clause(sz, inf); 
  for (unsigned i = 0; i < skipLiteral; i++) {
    out[i] = EqHelper::replace(cl[i], find, replace);
  }

  for (unsigned i = skipLiteral; i < sz; i++)  {
    out[i] = EqHelper::replace(cl[i+1], find, replace);
  }

  return &out;
}

} // namespace Inferences 
