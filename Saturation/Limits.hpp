
/*
 * File Limits.hpp.
 *
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 *
 * In summary, you are allowed to use Vampire for non-commercial
 * purposes but not allowed to distribute, modify, copy, create derivatives,
 * or use in competitions. 
 * For other uses of Vampire please contact developers for a different
 * licence, which we will make an effort to provide. 
 */
/**
 * @file Limits.hpp
 * Defines class Limits
 *
 */

#ifndef __Limits__
#define __Limits__

#include "Forwards.hpp"

#include "Lib/Event.hpp"

#include "Kernel/Clause.hpp"


namespace Saturation
{

using namespace Lib;
using namespace Kernel;
using namespace Shell;

typedef PlainEvent LimitsChangeEvent;

class Limits
{
public:
  Limits(const Options& opt) : _ageSelectionMaxAge(UINT_MAX), _weightSelectionMaxWeight(UINT_MAX), _opt(opt) {}

  LimitsChangeEvent changedEvent;

  bool ageLimited() const { return _ageSelectionMaxAge != UINT_MAX; }
  bool weightLimited() const { return _weightSelectionMaxWeight != UINT_MAX; }

  bool fulfilsAgeLimit(Clause* c) const;
  bool fulfilsAgeLimit(unsigned age) const;
  bool fulfilsWeightLimit(Clause* cl) const;
  // note: w here denotes the weight as returned by weight().
  // this method internally takes care of computing the corresponding weightForClauseSelection.
  bool fulfilsWeightLimit(unsigned int w, unsigned int numeralWeight, bool derivedFromGoal) const;
  bool childrenPotentiallyFulfilLimits(Clause* cl, unsigned upperBoundNumSelLits) const;
  bool setLimits(int newMaxAge, int newMaxWeight);

private:
  unsigned _ageSelectionMaxAge;
  unsigned _weightSelectionMaxWeight;
  const Options& _opt;
};

};

#endif /*__Limits__*/
