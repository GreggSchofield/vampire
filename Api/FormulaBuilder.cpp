/**
 * @file FormulaBuilder.cpp
 * Implements class FormulaBuilder.
 */

#include "FormulaBuilder.hpp"

#include "Helper_Internal.hpp"

#include "Debug/Assertion.hpp"

#include "Lib/DArray.hpp"
#include "Lib/Environment.hpp"
#include "Lib/Map.hpp"
#include "Lib/Metaiterators.hpp"
#include "Lib/VirtualIterator.hpp"

#include "Kernel/BDD.hpp"
#include "Kernel/Clause.hpp"
#include "Kernel/Connective.hpp"
#include "Kernel/Formula.hpp"
#include "Kernel/FormulaUnit.hpp"
#include "Kernel/Inference.hpp"
#include "Kernel/Signature.hpp"
#include "Kernel/Term.hpp"
#include "Kernel/TermIterators.hpp"
#include "Kernel/Unit.hpp"

#include "Shell/Parser.hpp"
#include "Shell/TPTP.hpp"


using namespace std;
using namespace Lib;
using namespace Shell;

namespace Api
{

FormulaBuilder::FormulaBuilder(bool checkNames, bool checkBindingBoundVariables)
{
  CALL("FormulaBuilder::FormulaBuilder");

  _aux->_checkNames=checkNames;
  _aux->_checkBindingBoundVariables=checkBindingBoundVariables;
}

Var FormulaBuilder::var(const string& varName)
{
  CALL("FormulaBuilder::var");

  return _aux->getVar(varName);
}

Function FormulaBuilder::function(const string& funName,unsigned arity)
{
  CALL("FormulaBuilder::function");

  if(_aux->_checkNames) {
    if(!islower(funName[0])) {
      throw InvalidTPTPNameException("Function name must start with a lowercase character", funName);
    }
    //TODO: add further checks
  }

  return env.signature->addFunction(funName, arity);
}

Function FormulaBuilder::predicate(const string& predName,unsigned arity)
{
  CALL("FormulaBuilder::predicate");

  if(_aux->_checkNames) {
    if(!islower(predName[0])) {
      throw InvalidTPTPNameException("Predicate name must start with a lowercase character", predName);
    }
    //TODO: add further checks
  }

  return env.signature->addPredicate(predName, arity);
}

Term FormulaBuilder::varTerm(const Var& v)
{
  CALL("FormulaBuilder::varTerm");

  Term res(Kernel::TermList(v,false));
  res._aux=_aux; //assign the correct helper object
  return res;
}

Term FormulaBuilder::term(const Function& f,const Term* args)
{
  CALL("FormulaBuilder::term");

  return _aux->term(f,args,env.signature->functionArity(f));
}

Formula FormulaBuilder::atom(const Predicate& p, const Term* args, bool positive)
{
  CALL("FormulaBuilder::atom");

  return _aux->atom(p,positive, args,env.signature->predicateArity(p));
}

Formula FormulaBuilder::equality(const Term& lhs,const Term& rhs, bool positive)
{
  CALL("FormulaBuilder::equality");

  Literal* lit=Kernel::Literal::createEquality(positive, lhs, rhs);
  Formula res(new Kernel::AtomicFormula(lit));
  res._aux=_aux; //assign the correct helper object
  return res;
}

Formula FormulaBuilder::trueFormula()
{
  CALL("FormulaBuilder::trueFormula");

  Formula res(new Kernel::Formula(true));
  res._aux=_aux; //assign the correct helper object
  return res;
}

Formula FormulaBuilder::falseFormula()
{
  CALL("FormulaBuilder::falseFormula");

  Formula res(new Kernel::Formula(false));
  res._aux=_aux; //assign the correct helper object
  return res;
}

Formula FormulaBuilder::negation(const Formula& f)
{
  CALL("FormulaBuilder::negation");

  if(f._aux!=_aux) {
    throw FormulaBuilderException("negation function called on a Formula object not built by the same FormulaBuilder object");
  }

  Formula res(new Kernel::NegatedFormula(f.form));
  res._aux=_aux; //assign the correct helper object
  return res;
}

Formula FormulaBuilder::formula(Connective c,const Formula& f1,const Formula& f2)
{
  CALL("FormulaBuilder::formula(Connective,const Formula&,const Formula&)");

  if(f1._aux!=_aux || f2._aux!=_aux) {
    throw FormulaBuilderException("formula function called on a Formula object not built by the same FormulaBuilder object");
  }

  Kernel::Connective con;

  switch(c) {
  case AND:
    con=Kernel::AND;
    break;
  case OR:
    con=Kernel::OR;
    break;
  case IMP:
    con=Kernel::IMP;
    break;
  case IFF:
    con=Kernel::IFF;
    break;
  case XOR:
    con=Kernel::XOR;
    break;
  default:
    throw FormulaBuilderException("Invalid binary connective");
  }

  Formula res;
  switch(c) {
  case AND:
  case OR:
  {
    Kernel::FormulaList* flst=0;
    Kernel::FormulaList::push(f2.form, flst);
    Kernel::FormulaList::push(f1.form, flst);
    res=Formula(new Kernel::JunctionFormula(con, flst));
    break;
  }
  case IMP:
  case IFF:
  case XOR:
    res=Formula(new Kernel::BinaryFormula(con, f1.form, f2.form));
    break;
  default:
    ASSERTION_VIOLATION;
  }
  ASS(res.form);
  res._aux=_aux; //assign the correct helper object
  return res;
}

Formula FormulaBuilder::formula(Connective q,const Var& v,const Formula& f)
{
  CALL("FormulaBuilder::formula(Connective,const Var&,const Formula&)");

  if(f._aux!=_aux) {
    throw FormulaBuilderException("formula function called on a Formula object not built by the same FormulaBuilder object");
  }
  if(_aux->_checkBindingBoundVariables) {
    VarList* boundVars=static_cast<Kernel::Formula*>(f)->boundVariables();
    bool alreadyBound=boundVars->member(v);
    boundVars->destroy();
    if(alreadyBound) {
      throw FormulaBuilderException("Attempt to bind a variable that is already bound: "+_aux->getVarName(v));
    }
  }

  Kernel::Connective con;

  switch(q) {
  case FORALL:
    con=Kernel::FORALL;
    break;
  case EXISTS:
    con=Kernel::EXISTS;
    break;
  default:
    throw FormulaBuilderException("Invalid quantifier connective");
  }

  Kernel::Formula::VarList* varList=0;
  Kernel::Formula::VarList::push(v, varList);

  Formula res(new QuantifiedFormula(con, varList, f.form));
  res._aux=_aux; //assign the correct helper object
  return res;
}

Formula FormulaBuilder::formula(Connective c,const Formula& cond,const Formula& thenBranch,const Formula& elseBranch)
{
  CALL("FormulaBuilder::formula(Connective,const Formula&,const Formula&,const Formula&)");

  if(c!=ITE) {
    throw FormulaBuilderException("Invalid if-then-else connective");
  }

  Formula res(new IteFormula(Kernel::ITE, cond.form, thenBranch.form, elseBranch.form));
  res._aux=_aux;
  return res;
}

AnnotatedFormula FormulaBuilder::annotatedFormula(Formula f, Annotation a, string name)
{
  CALL("FormulaBuilder::annotatedFormula");

  if(f._aux!=_aux) {
    throw FormulaBuilderException("annotatedFormula function called on a Formula object not built by the same FormulaBuilder object");
  }

  Kernel::Unit::InputType inputType;
  bool negate=false;
  switch(a) {
  case AXIOM:
    inputType=Kernel::Unit::AXIOM;
    break;
  case ASSUMPTION:
    inputType=Kernel::Unit::ASSUMPTION;
    break;
  case LEMMA:
    inputType=Kernel::Unit::LEMMA;
    break;
  case CONJECTURE:
    inputType=Kernel::Unit::CONJECTURE;
    negate=true;
    break;
  }

  if(negate) {
    Formula inner(Kernel::Formula::quantify(f));
    inner._aux=_aux;
    f=negation(inner);
  }

  FormulaUnit* fures=new Kernel::FormulaUnit(f, new Kernel::Inference(Kernel::Inference::INPUT), inputType);

  if(name!="") {
    Parser::assignAxiomName(fures, name);
  }

  AnnotatedFormula res(fures);
  res._aux=_aux; //assign the correct helper object
  return res;
}


//////////////////////////////
// Convenience functions

Term FormulaBuilder::term(const Function& c)
{
  CALL("FormulaBuilder::term/0");

  return _aux->term(c,0,0);
}

Term FormulaBuilder::term(const Function& f,const Term& t)
{
  CALL("FormulaBuilder::term/1");

  return _aux->term(f,&t,1);
}

Term FormulaBuilder::term(const Function& f,const Term& t1,const Term& t2)
{
  CALL("FormulaBuilder::term/2");

  Term args[]={t1, t2};
  return _aux->term(f,args,2);
}

Term FormulaBuilder::term(const Function& f,const Term& t1,const Term& t2,const Term& t3)
{
  CALL("FormulaBuilder::term/3");

  Term args[]={t1, t2, t3};
  return _aux->term(f,args,3);
}

Formula FormulaBuilder::formula(const Predicate& p)
{
  CALL("FormulaBuilder::formula/0");

  return _aux->atom(p,true,0,0);
}

Formula FormulaBuilder::formula(const Predicate& p,const Term& t)
{
  CALL("FormulaBuilder::formula/1");

  return _aux->atom(p,true,&t,1);
}

Formula FormulaBuilder::formula(const Predicate& p,const Term& t1,const Term& t2)
{
  CALL("FormulaBuilder::formula/2");

  Term args[]={t1, t2};
  return _aux->atom(p,true,args,2);
}

Formula FormulaBuilder::formula(const Predicate& p,const Term& t1,const Term& t2,const Term& t3)
{
  CALL("FormulaBuilder::formula/3");

  Term args[]={t1, t2, t3};
  return _aux->atom(p,true,args,3);
}


//////////////////////////////
// Wrapper implementation

Term::Term(Kernel::TermList t)
{
  content=t.content();
}

Term::Term(Kernel::TermList t, ApiHelper aux) : _aux(aux)
{
  content=t.content();
}

string Term::toString() const
{
  CALL("Term::toString");

  if(isNull()) {
    throw ApiException("Term not initialized");
  }
  return _aux->toString(static_cast<Kernel::TermList>(*this));
}

bool Term::isVar() const
{
  CALL("Term::isVar");

  if(isNull()) {
    throw ApiException("Term not initialized");
  }
  return static_cast<Kernel::TermList>(*this).isVar();
}

Var Term::var() const
{
  CALL("Term::var");

  if(isNull()) {
    throw ApiException("Term not initialized");
  }
  if(!isVar()) {
    throw ApiException("Variable can be retrieved only for a variable term");
  }
  return static_cast<Kernel::TermList>(*this).var();
}

Function Term::functor() const
{
  CALL("Term::functor");

  if(isNull()) {
    throw ApiException("Term not initialized");
  }
  if(isVar()) {
    throw ApiException("Functor cannot be retrieved for a variable term");
  }
  return static_cast<Kernel::TermList>(*this).term()->functor();
}

Function Term::arity() const
{
  CALL("Term::arity");

  if(isNull()) {
    throw ApiException("Term not initialized");
  }
  if(isVar()) {
    throw ApiException("Arity cannot be retrieved for a variable term");
  }
  return static_cast<Kernel::TermList>(*this).term()->arity();
}

Term Term::arg(unsigned i)
{
  CALL("Term::arg");

  if(isNull()) {
    throw ApiException("Term not initialized");
  }
  if(isVar()) {
    throw ApiException("Arguments cannot be retrieved for a variable term");
  }
  if(i>=arity()) {
    throw ApiException("Argument index out of bounds");
  }
  return Term(*static_cast<Kernel::TermList>(*this).term()->nthArgument(i), _aux);
}


Term::operator Kernel::TermList() const
{
  return TermList(content);
}

string Formula::toString() const
{
  CALL("Formula::toString");

  return _aux->toString(static_cast<Kernel::Formula*>(*this));
}

bool Formula::isTrue() const
{ return form->connective()==Kernel::TRUE; }

bool Formula::isFalse() const
{ return form->connective()==Kernel::FALSE; }

bool Formula::isNegation() const
{ return form->connective()==Kernel::NOT; }

FormulaBuilder::Connective Formula::connective() const
{
  CALL("Formula::connective");

  switch(form->connective()) {
  case Kernel::LITERAL:
    ASS(form->literal()->isPositive());
    return FormulaBuilder::ATOM;
  case Kernel::AND:
    return FormulaBuilder::AND;
  case Kernel::OR:
    return FormulaBuilder::OR;
  case Kernel::IMP:
    return FormulaBuilder::IMP;
  case Kernel::IFF:
    return FormulaBuilder::IFF;
  case Kernel::XOR:
    return FormulaBuilder::XOR;
  case Kernel::NOT:
    return FormulaBuilder::NOT;
  case Kernel::FORALL:
    return FormulaBuilder::FORALL;
  case Kernel::EXISTS:
    return FormulaBuilder::EXISTS;
  case Kernel::ITE:
    return FormulaBuilder::ITE;
  case Kernel::TRUE:
    return FormulaBuilder::TRUE;
  case Kernel::FALSE:
    return FormulaBuilder::FALSE;
  case Kernel::TERM_LET:
  case Kernel::FORMULA_LET:
  default:
    ASSERTION_VIOLATION;
  }
}

Predicate Formula::predicate() const
{
  CALL("Formula::predicate");

  if(form->connective()!=Kernel::LITERAL) {
    throw ApiException("Predicate symbol can be retrieved only from atoms");
  }
  return form->literal()->functor();
}

unsigned Formula::argCnt() const
{
  CALL("Formula::argCnt");

  switch(form->connective()) {
  case Kernel::LITERAL:
    return form->literal()->arity();
  case Kernel::AND:
  case Kernel::OR:
    ASS_EQ(form->args()->length(), 2);
    return 2;
  case Kernel::IMP:
  case Kernel::IFF:
  case Kernel::XOR:
    return 2;
  case Kernel::NOT:
  case Kernel::FORALL:
  case Kernel::EXISTS:
    return 1;
  case Kernel::ITE:
    return 3;
  case Kernel::TRUE:
  case Kernel::FALSE:
    return 0;
  case Kernel::TERM_LET:
  case Kernel::FORMULA_LET:
  default:
    ASSERTION_VIOLATION;
  }
}

Formula Formula::formulaArg(unsigned i)
{
  CALL("Formula::formulaArg");

  Kernel::Formula* res = 0;
  switch(form->connective()) {
  case Kernel::LITERAL:
    throw ApiException("Formula arguments cannot be obtained from atoms");
  case Kernel::AND:
  case Kernel::OR:
    res = form->args()->nth(i);
    break;
  case Kernel::IMP:
  case Kernel::IFF:
  case Kernel::XOR:
    if(i==0) {
      res = form->left();
    } else if(i==1) {
      res = form->right();
    }
    break;
  case Kernel::NOT:
    if(i==0) {
      res = form->uarg();
    }
    break;
  case Kernel::FORALL:
  case Kernel::EXISTS:
    if(i==0) {
      res = form->qarg();
    }
    break;
  case Kernel::ITE:
    switch(i) {
    case 0:
      res = form->condArg();
      break;
    case 1:
      res = form->thenArg();
      break;
    case 2:
      res = form->elseArg();
      break;
    default:;
    }
    break;
  case Kernel::TRUE:
  case Kernel::FALSE:
    break;
  case Kernel::TERM_LET:
  case Kernel::FORMULA_LET:
  default:
    ASSERTION_VIOLATION;
  }
  if(res==0) {
    throw ApiException("Argument index out of bounds");
  }
  return Formula(res, _aux);
}

Term Formula::termArg(unsigned i)
{
  CALL("Formula::termArg");

  if(form->connective()!=Kernel::LITERAL) {
    throw ApiException("Term arguments can be obtained only from atoms");
  }
  if(form->literal()->arity()<=i) {
    throw ApiException("Argument index out of bounds");
  }
  return Term(*form->literal()->nthArgument(i), _aux);
}

StringIterator Formula::freeVars()
{
  CALL("Formula::freeVars");

  if(!form) {
    return StringIterator(VirtualIterator<string>::getEmpty());
  }
  VarList* vars=form->freeVariables();
  return _aux->getVarNames(vars);
}

StringIterator Formula::boundVars()
{
  CALL("Formula::boundVars");

  if(!form) {
    return StringIterator(VirtualIterator<string>::getEmpty());
  }
  VarList* vars=form->boundVariables();
  return _aux->getVarNames(vars);
}


string AnnotatedFormula::toString() const
{
  CALL("AnnotatedFormula::toString");

  return _aux->toString(unit);
}

string AnnotatedFormula::name() const
{
  CALL("AnnotatedFormula::toString");

  string unitName;
  if(!Parser::findAxiomName(unit, unitName)) {
    unitName="u" + Int::toString(unit->number());
  }
  return unitName;
}

StringIterator AnnotatedFormula::freeVars()
{
  CALL("AnnotatedFormula::freeVars");

  if(!unit) {
    return StringIterator(VirtualIterator<string>::getEmpty());
  }
  VarList* vl=0;
  if(unit->isClause()) {
    VarList::pushFromIterator(static_cast<Clause*>(unit)->getVariableIterator(), vl);
  }
  else {
    vl=static_cast<FormulaUnit*>(unit)->formula()->freeVariables();
  }
  return _aux->getVarNames(vl);
}

StringIterator AnnotatedFormula::boundVars()
{
  CALL("AnnotatedFormula::boundVars");

  if(!unit || unit->isClause()) {
    return StringIterator(VirtualIterator<string>::getEmpty());
  }
  VarList* vl=static_cast<FormulaUnit*>(unit)->formula()->boundVariables();
  return _aux->getVarNames(vl);
}

FormulaBuilder::Annotation AnnotatedFormula::annotation() const
{
  CALL("AnnotatedFormula::annotation");

  switch(unit->inputType()) {
  case Kernel::Unit::AXIOM:
    return FormulaBuilder::AXIOM;
  case Kernel::Unit::ASSUMPTION:
    return FormulaBuilder::ASSUMPTION;
  case Kernel::Unit::LEMMA:
    return FormulaBuilder::LEMMA;
  case Kernel::Unit::CONJECTURE:
    return FormulaBuilder::CONJECTURE;
  default:
    ASSERTION_VIOLATION;
  }
}

Formula AnnotatedFormula::formula()
{
  CALL("AnnotatedFormula::formula");

  if(unit->isClause()) {
    throw ApiException("Cannot retrieve formula from clausified object");
  }

  Kernel::Formula* form = static_cast<FormulaUnit*>(unit)->formula();

  if(unit->inputType()!=Kernel::Unit::CONJECTURE) {
    return Formula(form, _aux);
  }

  //if we have a conjecture, we need to return negated formula
  if(form->connective()==Kernel::NOT) {
    return Formula(form->uarg(), _aux);
  }

  Kernel::Formula* negated = new Kernel::NegatedFormula(Kernel::Formula::quantify(form));
  return Formula(negated, _aux);
}

//////////////////////////////
// StringIterator implementation

StringIterator::StringIterator(const VirtualIterator<string>& vit)
{
  CALL("StringIterator::StringIterator");

  _impl=new VirtualIterator<string>(vit);
}

StringIterator::~StringIterator()
{
  CALL("StringIterator::~StringIterator");

  if(_impl) {
    delete _impl;
  }
}

StringIterator::StringIterator(const StringIterator& it)
{
  CALL("StringIterator::StringIterator(StringIterator&)");

  if(it._impl) {
    _impl=new VirtualIterator<string>(*it._impl);
  }
  else {
    _impl=0;
  }
}

StringIterator& StringIterator::operator=(const StringIterator& it)
{
  CALL("StringIterator::operator=");

  VirtualIterator<string>* oldImpl=_impl;

  if(it._impl) {
    _impl=new VirtualIterator<string>(*it._impl);
  }
  else {
    _impl=0;
  }

  if(oldImpl) {
    delete oldImpl;
  }

  return *this;
}

bool StringIterator::hasNext()
{
  CALL("StringIterator::hasNext");

  if(!_impl) {
    return false;
  }

  return _impl->hasNext();
}

string StringIterator::next()
{
  CALL("StringIterator::next");

  if(!hasNext()) {
    throw FormulaBuilderException("next() function called on a StringIterator object that contains no more elements");
  }
  ASS(_impl);
  return _impl->next();
}

} //namespace Api


//////////////////////////////
// Output implementation

ostream& operator<< (ostream& str,const Api::Formula& f)
{
  CALL("operator<< (ostream&,const Api::Formula&)");
  return str<<f.toString();
}

ostream& operator<< (ostream& str,const Api::AnnotatedFormula& af)
{
  CALL("operator<< (ostream&,const Api::AnnotatedFormula&)");
  return str<<af.toString();
}

