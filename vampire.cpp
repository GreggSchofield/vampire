/**
 * @file vampire.cpp. Implements the top-level procedures of Vampire.
 */

#include <iostream>
#include <ostream>
#include <fstream>
#include <csignal>

#include "Debug/Tracer.hpp"

#include "Lib/Random.hpp"
#include "Lib/Set.hpp"
#include "Lib/Int.hpp"
#include "Lib/Timer.hpp"
#include "Lib/Exception.hpp"
#include "Lib/Environment.hpp"

#include "Lib/Vector.hpp"
#include "Lib/System.hpp"
#include "Lib/Metaiterators.hpp"

#include "Kernel/Signature.hpp"
#include "Kernel/Clause.hpp"
#include "Kernel/Formula.hpp"
#include "Kernel/FormulaUnit.hpp"

#include "Indexing/TermSharing.hpp"
#include "Indexing/SubstitutionTree.hpp"
#include "Indexing/LiteralMiniIndex.hpp"

#include "Shell/Options.hpp"
#include "Shell/CommandLine.hpp"
#include "Shell/TPTPLexer.hpp"
#include "Shell/TPTP.hpp"
#include "Shell/TPTPParser.hpp"
#include "Shell/Property.hpp"
#include "Shell/Preprocess.hpp"
#include "Shell/Statistics.hpp"
#include "Shell/Refutation.hpp"

#include "Saturation/SaturationAlgorithm.hpp"


#if CHECK_LEAKS
#include "Lib/MemoryLeak.hpp"
#endif

#define SPIDER 1
#define SAVE_SPIDER_PROPERTIES 1

using namespace Shell;
using namespace Saturation;
using namespace Inferences;

UnitList* globUnitList=0;

void doProving()
{
  CALL("doProving()");
  try {
    env.signature = new Kernel::Signature;
    UnitList* units;
    {
      string inputFile = env.options->inputFile();
      ifstream input(inputFile.c_str());
      TPTPLexer lexer(input);
      TPTPParser parser(lexer);
      units = parser.units();
    }

    Property property;
    property.scan(units);

    Preprocess prepro(property,*env.options);
    prepro.preprocess(units);

    globUnitList=units;

    ClauseIterator clauses=pvi( getStaticCastIterator<Clause*>(UnitList::Iterator(units)) );

    SaturationAlgorithmSP salg=SaturationAlgorithm::createFromOptions();
    salg->addInputClauses(clauses);

    SaturationResult sres(salg->saturate());
    sres.updateStatistics();
  } catch(MemoryLimitExceededException) {
    env.statistics->terminationReason=Statistics::MEMORY_LIMIT;
    env.statistics->refutation=0;
    size_t limit=Allocator::getMemoryLimit();
    //add extra 1 MB to allow proper termination
    Allocator::setMemoryLimit(limit+1000000);
  }
}

void outputResult()
{
  CALL("outputResult()");

  switch (env.statistics->terminationReason) {
  case Statistics::REFUTATION:
    env.out << "Refutation found. Thanks to Tanya!\n";
    if (env.options->proof() != Options::PROOF_OFF) {
	Shell::Refutation refutation(env.statistics->refutation,
		env.options->proof() == Options::PROOF_ON);
	refutation.output(env.out);
    }
    break;
  case Statistics::TIME_LIMIT:
    env.out << "Time limit reached!\n";
    break;
  case Statistics::MEMORY_LIMIT:
#if VDEBUG
    Allocator::reportUsageByClasses();
#endif
    env.out << "Memory limit exceeded!\n";
    break;
  default:
    env.out << "Refutation not found!\n";
    break;
  }
  env.statistics->print();
}


void vampireMode()
{
  CALL("vampireMode()");
  env.out<<env.options->testId()<<" on "<<env.options->inputFile()<<endl;
  doProving();
  outputResult();
} // vampireMode

void spiderMode()
{
  CALL("spiderMode()");
  doProving();

  switch (env.statistics->terminationReason) {
  case Statistics::REFUTATION:
    env.out << "+ ";
    break;
  case Statistics::TIME_LIMIT:
  case Statistics::MEMORY_LIMIT:
    env.out << "? ";
    break;
  default:
    env.out << "- ";
    break;
  }
  env.out << env.options->problemName() << " ";
  env.out << env.timer->elapsedDeciseconds() << " ";
  env.out << env.options->testId() << "\n";
} // spiderMode


void explainException (Exception& exception)
{
  exception.cry(env.out);
} // explainException

/**
 * The main function.
  * @since 03/12/2003 many changes related to logging
  *        and exception handling.
  * @since 10/09/2004, Manchester changed to use knowledge bases
  */
int main(int argc, char* argv [])
{
  CALL ("main");

  System::setSignalHandlers();
   // create random seed for the random number generation
  Lib::Random::setSeed(123456);

  try {
    // read the command line and interpret it
    Options options;
    Shell::CommandLine cl(argc,argv);
    cl.interpret(options);
    Allocator::setMemoryLimit(options.memoryLimit()*1000000);

    Timer timer;
    timer.start();
    env.timer = &timer;
    Indexing::TermSharing sharing;
    env.sharing = &sharing;
    env.options = &options;
    Shell::Statistics statistics;
    env.statistics = &statistics;

    switch (options.mode())
      {
      case Options::MODE_VAMPIRE:
	vampireMode();
	break;
      case Options::MODE_SPIDER:
	spiderMode();
	break;
#if VDEBUG
      default:
  	ASSERTION_VIOLATION;
#endif
      }
#if CHECK_LEAKS
    if(globUnitList) {
      MemoryLeak leak;
      leak.release(globUnitList);
    }
    delete env.signature;
    env.signature = 0;
#endif
  }
#if VDEBUG
  catch (Debug::AssertionViolationException& exception) {
#if CHECK_LEAKS
    MemoryLeak::cancelReport();
#endif
  }
#endif
  catch (Exception& exception) {
#if CHECK_LEAKS
    MemoryLeak::cancelReport();
#endif
    explainException(exception);
    env.statistics->print();

  }
  catch (std::bad_alloc& _) {
#if CHECK_LEAKS
    MemoryLeak::cancelReport();
#endif
    env.out << "Insufficient system memory" << '\n';
  }
//   delete env.allocator;

  /*cout<<"good:\t"<<LiteralMiniIndex::goodPred<<endl;
  cout<<"bad:\t"<<LiteralMiniIndex::badPred<<endl;*/
  return EXIT_SUCCESS;
} // main

